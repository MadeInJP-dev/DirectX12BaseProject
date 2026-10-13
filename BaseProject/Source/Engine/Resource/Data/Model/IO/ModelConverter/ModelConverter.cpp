#include "ModelConverter.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

namespace Engine::Resource::Converter
{
	namespace
	{
		// 中間素材の頂点をエンジン側の頂点形式へ詰め替える
		std::vector<Engine::Resource::MeshVertexFloat> ConvertVertices(const Parse::RawMesh& a_srcMesh)
		{
			std::vector<Engine::Resource::MeshVertexFloat> _vertices = {};
			_vertices.resize(a_srcMesh.vertices.size());

			for (size_t _j = 0; _j < a_srcMesh.vertices.size(); ++_j)
			{
				const auto& _srcVertex = a_srcMesh.vertices[_j];
				Engine::Resource::MeshVertexFloat _dstVertex = {};

				unsigned int _srcColor = _srcVertex.color;
				float r = static_cast<float>((_srcColor >> 24) & 0xFF) / 255.0f;
				float g = static_cast<float>((_srcColor >> 16) & 0xFF) / 255.0f;
				float b = static_cast<float>((_srcColor >> 8) & 0xFF) / 255.0f;
				float a = static_cast<float>((_srcColor >> 0) & 0xFF) / 255.0f;
				_dstVertex.color = Math::Color(r, g, b, a);

				_dstVertex.normal = _srcVertex.normal;
				_dstVertex.pos = _srcVertex.pos;
				_dstVertex.tangent = _srcVertex.tangent;
				_dstVertex.uv = _srcVertex.uv;

				_dstVertex.skinIndexList = _srcVertex.skinIndexList;
				_dstVertex.skinWeightList = _srcVertex.skinWeightList;

				_vertices[_j] = _dstVertex;
			}

			return _vertices;
		}

		void ConvertNodes(
			const ResourceBuildContext& a_ctx,
			ModelData& a_destModel,
			const Parse::RawModel& a_rawModel
		)
		{
			//=================================================
			// メッシュ作成
			// 中間素材のメッシュ配列とMeshVecは1対1で対応させる
			//=================================================
			a_destModel.MeshVec.resize(a_rawModel.meshes.size());
			for (size_t _meshIdx = 0; _meshIdx < a_rawModel.meshes.size(); ++_meshIdx)
			{
				const auto& _srcMesh = a_rawModel.meshes[_meshIdx];

				// 頂点配列の詰め替え
				auto _vertices = ConvertVertices(_srcMesh);

				// メッシュ作成 : GPU命令はコンテキストのコマンドリストへ積まれるだけ
				a_destModel.MeshVec[_meshIdx].CreateFloat(
					a_ctx,
					_vertices,
					_srcMesh.faces,
					_srcMesh.subsets,
					_srcMesh.isSkinMesh
				);
			}

			//=================================================
			// ノード作成
			//=================================================
			a_destModel.originalNodes.resize(a_rawModel.nodes.size());		// ノード配列確保
			for (UINT _i = 0; _i < static_cast<UINT>(a_rawModel.nodes.size()); ++_i)
			{
				// ノード情報
				const Parse::RawNode& _srcNode = a_rawModel.nodes[_i];			// 入力元
				Engine::Resource::Node& _dstNode = a_destModel.originalNodes[_i];	// 出力先

				// メッシュを持つノードなら参照を張る
				if (_srcNode.HasMesh())
				{
					// メッシュノードリストにインデックス登録
					a_destModel.meshNodeIndices.push_back(_i);
					_dstNode.meshIndices.push_back(_srcNode.meshIndex);
					_dstNode.isSkinMesh = a_rawModel.meshes[_srcNode.meshIndex].isSkinMesh;	// スキンメッシュ持ちかどうか
				}

				// ノード情報セット
				_dstNode.name = _srcNode.name;										// ノード名
				_dstNode.localTransform = _srcNode.localTransform;					// ローカル行列
				_dstNode.worldTransform = _srcNode.worldTransform;					// ワールド行列
				_dstNode.boneInverseWorldMatrix = _srcNode.inverseBindMatrix;		// ボーンのオフセット行列

				_dstNode.parent = _srcNode.parent;									// 親インデックス
				_dstNode.children = _srcNode.children;								// 子供リスト

				_dstNode.boneIndex = _srcNode.boneNodeIndex;						// ボーンインデックス

				// 当たり判定用ノードの検索
				if (_dstNode.name.find("COL") != std::string::npos)
				{
					// 判定用ノードに割り当て
					a_destModel.collisionMeshNodeIndices.push_back(_i);
				}
				else
				{
					// 描画用ノードに割り当て
					a_destModel.drawMeshNodeIndices.push_back(_i);
				}
			}

			//=================================================
			// ルートノード・ボーンノードリスト作成
			//=================================================
			for (UINT _nodeIdx = 0; _nodeIdx < a_rawModel.nodes.size(); ++_nodeIdx)
			{
				// ルートノードの登録
				if (a_rawModel.nodes[_nodeIdx].parent == -1)
				{
					a_destModel.rootNodeIndices.push_back(_nodeIdx);
				}
				// ボーンノードの登録
				int _boneIdx = a_rawModel.nodes[_nodeIdx].boneNodeIndex;		// ボーンインデックス取得
				if (_boneIdx >= 0)
				{
					// ボーンノードリストのサイズ確保
					if (_boneIdx >= static_cast<int>(a_destModel.boneNodeIndices.size()))
					{
						a_destModel.boneNodeIndices.resize(_boneIdx + 1);
					}

					// 登録
					a_destModel.boneNodeIndices[_boneIdx] = _nodeIdx;
				}
			}

			// 当たり判定用ノードがなければ、a_dst.drawMeshNodeと同じ割り当てを行う。
			//
			// 判定の形状は、物理空間(Physics::PhysicsWorld)がここで決めた判定ノードの
			// メッシュの頂点と面から作る。判定専用のデータはメッシュに持たせない
			// (以前は頂点と面を複製した CollisionMesh を作っていた)
			if (a_destModel.collisionMeshNodeIndices.size() == 0)
			{
				a_destModel.collisionMeshNodeIndices = a_destModel.drawMeshNodeIndices;
			}
		}

		/// <summary>
		/// マテリアルのコンバート
		/// </summary>
		/// <param name="a_ctx">ビルドコンテキスト</param>
		/// <param name="a_dstModel">出力モデル</param>
		/// <param name="a_srcModel">入力モデル : 加工済みの中間素材</param>
		/// <param name="a_fileDir">モデルのディレクトリパス</param>
		void ConvertMaterial(
			const ResourceBuildContext& a_ctx,
			ModelData& a_dstModel,
			const Parse::RawModel& a_srcModel,
			const std::string& a_fileDir
		)
		{
			//=================================================
			// マテリアル作成
			//=================================================

			// マテリアル配列を受け取れるサイズのメモリを確保
			a_dstModel.MaterialVec.resize(a_srcModel.materials.size());

			for (UINT _i = 0; _i < a_dstModel.MaterialVec.size(); ++_i)
			{
				// マテリアル情報
				a_dstModel.MaterialVec[_i] = {};
				const Parse::RawMaterial& _srcMaterial = a_srcModel.materials[_i];					// 入力元
				Engine::Resource::Material& _dstMaterial = a_dstModel.MaterialVec[_i];				// 出力先

				// マテリアル情報コピー
				_dstMaterial.name = _srcMaterial.name;									// マテリアル名

				// アルファ
				if (_srcMaterial.alphaMode == "OPAQUE")
				{
					_dstMaterial.alphaMode = Engine::Resource::EAlpha::Opaque;
				}
				else if (_srcMaterial.alphaMode == "MASK")
				{
					_dstMaterial.alphaMode = Engine::Resource::EAlpha::Mask;
				}
				else if (_srcMaterial.alphaMode == "BLEND")
				{
					_dstMaterial.alphaMode = Engine::Resource::EAlpha::Blend;
				}

				// 材質データ
				_dstMaterial.baseColor = _srcMaterial.baseColorFactor;				// 基本色
				_dstMaterial.metallic = _srcMaterial.metallicFactor;				// 金属製
				_dstMaterial.roughness = _srcMaterial.roughnessFactor;				// 粗さ
				_dstMaterial.emissive = _srcMaterial.emissiveFactor;				// エミッシブ

				// テクスチャセット
				_dstMaterial.SetTexture2D(
					a_ctx,
					a_fileDir,
					_srcMaterial.baseColorTexName,
					_srcMaterial.metallicRoughnessTexName,
					_srcMaterial.emissiveTexName,
					_srcMaterial.normalTexName
				);
			}
		}

		void ConvertAnimation(ModelData& a_dstModel,const Parse::RawModel& a_srcModel)
		{
			a_dstModel.AnimationVec.resize(a_srcModel.animations.size());		// アニメーション配列確保

			for (UINT _i = 0; _i < a_dstModel.AnimationVec.size(); ++_i)
			{
				const auto& _srcAnimation = a_srcModel.animations[_i];	// 元データ

				a_dstModel.AnimationVec[_i] = {};
				Engine::Resource::AnimationData& _dstAnimation = a_dstModel.AnimationVec[_i];							// 出力先

				// アニメーション情報コピー
				_dstAnimation.name = _srcAnimation.name;									// 名前
				_dstAnimation.maxLength = _srcAnimation.maxLength;							// アニメーションの長さ

				_dstAnimation.nodes.resize(_srcAnimation.animationNodes.size());				// ノード配列確保
				for (UINT _nIdx = 0; _nIdx < _dstAnimation.nodes.size(); ++_nIdx)
				{
					const auto& _srcNode = _srcAnimation.animationNodes[_nIdx];	// 元データ
					Engine::Resource::AnimationNode& _dstAnimaNode = _dstAnimation.nodes[_nIdx];						// 出力先
					// ノード情報コピー
					_dstAnimaNode.nodeOffset = _srcNode.nodeOffset;					// 対象ノードのオフセット
					_dstAnimaNode.translations = _srcNode.translations;				// 座標キーリスト
					_dstAnimaNode.rotations = _srcNode.rotations;					// 回転キーリスト
					_dstAnimaNode.scales = _srcNode.scales;							// 拡縮キーリスト
				}

			}
		}
	}

	ModelData Engine::Resource::Converter::ModelConverter::ConvertModelData(
		const ResourceBuildContext& a_ctx,
		const std::string& a_filePath,
		const Parse::RawModel& a_rawModel
	)
	{
		// 出力データ
		ModelData _modelData = {};
		auto _fileDir = Core::File::GetDirFromPath(a_filePath);

		ConvertNodes(a_ctx,_modelData,a_rawModel);
		ConvertMaterial(a_ctx,_modelData,a_rawModel,_fileDir);
		ConvertAnimation(_modelData,a_rawModel);

		return _modelData;
	}
	bool ModelConverter::ConvertModelDataToBinary(ResourceManager& a_resourceManager, const std::string& a_filePath)
	{
		ENGINE_LOG("モデルのconvert開始 : %s",a_filePath.c_str());
		auto _guid = a_resourceManager.RefAssetDatabase().GetGUIDFromFilePath(a_filePath);
		return ConvertModelDataToBinary(a_resourceManager, _guid);
	}
	bool ModelConverter::ConvertModelDataToBinary(ResourceManager& a_resourceManager, const Core::GUID& a_guid)
	{
		auto _refHandle = a_resourceManager.LoadImmediate<Model>(a_guid);
		return ConvertModelDataToBinary(a_resourceManager, _refHandle);
	}
	bool ModelConverter::ConvertModelDataToBinary(ResourceManager& a_resourceManager, const ResourceRef<Model>& a_modelHandle)
	{
		// モデル取得
		const auto* _pModel = a_resourceManager.Get(a_modelHandle);
		if (!_pModel)
		{
			ENGINE_LOG("コンバート対象のモデル取得に失敗");
			return false;
		}

		// モデル内の各情報を取得
		const auto& _assetData = _pModel->GetAssestData();
		const auto& _runtimeData = _pModel->GetRuntimeData();

		// 保存用にコピー
		auto _saveAssetData = _assetData;

		// 各参照データのコンバート処理
		std::string _mtrlBasePath		= "Asset/Material/";
		std::string _meshBasePath		= "Asset/Mesh/";
		std::string _animationBasePath	= "Asset/Animation/";
		ConvertMaterialToBinary(a_resourceManager, _mtrlBasePath, _saveAssetData,_runtimeData);
		ConvertMeshToBinary(a_resourceManager, _meshBasePath,_saveAssetData,_runtimeData);
		ConvertAnimationToBinary(a_resourceManager, _animationBasePath,_saveAssetData,_runtimeData);

		// モデルデータのコンバート処理

		// コンバートパスの取得
		auto _guid = a_resourceManager.GetCache(a_modelHandle.GetRaw());
		auto _filePath = a_resourceManager.RefAssetDatabase().GetFilePathFromGUID(_guid);

		WriteModelFile(_filePath, _saveAssetData);

		return true;
	}
	bool ModelConverter::SaveModelAsset(ResourceManager& a_resourceManager, const Core::GUID& a_guid)
	{
		auto _handle = a_resourceManager.GetCache<Model>(a_guid);
		const auto* _pModel = a_resourceManager.Get(_handle);
		if (!_pModel)
		{
			ENGINE_LOG("保存対象のモデル取得に失敗");
			return false;
		}

		// gltf から読んだモデルは、サブアセットのGUIDがまだ無い。
		// .mdl だけを書いても参照先が無いので、丸ごとコンバートする
		const auto& _assetData = _pModel->GetAssestData();
		const auto& _runtimeData = _pModel->GetRuntimeData();
		const bool _isConverted =
			_assetData.materialGUIDs.size() == _runtimeData.materials.size() &&
			_assetData.meshGUIDs.size() == _runtimeData.meshes.size() &&
			_assetData.animationGUIDs.size() == _runtimeData.animations.size();
		if (!_isConverted)
		{
			return ConvertModelDataToBinary(a_resourceManager, a_guid);
		}

		auto _filePath = a_resourceManager.RefAssetDatabase().GetFilePathFromGUID(a_guid);
		auto _saveAssetData = _assetData;
		WriteModelFile(_filePath, _saveAssetData);
		return true;
	}
	void ModelConverter::WriteModelFile(const std::string& a_filePath, ModelAssetData& a_asset)
	{
		auto _dir = Core::File::GetDirFromPath(a_filePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_filePath);
		Persistence::Archive _ar(Persistence::Archive::EMode::Save, _dir, _fileName, "mdl");
		_ar.StringField("ModelName", a_asset.name);

		_ar.GUIDVectorField("MaterialGUID", a_asset.materialGUIDs);
		_ar.GUIDVectorField("MeshGUID", a_asset.meshGUIDs);
		_ar.GUIDVectorField("AnimationGUID", a_asset.animationGUIDs);

		UINT _nodeCount = static_cast<UINT>(a_asset.originalNodes.size());
		_ar.Field("NodeCount", _nodeCount);
		for (UINT _i = 0; _i < _nodeCount; ++_i)
		{
			a_asset.originalNodes[_i].Archive(_ar, _i);
		}

		_ar.VectorField("RootNodeIndices", a_asset.rootNodeIndices);
		_ar.VectorField("BoneNodeIndices", a_asset.boneNodeIndices);
		_ar.VectorField("MeshNodeIndices", a_asset.meshNodeIndices);
		_ar.VectorField("CollisionMeshNodeIndices", a_asset.collisionMeshNodeIndices);
		_ar.VectorField("DrawMeshNodeIndices", a_asset.drawMeshNodeIndices);

		// ModelIO::Load と同じ順で、末尾に書く
		ArchiveBoneMasks(_ar, a_asset.boneMasks);
		ArchiveLodLevels(_ar, a_asset.lodLevels);
	}
	void ModelConverter::ConvertMaterialToBinary(ResourceManager& a_resourceManager, const std::string& a_basePath, ModelAssetData& a_asset,const ModelRuntimeData& a_runtime)
	{

		UINT _mtrlHandleSize = static_cast<UINT>(a_runtime.materials.size());
		a_asset.materialGUIDs.resize(_mtrlHandleSize);
		for (UINT _i = 0; _i < _mtrlHandleSize; ++_i)
		{
			// マテリアル取得
			auto _mtrlHandle = a_runtime.materials[_i];
			auto* _pMaterial = a_resourceManager.Ref(_mtrlHandle);
			if (!_pMaterial) continue;

			// コンバートパスの作成
			auto _dirName = Core::File::GetFileNameWithoutExtension(a_asset.name);
			auto _fileName = _dirName + "_" + std::to_string(_i);
			auto _convertDir = a_basePath + _dirName;
			auto _fullPath = _convertDir + "/" + _fileName;

			// テクスチャのconvert
			ConvertTexture(a_resourceManager, _pMaterial->baseColorTex);
			ConvertTexture(a_resourceManager, _pMaterial->metaRoughTex);
			ConvertTexture(a_resourceManager, _pMaterial->emissiveTex);
			ConvertTexture(a_resourceManager, _pMaterial->normalTex);

			// アセットデータベースに登録
			a_asset.materialGUIDs[_i] = a_resourceManager.RefAssetDatabase().AddMetaData(_fullPath, "Material");

			// マテリアルのセーブ
			Persistence::Archive _ar(Persistence::Archive::EMode::Save, _convertDir, _fileName, "mtrl");
			_pMaterial->Archive(_ar);
		}
	}
	void ModelConverter::ConvertMeshToBinary(ResourceManager& a_resourceManager, const std::string& a_basePath, ModelAssetData& a_asset, const ModelRuntimeData& a_runtime)
	{
		size_t _meshHandleSize = a_runtime.meshes.size();
		a_asset.meshGUIDs.resize(_meshHandleSize);
		for (size_t _i = 0; _i < _meshHandleSize; ++_i)
		{
			// メッシュ取得
			auto _meshHandle = a_runtime.meshes[_i];
			auto* _pMesh = a_resourceManager.Ref(_meshHandle);
			if (!_pMesh) continue;

			// コンバートパスの作成
			auto _dirName = Core::File::GetFileNameWithoutExtension(a_asset.name);
			auto _fileName = _dirName + "_" + std::to_string(_i);
			auto _convertDir = a_basePath + _dirName;
			auto _fullPath = _convertDir + "/" + _fileName;

			// 保存
			a_asset.meshGUIDs[_i] = a_resourceManager.RefAssetDatabase().AddMetaData(_fullPath, "Mesh");
			_pMesh->Save(_convertDir, _fileName);
		}
	}
	void ModelConverter::ConvertAnimationToBinary(ResourceManager& a_resourceManager, const std::string & a_basePath, ModelAssetData & a_asset, const ModelRuntimeData & a_runtime)
	{
		size_t _animHandleSize = a_runtime.animations.size();
		a_asset.animationGUIDs.resize(_animHandleSize);
		for (size_t _i = 0; _i < _animHandleSize; ++_i)
		{
			// アニメーションの取得
			auto _animHandle = a_runtime.animations[_i];
			auto* _pAnim = a_resourceManager.Ref(_animHandle);
			if (!_pAnim) continue;

			// コンバートパスの作成
			auto _dirName = Core::File::GetFileNameWithoutExtension(a_asset.name);
			auto _fileName = _dirName + "_" + std::to_string(_i);
			auto _convertDir = a_basePath + _dirName;
			auto _fullPath = _convertDir + "/" + _fileName;

			// 保存
			a_asset.animationGUIDs[_i] = a_resourceManager.RefAssetDatabase().AddMetaData(_fullPath, "Animation");
			_pAnim->Save(_convertDir, _fileName);
		}
	}
	void ModelConverter::ConvertTexture(ResourceManager& a_resourceManager, const ResourceRef<Texture>& a_ref)
	{
		auto* _pTex = RefTexture(a_resourceManager, a_ref);
		if (!_pTex) return;

		auto _guid = a_resourceManager.GetCache(a_ref.GetRaw());
		if (_guid == Core::DEFAULT_GUID) return;

		auto _path = a_resourceManager.RefAssetDatabase().GetFilePathFromGUID(_guid);

		_pTex->Save(_path);
	}
	Texture* ModelConverter::RefTexture(ResourceManager& a_resourceManager, const ResourceRef<Texture>& a_ref)
	{
		return a_resourceManager.Ref(a_ref);
	}
}