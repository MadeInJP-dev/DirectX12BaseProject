#include "ModelIO.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "ModelConverter/ModelConverter.h"

#include "Parser/tinyGLTF/tinyGLTF.h"
#include "Processor/ModelProcessor.h"

#include "../../../Common/ScopedResourceBuild.h"

namespace Engine::Resource
{
	namespace
	{
		// このスレッドで LOD モデルを読んでいる深さ。
		// LOD として読まれているモデルは、自分の LOD を読まない。
		// LOD を持てるのは LOD0 だけで、読むのを1段で止めておけば、
		// LOD 同士を指し合っていても同じモデルの読み終わりを自分で待つことが無い
		thread_local int t_lodLoadDepth = 0;
	}

	Model ModelIO::Import(const std::string& a_filePath, const ResourceBuildContext* a_pContext)
	{
		// アセットデータベースからメタファイルを検索。
		// コンテキストが無い経路では、読み込みを受け持つリソースマネージャーの持ち物を使う
		ENGINE_ERRLOG(a_pContext && a_pContext->pResourceManager, "ResourceBuildContext.pResourceManager が空です");
		auto& _assetDB = a_pContext->pAssetDatabase
			? *a_pContext->pAssetDatabase
			: a_pContext->pResourceManager->RefAssetDatabase();
		auto* _pAssetProp = _assetDB.GetAssetProperty(a_filePath);
		if (!_pAssetProp)
		{
			ENGINE_LOG("メタファイルが見つからなかったためモデルの読み込みに失敗");
			return Model();
		}

		// 優先度の高い拡張子のタイプを検索
		auto _ext = FinddExtension(_pAssetProp->extensionsVec);

		// 対応していない拡張子はここで弾く : バッチを開く前に判定する
		if (_ext != ".obmdl" && _ext != ".gltf" && _ext != ".ojmdl")
		{
			ENGINE_ERRLOG(false,"この拡張子のモデルは対応していません : %s", _ext.c_str());
			return Model();
		}

		// モデル1体分のバッチを開く
		// ここで開いたコマンドリストへ、配下のメッシュ・マテリアル・テクスチャがまとめて積まれる。
		// スコープを抜けるところで1回だけキューへ実行される。
		ResourceBuildScope _scope(a_pContext);
		const auto& _ctx = _scope.GetContext();

		// 拡張子ごとに読み込み方を変更する
		if (_ext == ".gltf")
		{
			return GLTFLoad(_ctx, a_filePath);
		}

		// .obmdl / .ojmdl はコンバート済みの独自形式
		return Load(_ctx, a_filePath);
	}
	Model ModelIO::Load(const ResourceBuildContext& a_ctx, const std::string& a_filePath)
	{
		auto _dir = Core::File::GetDirFromPath(a_filePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_filePath);
		Persistence::Archive _ar(Persistence::Archive::EMode::Load, _dir, _fileName, "mdl");

		ModelAssetData _assetData = {};

		_ar.StringField("ModelName", _assetData.name);

		_ar.GUIDVectorField("MaterialGUID", _assetData.materialGUIDs);
		_ar.GUIDVectorField("MeshGUID", _assetData.meshGUIDs);
		_ar.GUIDVectorField("AnimationGUID", _assetData.animationGUIDs);

		UINT _nodeCount = 0;
		_ar.Field("NodeCount", _nodeCount);
		_assetData.originalNodes.resize(_nodeCount);
		for (UINT _i = 0; _i < _nodeCount; ++_i)
		{
			_assetData.originalNodes[_i].Archive(_ar, _i);
		}

		_ar.VectorField("RootNodeIndices", _assetData.rootNodeIndices);
		_ar.VectorField("BoneNodeIndices", _assetData.boneNodeIndices);
		_ar.VectorField("MeshNodeIndices", _assetData.meshNodeIndices);
		_ar.VectorField("CollisionMeshNodeIndices", _assetData.collisionMeshNodeIndices);
		_ar.VectorField("DrawMeshNodeIndices", _assetData.drawMeshNodeIndices);

		// ボーンレイヤー : ノードの範囲外を指すものは捨てる
		ArchiveBoneMasks(_ar, _assetData.boneMasks);
		for (auto& _mask : _assetData.boneMasks)
		{
			std::erase_if(_mask.bones, [&](const BoneWeight& a_bone) { return a_bone.nodeIndex >= _nodeCount; });
		}

		// LOD : ボーンレイヤーの後ろ
		ArchiveLodLevels(_ar, _assetData.lodLevels);

		ModelRuntimeData _runtimeData = {};

		// ---- 参照しているデータの復元 ----
		// コンテキストを渡すことで、配下のリソースも同じバッチへ積まれる
		auto& _resourceManager = *a_ctx.pResourceManager;

		for (const auto& _guid : _assetData.materialGUIDs)
		{
			auto _handle = _resourceManager.LoadImmediate<Material>(_guid, &a_ctx);
			_runtimeData.materials.push_back(std::move(_handle));
		}
		for (const auto& _guid : _assetData.meshGUIDs)
		{
			auto _handle = _resourceManager.LoadImmediate<Mesh>(_guid, &a_ctx);
			_runtimeData.meshes.push_back(std::move(_handle));
		}
		for (const auto& _guid : _assetData.animationGUIDs)
		{
			auto _handle = _resourceManager.LoadImmediate<AnimationData>(_guid, &a_ctx);
			_runtimeData.animations.push_back(std::move(_handle));
		}

		// 各ノードに名前のハッシュ値をつける
		for (auto& _node : _assetData.originalNodes)
		{
			_node.nodeNameHash = Core::String::ToHash(_node.name);
		}

		// 描画用コマンドの構築
		CreateDrawCmd(a_ctx, _assetData, _runtimeData);

		// LOD モデル
		LoadLodModels(a_ctx, a_filePath, _assetData, _runtimeData);

		return Model(std::move(_assetData), std::move(_runtimeData));
	}
	Model ModelIO::GLTFLoad(const ResourceBuildContext& a_ctx, const std::string& a_filePath)
	{
		//----------------------------------------------------------------
		// パース : 読み込んだままの中間素材を得る
		//----------------------------------------------------------------
		auto _rawModel = GLTF::Load(a_filePath);

		//----------------------------------------------------------------
		// 加工 : 座標系の変換など、エンジンの仕様へ合わせる
		//----------------------------------------------------------------
		// TODO: インポート設定はアセットの .meta から読めるようにする
		Processor::ModelImportSettings _importSettings = {};
		Processor::ModelProcessor::Process(_rawModel, _importSettings);

		//----------------------------------------------------------------
		// コンバート : エンジン形式へ詰め替え、GPU命令をバッチへ積む
		//----------------------------------------------------------------
		auto _model = Converter::ModelConverter::ConvertModelData(a_ctx, a_filePath, _rawModel);

		//----------------------------------------------------------------
		// 登録 : ResourceManagerへ実体を預けてハンドル化する
		//----------------------------------------------------------------
		auto& _resourceManager = *a_ctx.pResourceManager;

		ModelAssetData _assetData = {};
		_assetData.name = Core::File::GetFileName(a_filePath);
		_assetData.originalNodes = std::move(_model.originalNodes);
		_assetData.rootNodeIndices = _model.rootNodeIndices;
		_assetData.boneNodeIndices = _model.boneNodeIndices;
		_assetData.meshNodeIndices = _model.meshNodeIndices;
		_assetData.collisionMeshNodeIndices = _model.collisionMeshNodeIndices;
		_assetData.drawMeshNodeIndices = _model.drawMeshNodeIndices;

		ModelRuntimeData _runtimeData = {};
		for (auto& _material : _model.MaterialVec)
		{
			auto _handle = _resourceManager.Add(std::move(_material));
			_runtimeData.materials.push_back(_handle);
		}
		for (auto& _mesh : _model.MeshVec)
		{
			auto _handle = _resourceManager.Add(std::move(_mesh));
			_runtimeData.meshes.push_back(_handle);
		}
		for (auto& _ani : _model.AnimationVec)
		{
			auto _handle = _resourceManager.Add(std::move(_ani));
			_runtimeData.animations.push_back(_handle);
		}

		// 各ノードに名前のハッシュ値をつける
		for (auto& _node : _assetData.originalNodes)
		{
			_node.nodeNameHash = Core::String::ToHash(_node.name);
		}

		// 描画用コマンドの構築
		CreateDrawCmd(a_ctx, _assetData, _runtimeData);

		return Model(std::move(_assetData), std::move(_runtimeData));
	}

	void ModelIO::CreateDrawCmd(const ResourceBuildContext& a_ctx, const ModelAssetData& a_modelAssetData, ModelRuntimeData& a_runtimeData)
	{
		auto& _resourceManager = *a_ctx.pResourceManager;

		// 描画時用に事前コマンド構築
		for (auto& _meshNodeIdx : a_modelAssetData.drawMeshNodeIndices)
		{
			// 描画用ノードからメッシュのインデックスを取得
			for (auto& _meshIdx : a_modelAssetData.originalNodes[_meshNodeIdx].meshIndices)
			{
				// 描画メッシュハンドルを取得
				const auto& _meshHandle = a_runtimeData.meshes[_meshIdx];
				auto* _pMesh = _resourceManager.Ref(_meshHandle);
				if (!_pMesh)
				{
					ENGINE_ERRLOG(false, "メッシュが読み込まれていません");
					continue;
				}

				// サブセットごとに描画アイテムを集める
				for (UINT _subIdx = 0; _subIdx < _pMesh->GetMetaData().subsets.size(); ++_subIdx)
				{
					// 面が一枚もなければスキップ
					if (_pMesh->GetMetaData().subsets[_subIdx].faceCount == 0) continue;

					// マテリアルハンドル取得
					const auto& _materialHandle = a_runtimeData.materials[_pMesh->GetMetaData().subsets[_subIdx].materialNumber];
					auto* _pMate = _resourceManager.Ref(_materialHandle);
					if (!_pMate)
					{
						ENGINE_ERRLOG(false, "マテリアルが読み込まれていません");
						continue;
					}

					// コマンド作成
					// 実体はリソースマネージャーに置いたままにして、参照先の番号だけを控える
					ModelDrawCommand _cmd = {};
					_cmd.nodeIndex = static_cast<uint16_t>(_meshNodeIdx);
					_cmd.meshHandle = _meshHandle;
					_cmd.materialHandle = _materialHandle;
					_cmd.subIdx = static_cast<uint8_t>(_subIdx);
					_cmd.alphaMode = _pMate->alphaMode;
					a_runtimeData.drawCommands.push_back(_cmd);
				}
			}
		}

		//--------------------------------------------------------------------------------
		// モデル全体を囲む球 : 描画するメッシュの球を、ノードの変換を掛けてから合わせる。
		// LOD を選ぶときに、画面にどれだけの大きさで映るかを測るのに使う
		//--------------------------------------------------------------------------------
		bool _hasBounds = false;
		for (const auto& _cmd : a_runtimeData.drawCommands)
		{
			const auto* _pMesh = _resourceManager.Get(_cmd.meshHandle);
			if (!_pMesh) continue;

			const DirectX::XMMATRIX _nodeMat = Math::DX::Load(a_modelAssetData.originalNodes[_cmd.nodeIndex].worldTransform);
			DirectX::BoundingSphere _sphere = {};
			_pMesh->GetMetaData().bSphere.Transform(_sphere, _nodeMat);

			if (_hasBounds)
			{
				DirectX::BoundingSphere::CreateMerged(a_runtimeData.localBounds, a_runtimeData.localBounds, _sphere);
			}
			else
			{
				a_runtimeData.localBounds = _sphere;
				_hasBounds = true;
			}
		}
	}

	void ModelIO::LoadLodModels(const ResourceBuildContext& a_ctx, const std::string& a_filePath, const ModelAssetData& a_modelAssetData, ModelRuntimeData& a_runtimeData)
	{
		a_runtimeData.lodModels.clear();
		if (a_modelAssetData.lodLevels.empty()) return;

		// LOD として読まれているモデルは、自分の LOD を読まない
		if (t_lodLoadDepth > 0)
		{
			ENGINE_LOG("LOD モデルが自分の LOD を持っているため無視します(LOD を持てるのは LOD0 だけ) : %s", a_filePath.c_str());
			return;
		}

		auto& _resourceManager = *a_ctx.pResourceManager;
		auto& _assetDB = a_ctx.pAssetDatabase
			? *a_ctx.pAssetDatabase
			: _resourceManager.RefAssetDatabase();

		// 自分自身を LOD に指していたら読まない(読み終わりを自分で待つことになる)
		const auto* _pSelfProp = _assetDB.GetAssetProperty(a_filePath);
		const Core::GUID _selfGUID = _pSelfProp ? _pSelfProp->guid : Core::GUID();

		++t_lodLoadDepth;
		for (const auto& _level : a_modelAssetData.lodLevels)
		{
			ResourceRef<Model> _lodModel = {};

			const auto* _pProp = _level.modelGUID.IsValid() ? _assetDB.GetAssetProperty(_level.modelGUID) : nullptr;
			if (_pProp && _pProp->type == "Model" && !(_level.modelGUID == _selfGUID))
			{
				_lodModel = _resourceManager.LoadImmediate<Model>(_level.modelGUID, &a_ctx);
			}

			// 読めなかった段も並びを保つために空のまま入れる(描くときに飛ばす)
			a_runtimeData.lodModels.push_back(std::move(_lodModel));
		}
		--t_lodLoadDepth;
	}


	std::string ModelIO::FinddExtension(const std::vector<std::string>& a_extVed)
	{
		// 数字が小さいほど優先。None は「まだ何も見つかっていない」
		enum class EExtTier
		{
			OB,				// binary ".obmdl",
			Default,		// デフォルトのアセット拡張子".gltf",
			OJ,				// JSON  ".ojmdl"
			None			// 未決定
		};

		std::string _res = "";

		// 最下位(OJ)から始めると ".ojmdl" の判定( _tier > OJ )が絶対に成立せず、
		// .ojmdl しか持たないモデルが「拡張子なし」として弾かれてしまう。
		// 未決定を表す段を用意して、そこから始めること
		EExtTier _tier = EExtTier::None;
		for (size_t _i = 0; _i < a_extVed.size(); ++_i)
		{
			if (a_extVed[_i] == ".obmdl")
			{
				_res = ".obmdl";
				break;
			}
			else if (a_extVed[_i] == ".gltf")
			{
				if (_tier > EExtTier::Default)
				{
					_res = ".gltf";
					_tier = EExtTier::Default;
				}
			}
			else if (a_extVed[_i] == ".ojmdl")
			{
				if (_tier > EExtTier::OJ)
				{
					_res = ".ojmdl";
					_tier = EExtTier::OJ;
				}
			}
		}

		return _res;
	}
}
