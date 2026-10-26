#include "HierarchyPanel.h"

#include "Engine/ECS/World/World.h"
#include "Application/ECS/PhaseTag/PhaseTag.h"	// ライフサイクルのフェーズタグ

#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/Scene/SceneManager/SceneManager.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"

#include "Application/Components/Core/NameComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Application/Utility/PrefabSpawnHelper.h"

#include "../../EditorCamera/EditorCamera.h"
#include "../../Helper/EditorHelper.h"

namespace Editor
{
	void Editor::HierarchyPanel::OnDrawImGui(EditorContext& a_editContext)
	{
		// ワールド取得
		Engine::ECS::World* _pWorld = a_editContext.pServices->pSceneManager->RefWorld();
		if (!_pWorld || !_pWorld->IsInit()) return;

		// 全エンティティ取得
		UINT _aliveEntityCount = _pWorld->GetAliveEntityCount();
		Engine::EditorField::Value("EntityNum", "%d", _aliveEntityCount);

		Engine::EditorField::Line();

		// エンティティ一覧表示
		ImGui::BeginChild("EntityList");
		{
			// エンティティ追加(空 or プレハブから)
			if (Engine::EditorField::CreateButton("AddEntity"))
			{
				ImGui::OpenPopup("AddEntityPopup");
			}
			if (ImGui::BeginPopup("AddEntityPopup"))
			{
				// 空のエンティティ
				if (ImGui::MenuItem("Empty Entity"))
				{
					AddEntity(a_editContext,_pWorld);
				}

				Engine::EditorField::Line();
				Engine::EditorField::HelpText("From Prefab");

				// アセットデータベースに登録されているプレハブ一覧
				const auto _prefabSpan = a_editContext.pServices->pAssetDatabase->GetTypeMetaVec("Prefab");
				if (const auto* _pProp = EditorHelper::DrawAssetSelectList(_prefabSpan))
				{
					InstantiatePrefab(a_editContext, _pWorld, _pProp->guid);
				}

				ImGui::EndPopup();
			}
			Engine::EditorField::Line();

			// 名前でエンティティを探す。出しっぱなしの欄なので入力は消さない
			const std::string& _search = EditorHelper::DrawSearchBox("##EntitySearch", "Search entity...", false);

			Engine::EditorField::Line();

			// ループ中のコンポーネント追加/削除によるベクター破損を防ぐため、一度EntityIDのリストをコピーする
			const auto& _entityLocationList = _pWorld->GetEntityList();
			std::vector<Engine::ECS::Entity> _drawEntities;
			_drawEntities.reserve(_entityLocationList.size());

			for (auto& _location : _entityLocationList)
			{
				Engine::ECS::Entity _entity = _pWorld->GetEntity(_location);
				if (_entity == ECS::Limits::INVALID_ENTITY) continue;

				// 検索中は親子をたどらず、名前が一致したものだけを平らに並べる。
				// 一致した子だけを出したいのに、親をたどると出てこない/親ごと出てしまうため
				if (!_search.empty())
				{
					if (EditorHelper::IsMatchSearch(_search, GetEntityLabel(_pWorld, _entity)))
					{
						_drawEntities.push_back(_entity);
					}
					continue;
				}

				// 親を持っていないもの・親が見つからないものをルートとして抽出
				if (GetParentState(_pWorld, _entity) == EParentState::HasParent) continue;
				_drawEntities.push_back(_entity);
			}

			// コピーした安全なリストで描画を回す
			for (const auto& _entity : _drawEntities)
			{
				DrawEntityNode(a_editContext, _pWorld, _entity, _search.empty());
			}
		}
		ImGui::EndChild();
	}

	void Editor::HierarchyPanel::AddEntity(EditorContext& a_editContext,ECS::World* a_pWorld)
	{
		auto _trsCompTypeID = a_pWorld->GetCompTypeID<App::Component::LocalTransformComponent>();

		Engine::ECS::Signature _sig = {};
		_sig.set(_trsCompTypeID);
		_sig.set(a_pWorld->GetCompTypeID<App::Component::WorldMatrixComponent>());
		_sig.set(a_pWorld->GetCompTypeID<App::Component::NameComponent>());
		_sig.set(a_pWorld->GetCompTypeID<Engine::ECS::GUIDComponent>());
		_sig.set(a_pWorld->GetCompTypeID<App::Component::PostDeserializeTag>());
		
		// ---- カメラの正面にエンティティを出現させる ----
		// データ配列用意
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>> _dataMap = {};

		if(a_editContext.pEditorCamera)
		{
			auto& _buf = _dataMap[_trsCompTypeID];
			_buf.assign(a_pWorld->GetComponentMetaData(_trsCompTypeID).compAlignSize, 0);		// 空でメモリ確保
			auto _ctor = a_pWorld->GetCompFunc(_trsCompTypeID).construct;
			if (_ctor) _ctor(_buf.data());											// コンストラクタで初期化

			// カメラ情報取得
			auto _trs = Math::Decompose(a_editContext.pEditorCamera->GetWorldMatrix());
			auto _forward = Math::Vector3::Transform(Math::Vector3::Forward(), _trs.rotation);

			// トランスフォームにカメラの正面 + オフセットを座標として初期化
			App::Component::LocalTransformComponent _ltComp = {};
			_ltComp.pos = _trs.pos + _forward * m_distance;
			std::memcpy(_buf.data(), &_ltComp, sizeof(_ltComp));
		}

		// エンティティの追加
		a_pWorld->ReserveCreateEntityWithData(_sig,_dataMap);
	}

	void Editor::HierarchyPanel::InstantiatePrefab(EditorContext& a_editContext,ECS::World* a_pWorld, const Core::GUID& a_guid)
	{
		// プレハブをロード(未ロードならここで読み込まれる)
		if (!a_editContext.pServices->pResourceManager->Has<Resource::Prefab>(a_guid))
		{
			a_editContext.pServices->pResourceManager->LoadImmediate<Resource::Prefab>(a_guid);
		}

		// プレハブ取得
		auto _handle = a_editContext.pServices->pResourceManager->GetCache<Resource::Prefab>(a_guid);
		auto* _pPrefab = a_editContext.pServices->pResourceManager->Ref(_handle);
		if (!_pPrefab) return;

		//------------------------------------------------------------------
		// プレハブ情報取得
		//------------------------------------------------------------------
		// シグネチャと初期データを直に取ると、そのプレハブが覚えている
		// 子エンティティが落ちる(ルート1体しか生えない)。
		// 材料の組み立てはプレハブ側に任せること。GUIDの振り直しと
		// 親子リンクの張り替えもそちらで済ませてくれる。
		//------------------------------------------------------------------
		std::vector<Resource::PrefabInstanceData> _instanceVec = _pPrefab->BuildInstanceData(a_pWorld);
		if (_instanceVec.empty()) return;

		// 初期値スポーン位置の編集
		auto _ltID = a_pWorld->GetCompTypeID<App::Component::LocalTransformComponent>();

		// 位置を動かすのはルートだけ。子はルートからの相対で付いてくる
		Resource::PrefabInstanceData& _root = _instanceVec[0];

		// トランスフォームは持っている前提だが、念のためテスト
		if (_root.sig.test(_ltID))
		{
			// 位置情報の上書き
			auto& _buf = _root.dataMap[_ltID];
			App::Component::LocalTransformComponent _ltComp = {};
			auto _trs = Math::Decompose(a_editContext.pEditorCamera->GetWorldMatrix());
			auto _forward = Math::Vector3::Transform(Math::Vector3::Forward(), _trs.rotation);

			std::memcpy(&_ltComp, _buf.data(), sizeof(_ltComp));
			_ltComp.pos = _trs.pos + _forward * m_distance;
			_ltComp.isDirty = true;
			std::memcpy(_buf.data(), &_ltComp, sizeof(_ltComp));
		}

		// シーンに追加(その場で作る)。
		// GUID を持たない子のあるプレハブ(プレイヤーなど)は、親子とアタッチメントを
		// エンティティIDで付けるしかないので、IDが返らない遅延生成は使わない。
		// ここはシステムの反復の外(エディターの更新)なので即時生成してよい
		App::Utility::CreateInstanceNow(*a_pWorld, _instanceVec);
	}

	void Editor::HierarchyPanel::DrawEntityNode(EditorContext& a_editContext, Engine::ECS::World* a_pWorld, const Engine::ECS::Entity& a_entity, bool a_isDrawChildren)
	{
		// エンティティチェック
		if (a_entity == Engine::ECS::Limits::INVALID_ENTITY) return;

		// ツリーノードして表示 : ドラッグアンドドロップ受け入れ
		ImGuiTreeNodeFlags _flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
		// 複数選択中はすべての選択エンティティをハイライトする
		if (a_editContext.IsSelectedEntity(a_entity))
		{
			_flags |= ImGuiTreeNodeFlags_Selected;
		}

		// 検索中は一致したものを平らに並べるので、子はたどらない
		std::vector<ECS::Entity> _children = {};
		if (a_isDrawChildren)
		{
			_children = GetChildEntities(a_pWorld, a_entity);
		}
		if (_children.empty())
		{
			_flags |= ImGuiTreeNodeFlags_Leaf;
		}

		// ラベル作成。親を指しているのに見つからないものは、それと分かるようにしておく
		std::string _label = GetEntityLabel(a_pWorld, a_entity);
		if (GetParentState(a_pWorld, a_entity) == EParentState::Missing)
		{
			_label += "  (parent missing)";
		}

		// ツリーノード表示
		bool _isNodeOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(a_entity)), _flags, _label.c_str());

		// clickで選択
		// LCtrl押下中は追加選択(再クリックで解除)、押していなければ単体選択に切り替わる
		if (ImGui::IsItemClicked())
		{
			a_editContext.SelectEntity(a_entity, a_editContext.m_isSelecting);
		}

		// ドラッグアンドドロップの制御
		HandleDragAndDrop(a_pWorld, a_entity, _label);

		// 子エンティティがあれば再起表示
		if (_isNodeOpen)
		{
			for (ECS::Entity& _child : _children)
			{
				DrawEntityNode(a_editContext, a_pWorld, _child);
			}

			// ツリーノード終了
			ImGui::TreePop();
		}
	}

	std::string Editor::HierarchyPanel::GetEntityLabel(Engine::ECS::World* a_pWorld, const Engine::ECS::Entity& a_entity) const
	{
		if (a_pWorld->HasComponent<App::Component::NameComponent>(a_entity))
		{
			auto* _pNameComp = a_pWorld->RefData<App::Component::NameComponent>(a_entity);
			if (_pNameComp && _pNameComp->name[0] != '\0')
			{
				return _pNameComp->name;
			}
		}

		// 名前を持っていないものはIDで見分ける
		return std::to_string(a_entity);
	}

	void Editor::HierarchyPanel::HandleDragAndDrop(ECS::World* a_pWorld, const ECS::Entity& a_entity, const std::string& a_label)
	{
		// ドラッグリソースとして設定 : 自分をドラッグする
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("Entity", &a_entity, sizeof(ECS::Entity));
			Engine::EditorField::Text("%s", a_label.c_str());
			ImGui::EndDragDropSource();
		}

		// ドロップターゲットの設定 : 自分にドロップされる
		if (ImGui::BeginDragDropTarget())
		{
			// ペイロードが存在するのなら
			if (const ImGuiPayload* _payload = ImGui::AcceptDragDropPayload("Entity"))
			{
				// 子供のエンティティを取得
				ECS::Entity _child = *static_cast<const ECS::Entity*>(_payload->Data);
				// 自分自身や不正なエンティティは親にできない
				if (_child != a_entity && _child != ECS::Limits::INVALID_ENTITY)
				{
					// 親子関係の再構築処理を実行
					AttachChild(a_pWorld, a_entity, _child);
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	void Editor::HierarchyPanel::AttachChild(ECS::World* a_pWorld, const ECS::Entity& a_parent, const ECS::Entity& a_child)
	{
		// 安全チェック: 必要なGUIDコンポーネントがあるか
		if (!a_pWorld->HasComponent<Engine::ECS::GUIDComponent>(a_parent) || !a_pWorld->HasComponent<Engine::ECS::GUIDComponent>(a_child)) return;

		auto* _pParentGUID = a_pWorld->RefData<Engine::ECS::GUIDComponent>(a_parent);

		// 新たな親子関係の構築
		Core::GUID _parentGUID = _pParentGUID->guid;

		UINT _depth = 1;
		// 親がヒエラルキーを持っていたら情報を引き継ぐ
		if (a_pWorld->HasComponent<App::Component::HierarchyComponent>(a_parent))
		{
			auto* _pHieComp = a_pWorld->RefData<App::Component::HierarchyComponent>(a_parent);
			// 深度の更新
			_depth = _pHieComp->depth + 1;
		}
		// 持っていなければ親に付与して先に引っ越しさせる
		else
		{
			// コンポーネントの追加
			auto _compTypeID = a_pWorld->GetCompTypeID<App::Component::HierarchyComponent>();
			App::Component::HierarchyComponent _initData = {};
			_initData.parentGUID = Core::DEFAULT_GUID;
			_initData.parentID = ECS::Limits::INVALID_ENTITY;
			_initData.depth = 0;
			// 内部でディープコピーしてるのでローカルでいい
			a_pWorld->ReserveAddComponent(_compTypeID, a_parent, reinterpret_cast<uint8_t*>(&_initData));
		}
		// ヒエラルキーコンポーネントを持っていなかった場合
		// 付与してデータを入れる
		App::Component::HierarchyComponent _newComp = {};
		_newComp.parentGUID = _parentGUID;
		_newComp.parentID = a_parent;
		_newComp.depth = _depth;

		if (a_pWorld->HasComponent<App::Component::HierarchyComponent>(a_child))
		{
			*a_pWorld->RefData<App::Component::HierarchyComponent>(a_child) = _newComp;
		}
		else
		{
			auto _compTypeID = a_pWorld->GetCompTypeID<App::Component::HierarchyComponent>();
			a_pWorld->ReserveAddComponent(_compTypeID, a_child, reinterpret_cast<uint8_t*>(&_newComp));
		}
	}

	//======================================================================================
	// 親の状態
	//--------------------------------------------------------------------------------------
	// 親IDが付いていればそれで見る。付く前(読み込み直後の Awake 前)は GUID で見る
	// (GetChildEntities と同じ見方。GUID を持たない親(スポナーが出したプレイヤー)の子は ID でしか引けない)。
	//
	// 指している親が見つからないものは「親が居ない」として扱い、ルートに出す。
	// 以前はルートにも誰かの子にも出ず、一覧から消えていた(居るのに選べない)
	//======================================================================================
	Editor::HierarchyPanel::EParentState Editor::HierarchyPanel::GetParentState(Engine::ECS::World* a_pWorld, const ECS::Entity& a_entity) const
	{
		if (!a_pWorld->HasComponent<App::Component::HierarchyComponent>(a_entity)) return EParentState::None;

		const auto* _pHieComp = a_pWorld->RefData<App::Component::HierarchyComponent>(a_entity);
		if (!_pHieComp) return EParentState::None;

		if (_pHieComp->parentID != ECS::Limits::INVALID_ENTITY)
		{
			return a_pWorld->IsAliveEntity(_pHieComp->parentID) ? EParentState::HasParent : EParentState::Missing;
		}

		if (_pHieComp->parentGUID.IsValid())
		{
			return (a_pWorld->GetEntity(_pHieComp->parentGUID) != ECS::Limits::INVALID_ENTITY)
				? EParentState::HasParent
				: EParentState::Missing;
		}

		return EParentState::None;
	}

	std::vector<Engine::ECS::Entity> Editor::HierarchyPanel::GetChildEntities(Engine::ECS::World* a_pWorld, ECS::Entity a_parent) const
	{
		std::vector<ECS::Entity> _children;

		// 親のGUID(持っていなければ無効)
		Core::GUID _parentGUID = Core::DEFAULT_GUID;
		if (a_pWorld->HasComponent<Engine::ECS::GUIDComponent>(a_parent))
		{
			_parentGUID = a_pWorld->RefData<Engine::ECS::GUIDComponent>(a_parent)->guid;
		}

		// 親IDが付いていればそれで見る。付く前(読み込み直後の Awake 前)は GUID で見る。
		// GUID を持たない親(スポナーが出したプレイヤー)の子は ID でしか引けない
		a_pWorld->ForEach<App::Component::HierarchyComponent>(
			[&](ECS::Chunk* a_pChunk, UINT a_count, App::Component::HierarchyComponent* a_hieArray)
			{
				for (UINT _i = 0; _i < a_count; ++_i)
				{
					const App::Component::HierarchyComponent& _hierarchy = a_hieArray[_i];

					const bool _isChild = (_hierarchy.parentID != ECS::Limits::INVALID_ENTITY)
						? (_hierarchy.parentID == a_parent)
						: (_parentGUID.IsValid() && _hierarchy.parentGUID == _parentGUID);

					if (_isChild)
					{
						_children.push_back(a_pChunk->entityData[_i]);
					}
				}
			}
		);

		return _children;
	}
}