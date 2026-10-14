#include "ResourceDraw.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "ModelEdit/ModelEdit.h"
#include "MeshEdit/MeshEdit.h"
#include "MaterialEdit/MaterialEdit.h"
#include "AnimationEdit/AnimationEdit.h"
#include "TextureEdit/TextureEdit.h"
#include "ShaderEdit/ShaderEdit.h"
#include "ParticleEdit/ParticleEdit.h"
#include "AnimatorEdit/AnimatorEdit.h"
#include "AudioBehaviorEdit/AudioBehaviorEdit.h"
#include "EffectAssetEdit/EffectAssetEdit.h"
#include "UIMaterialEdit/UIMaterialEdit.h"

#include "Engine/Resource/Data/EffectPrefab/EffectPrefab.h"
#include "Editor/EffectEditor/EffectEditor.h"
#include "Editor/PrefabEditor/PrefabEditor.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"

#include "Engine/Resource/Data/Model/IO/ModelConverter/ModelConverter.h"

// プレハブ編集用(ECSのエンティティインスペクタと同じ構成で描く)
#include "Engine/ECS/World/World.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Editor/Helper/EditorHelper.h"

namespace Editor::Inspector
{
	namespace
	{
		//-----------------------------------------------------------------------------------------
		// アセットの解決
		// ロード済みなら実体を返し、未ロードならロードボタンを出してnullptrを返す
		//-----------------------------------------------------------------------------------------
		template<typename TResource>
		TResource* ResolveAsset(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid)
		{
			if (!a_resourceManager.Has<TResource>(a_guid))
			{
				Engine::EditorField::Header("No loaded file");
				if (ImGui::Button("Load"))
				{
					a_resourceManager.LoadImmediate<TResource>(a_guid);
				}
				return nullptr;
			}

			auto _handle = a_resourceManager.GetCache<TResource>(a_guid);
			auto* _pResource = a_resourceManager.Ref(_handle);
			if (!_pResource)
			{
				Engine::EditorField::WarningText("Not found asset");
				return nullptr;
			}

			return _pResource;
		}
	}

	//-----------------------------------------------------------------------------------------
	// モデル
	//-----------------------------------------------------------------------------------------
	void ModelDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pModel = ResolveAsset<Resource::Model>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pModel) { return; }

		// バイナリへの変換
		if (ImGui::Button("Convert"))
		{
			auto _filePath = a_editContext.pServices->pAssetDatabase->GetFilePathFromGUID(_guid);
			Resource::Converter::ModelConverter::ConvertModelDataToBinary(*a_editContext.pServices->pResourceManager, _filePath);
			ENGINE_LOG("モデルのconvert処理が完了 : %s", _filePath.c_str());
		}

		Engine::EditorField::Line();

		ModelEdit(a_editContext, _pModel);
	}

	//-----------------------------------------------------------------------------------------
	// テクスチャ
	//-----------------------------------------------------------------------------------------
	void TextureDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		// 名前と、GUID表示
		auto _fileName = a_editContext.pServices->pAssetDatabase->GetFileNameFromGUID(_guid);
		Engine::EditorField::Text("%s", _fileName.c_str());
		Engine::EditorField::Text("%s", _guid.String().c_str());

		Engine::EditorField::Line();

		auto* _pTexture = ResolveAsset<Resource::Texture>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pTexture) { return; }

		TextureEdit(a_editContext, _pTexture);
	}

	//-----------------------------------------------------------------------------------------
	// アニメーター(アニメ用ステートマシン)
	//-----------------------------------------------------------------------------------------
	void AnimatorDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pAnimator = ResolveAsset<Resource::AnimatorAsset>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pAnimator) { return; }

		// ノードエディタ側がハンドルを必要とするため取得しておく
		auto _handle = a_editContext.pServices->pResourceManager->GetCache<Resource::AnimatorAsset>(_guid);

		AnimatorEdit(a_editContext, _pAnimator, _handle);
	}

	//-----------------------------------------------------------------------------------------
	// パーティクル
	//-----------------------------------------------------------------------------------------
	void ParticleDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pParticles = ResolveAsset<Resource::ParticlesAsset>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pParticles) { return; }

		ParticleEdit(*a_editContext.pServices, _guid, _pParticles, &a_editContext);
	}

	//-----------------------------------------------------------------------------------------
	// マテリアル
	//-----------------------------------------------------------------------------------------
	void MaterialDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pMaterial = ResolveAsset<Resource::Material>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pMaterial) { return; }

		MaterialEdit(a_editContext, _pMaterial);
	}

	//-----------------------------------------------------------------------------------------
	// メッシュ
	//-----------------------------------------------------------------------------------------
	void MeshDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pMesh = ResolveAsset<Resource::Mesh>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pMesh) { return; }

		MeshEdit(a_editContext, _pMesh);
	}

	//-----------------------------------------------------------------------------------------
	// アニメーション
	//-----------------------------------------------------------------------------------------
	void AnimationDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pAnimation = ResolveAsset<Resource::AnimationData>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pAnimation) { return; }

		AnimationEdit(a_editContext, _pAnimation);
	}

	//-----------------------------------------------------------------------------------------
	// シェーダー
	//-----------------------------------------------------------------------------------------
	void ShaderDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pShader = ResolveAsset<Resource::Shader>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pShader) { return; }

		ShaderEdit(a_editContext, _pShader);
	}

	//-----------------------------------------------------------------------------------------
	// UIマテリアル
	//-----------------------------------------------------------------------------------------
	void UIMaterialDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pMaterial = ResolveAsset<Resource::UIMaterial>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pMaterial) { return; }

		UIMaterialEdit(a_editContext, _pMaterial);
	}

	//-----------------------------------------------------------------------------------------
	// オーディオビヘイビア
	//-----------------------------------------------------------------------------------------
	void AudioBehaviorDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pBehavior = ResolveAsset<Resource::AudioBehavior>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pBehavior) { return; }

		AudioBehaviorEdit(a_editContext, _pBehavior);
	}

	//-----------------------------------------------------------------------------------------
	// エフェクト
	//-----------------------------------------------------------------------------------------
	void EffectAssetDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pEffect = ResolveAsset<Resource::EffectAsset>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pEffect) { return; }

		EffectAssetEdit(*a_editContext.pServices, _guid, _pEffect, true, &a_editContext);
	}

	//-----------------------------------------------------------------------------------------
	// エフェクトプレハブ
	// 中身はプレハブと同じ編集UI。違うのは演出全体の寿命を必ず持つことだけ
	//-----------------------------------------------------------------------------------------
	void EffectPrefabDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pEffectPrefab = ResolveAsset<Resource::EffectPrefab>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pEffectPrefab) { return; }

		// コンポーネントのメタ情報・編集関数を引くために World が必要
		ECS::World* _pWorld = a_editContext.pServices->pSceneManager->RefWorld();
		if (!_pWorld || !_pWorld->IsInit())
		{
			Engine::EditorField::HelpText("No active World.");
			Engine::EditorField::HelpText("Open a scene to edit effect prefab components.");
			return;
		}

		if (ImGui::Button("Save"))
		{
			auto _path = a_editContext.pServices->pAssetDatabase->GetFilePathFromGUID(_guid);
			_pEffectPrefab->Save(_pWorld, _path);
			ENGINE_LOG("Save EffectPrefab : %s", _path.c_str());
		}

		// ゲームと同じ描画で、炊いたところを繰り返し確認する
		Engine::EditorField::SameLine();
		if (ImGui::Button("Open Effect Editor"))
		{
			if (auto* _pEffectEditor = a_editContext.pEffectEditor)
			{
				_pEffectEditor->OpenEffectPrefab(_guid);
			}
		}

		float _lifeTime = _pEffectPrefab->GetLifeTime();
		if (Engine::EditorField::Field("Life Time", _lifeTime, 0.05f, Resource::EffectPrefab::MIN_LIFE_TIME, 60.0f))
		{
			_pEffectPrefab->SetLifeTime(_lifeTime);
		}
		Engine::EditorField::Tooltip("炊いたらこの秒数で全部消える(各ノードの寿命はこれで頭打ち)");

		Engine::EditorField::Line();

		PrefabComponentsEdit(_pWorld, &_pEffectPrefab->RefPrefab());
	}

	//-----------------------------------------------------------------------------------------
	// レンダリングパイプライン
	// ノードエディタはアセット自身が持っているので、ここは呼び出しとセーブだけ
	//-----------------------------------------------------------------------------------------
	//-----------------------------------------------------------------------------------------
	// プレハブ
	// ECS のエンティティインスペクタと同じ構成で、コンポーネントを追加・削除・編集する
	//-----------------------------------------------------------------------------------------
	void PrefabDraw(EditorContext& a_editContext)
	{
		auto _guid = a_editContext.pAssetProp->guid;

		auto* _pPrefab = ResolveAsset<Resource::Prefab>(*a_editContext.pServices->pResourceManager, _guid);
		if (!_pPrefab) { return; }

		// コンポーネントのメタ情報・編集関数を引くために World が必要
		ECS::World* _pWorld = a_editContext.pServices->pSceneManager->RefWorld();
		if (!_pWorld || !_pWorld->IsInit())
		{
			Engine::EditorField::HelpText("No active World.");
			Engine::EditorField::HelpText("Open a scene to edit prefab components.");
			return;
		}

		// ---- 保存 ----
		if (ImGui::Button("Save"))
		{
			auto _path = a_editContext.pServices->pAssetDatabase->GetFilePathFromGUID(_guid);
			_pPrefab->Save(_pWorld, _path);
			ENGINE_LOG("Save Prefab : %s", _path.c_str());
		}

		// 出したところを見ながら、子も含めて組む
		Engine::EditorField::SameLine();
		if (ImGui::Button("Open Prefab Editor"))
		{
			if (auto* _pPrefabEditor = a_editContext.pPrefabEditor)
			{
				_pPrefabEditor->Open(_guid);
			}
		}

		//------------------------------------------------------------------
		// シグネチャのコピー / ペースト
		//
		// 別のプレハブへコンポーネント構成をそのまま持っていくためのもの。
		// 初期値のバイト列ごと運ぶので、貼り付け先は編集済みの値まで引き継ぐ。
		// 貼り付けはメモリ上のプレハブを書き換えるだけなので、
		// 確定させるには Save を押すこと。
		//------------------------------------------------------------------
		auto& _clipboard = a_editContext.prefabClipboard;

		Engine::EditorField::SameLine();
		if (ImGui::Button("Copy Signature"))
		{
			_clipboard.signature = _pPrefab->GetSignature();
			_clipboard.dataMap = _pPrefab->GetDataMap();
			_clipboard.isValid = true;

			ENGINE_LOG("Copy Prefab Signature : %d components", static_cast<int>(_clipboard.GetCount()));
		}

		// コピー前は貼り付けられない
		Engine::EditorField::SameLine();
		ImGui::BeginDisabled(!_clipboard.isValid);
		if (ImGui::Button("Paste Signature"))
		{
			_pPrefab->PasteSignatureAndData(_clipboard.signature, _clipboard.dataMap);
			ENGINE_LOG("Paste Prefab Signature : %d components", static_cast<int>(_clipboard.GetCount()));
		}
		ImGui::EndDisabled();

		// クリップボードの中身を出しておく : 何を貼るのか押す前に分かるようにする
		if (_clipboard.isValid)
		{
			Engine::EditorField::HelpText("Clipboard : %d components", static_cast<int>(_clipboard.GetCount()));
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				for (size_t _typeID = 0; _typeID < _clipboard.signature.size(); ++_typeID)
				{
					if (!_clipboard.signature.test(_typeID)) continue;

					const auto& _meta = _pWorld->GetComponentMetaData(static_cast<ECS::ComponentTypeID>(_typeID));
					ImGui::Text("%s", _meta.name.c_str());
				}
				ImGui::EndTooltip();
			}
		}
		else
		{
			Engine::EditorField::HelpText("Clipboard : empty");
		}

		Engine::EditorField::Line();

		//------------------------------------------------------------------
		// 一緒に覚えている子エンティティ
		//------------------------------------------------------------------
		// ここで編集はできない(このインスペクタはルートの構成を触る場所)。
		// 子の追加・削除・編集はプレハブエディター(Open Prefab Editor)で行う。
		// 実体化すると、この一覧ぶんが親子リンク付きで一緒に生成される。
		//------------------------------------------------------------------
		const auto& _childVec = _pPrefab->GetChildren();
		if (_childVec.empty())
		{
			Engine::EditorField::HelpText("Children : none");
		}
		else
		{
			Engine::EditorField::Value("Children", "%d", static_cast<int>(_childVec.size()));

			if (ImGui::TreeNode("Children List"))
			{
				for (size_t _i = 0; _i < _childVec.size(); ++_i)
				{
					const auto& _child = _childVec[_i];

					// 親の位置(-1 はルート直下)
					const std::string _parentLabel = (_child.parentIndex < 0)
						? std::string("Root")
						: ("Child " + std::to_string(_child.parentIndex));

					// コンポーネント数だけ出しておけば、空でないことは分かる
					ImGui::BulletText("Child %d : parent = %s (%d components)",
						static_cast<int>(_i),
						_parentLabel.c_str(),
						static_cast<int>(_child.sig.count()));
				}
				ImGui::TreePop();
			}
		}

		Engine::EditorField::Line();

		PrefabComponentsEdit(_pWorld, _pPrefab);
	}

	//-----------------------------------------------------------------------------------------
	// プレハブのコンポーネントの羅列・編集・追加
	// エンティティインスペクタと同じ edit 関数を使う
	//-----------------------------------------------------------------------------------------
	void PrefabComponentsEdit(ECS::World* a_pWorld, Resource::Prefab* a_pPrefab, int a_nodeIndex)
	{
		if (!a_pWorld || !a_pPrefab) return;
		if (!a_pPrefab->IsValidNode(a_nodeIndex)) return;

		ECS::World* _pWorld = a_pWorld;
		Resource::Prefab* _pPrefab = a_pPrefab;

		// ノードを切り替えたとき、開いていたツリーの状態を別のノードへ持ち越さない
		ImGui::PushID(a_nodeIndex);

		// ---- 所持コンポーネントの羅列・編集 ----
		// 追加・削除はこの関数の終わりまで遅らせるので、参照のままで崩れない
		const ECS::Signature& _sig = _pPrefab->GetSignature(a_nodeIndex);

		ECS::CompEditContext _compEditContext = {};
		_compEditContext.pWorld = _pWorld;
		_compEditContext.entity = ECS::Limits::INVALID_ENTITY;	// プレハブは実体を持たない

		// 反復中に消すと崩れるので削除は予約する
		ECS::ComponentTypeID _removeTypeID = ECS::Limits::INVALID_COMPONENTTYPEID;

		for (size_t _typeID = 0; _typeID < _sig.size(); ++_typeID)
		{
			if (!_sig.test(_typeID)) continue;

			auto _compTypeID = static_cast<ECS::ComponentTypeID>(_typeID);
			const auto& _metaData = _pWorld->GetComponentMetaData(_compTypeID);

			if (ImGui::TreeNodeEx(_metaData.name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Framed))
			{
				// コンポーネントごとの編集UI(エンティティインスペクタと同じ edit 関数を使う)
				_compEditContext.pData = _pPrefab->RefData(_compTypeID, a_nodeIndex);
				auto _func = _pWorld->GetCompFunc(_compTypeID).edit;
				if (_func && _compEditContext.pData)
				{
					_func(_compEditContext);
				}

				if (Engine::EditorField::DeleteButton("RemoveComponent"))
				{
					_removeTypeID = _compTypeID;
				}

				ImGui::TreePop();
			}
		}

		if (_removeTypeID != ECS::Limits::INVALID_COMPONENTTYPEID)
		{
			_pPrefab->RemoveComponent(_removeTypeID, a_nodeIndex);
		}

		// ---- コンポーネントの追加 ----
		ECS::ComponentTypeID _addTypeID = ECS::Limits::INVALID_COMPONENTTYPEID;
		if (Engine::EditorField::ComboScope _combo{ "Add Component", "Select..." })
		{
			// 数が増えると探せなくなるので名前で絞り込めるようにする
			const std::string& _search = EditorHelper::DrawSearchBox();

			const auto& _metaVec = _pWorld->GetAllComponentMetaData();
			for (ECS::ComponentTypeID _compTypeID = 0; _compTypeID < _metaVec.size(); ++_compTypeID)
			{
				const ECS::ComponentMeta& _meta = _metaVec[_compTypeID];

				// すでに持っていたら出さない
				if (_sig.test(_compTypeID)) continue;

				if (!EditorHelper::IsMatchSearch(_search, _meta.name)) continue;

				// 登録名が同じコンポーネントがあってもImGuiのIDがぶつからないようにする
				// (Selectable のIDはラベル文字列から作られるため)
				ImGui::PushID(static_cast<int>(_compTypeID));

				if (ImGui::Selectable(_meta.name.c_str()))
				{
					_addTypeID = _compTypeID;
				}

				ImGui::PopID();
			}
		}

		if (_addTypeID != ECS::Limits::INVALID_COMPONENTTYPEID)
		{
			_pPrefab->AddComponentDefault(_pWorld, _addTypeID, a_nodeIndex);
		}

		ImGui::PopID();
	}
}
