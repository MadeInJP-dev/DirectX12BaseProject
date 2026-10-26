#include "EntityInspector.h"
#include "Engine/ECS/World/World.h"
#include "Application/ECS/PhaseTag/PhaseTag.h"	// ライフサイクルのフェーズタグ

#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/Scene/SceneManager/SceneManager.h"

#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "../../../Helper/EditorHelper.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Core/NameComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Components/Attachment/AttachmentSlotsComponent.h"
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Engine/ECS/Component/TransientTag.h"

#include "Application/Components/Animation/AnimatorComponent.h"
#include "Application/Components/Animation/NodePoseComponent.h"
#include "Application/Components/Animation/SkeletonPoseComponent.h"

namespace Editor::Inspector
{
	// コンポーネントの追加
	void AddComponent(EditorContext& a_editContext, Engine::ECS::World* a_pWorld)
	{
		// 指定して追加
		// 編集対象はプライマリ選択(選択リストの先頭)
		const ECS::Entity _entity = a_editContext.GetPrimaryEntity();

		if (Engine::EditorField::ComboScope _combo{ "Add Component", "Select..." })
		{
			// 数が増えると探せなくなるので名前で絞り込めるようにする
			const std::string& _search = EditorHelper::DrawSearchBox();

			const ECS::Signature& _sig = a_pWorld->GetSignature(_entity);
			const auto& _metaVec = a_pWorld->GetAllComponentMetaData();
			for (ECS::ComponentTypeID _typeID = 0; _typeID < _metaVec.size(); ++_typeID)
			{
				const ECS::ComponentMeta& _meta = _metaVec[_typeID];

				// 所持していたら表示しない
				if (_sig.test(_typeID)) continue;

				if (!EditorHelper::IsMatchSearch(_search, _meta.name)) continue;

				// 登録名が同じコンポーネントがあってもImGuiのIDがぶつからないようにする
				// (Selectable のIDはラベル文字列から作られるため)
				ImGui::PushID(static_cast<int>(_typeID));

				// メタ情報から名前表示
				if (ImGui::Selectable(_meta.name.c_str()))
				{
					// コンポーネントの追加
					a_pWorld->ReserveAddComponent(_typeID, _entity);
				}

				ImGui::PopID();
			}
		}

		// 動きごとに追加
		// アニメーションするエンティティの場合
		if (ImGui::Button("AnimationEntity"))
		{
			ECS::ChangeEntityCmd _cmd = {};
			_cmd.entity = _entity;
			_cmd.toSig = a_pWorld->GetSignature(_entity);
			_cmd.toSig.set(a_pWorld->GetCompTypeID<App::Component::AnimatorComponent>());
			_cmd.toSig.set(a_pWorld->GetCompTypeID<App::Component::NodePoseComponent>());
			_cmd.toSig.set(a_pWorld->GetCompTypeID<App::Component::SkeletonPoseComponent>());
			_cmd.toSig.set(a_pWorld->GetCompTypeID<App::Component::DynamicRaytracingComponent>());
			_cmd.toSig.set(a_pWorld->GetCompTypeID<App::Component::PostDeserializeTag>());
			if (_cmd.toSig.test(a_pWorld->GetCompTypeID<App::Component::ActiveTag>()))
			{
				_cmd.toSig.reset(a_pWorld->GetCompTypeID<App::Component::ActiveTag>());
			}
			a_pWorld->ReserveChangeSignature(_cmd);
		}
	}

	// コンポーネントの削除
	void SubmitCommponent(EditorContext& a_editContext, Engine::ECS::World* a_pWorld, ECS::ComponentTypeID a_typeID)
	{
		Engine::EditorField::Line();

		// 削除系の色分けは EditorHelper に寄せてある(色をここで持たない)
		if (Engine::EditorField::DeleteButton("RemoveComponnet"))
		{
			a_pWorld->ReserveRemoveComponent(a_typeID, a_editContext.GetPrimaryEntity());
		}
	}

	//======================================================================================
	// エンティティのプレハブ化
	//======================================================================================

	// プレハブアセットを置くルートフォルダ
	static const std::string PREFAB_ROOT_DIR = "Asset/Prefab/";

	// プレハブ名からアセットのベースパス(拡張子なし)を作る
	std::string MakePrefabBasePath(const std::string& a_name)
	{
		return PREFAB_ROOT_DIR + a_name + "/" + a_name;
	}

	// その名前がすでに使われているか
	bool IsUsedPrefabName(const Resource::AssetDatabase& a_assetDB, const std::string& a_name)
	{
		const std::string _basePath = MakePrefabBasePath(a_name);

		// アセットデータベースに登録済み
		if (a_assetDB.GetGUIDFromFilePath(_basePath) != Core::DEFAULT_GUID)
		{
			return true;
		}

		// 未登録でもファイルが残っているケースを拾っておく
		std::error_code _ec;
		if (std::filesystem::exists(_basePath + ".ojprfb", _ec)) return true;
		if (std::filesystem::exists(_basePath + ".obprfb", _ec)) return true;

		return false;
	}

	// ファイル名に使えない文字を落とす
	// ('.' はベースパス比較(拡張子除去)が狂うので同じく落とす)
	std::string SanitizePrefabName(const std::string& a_name)
	{
		std::string _res = {};
		for (const char& _c : a_name)
		{
			if (std::strchr("\\/:*?\"<>|.", _c) != nullptr) continue;
			_res += _c;
		}

		// 前後の空白を削る
		const size_t _begin = _res.find_first_not_of(" \t");
		if (_begin == std::string::npos) return "";
		const size_t _end = _res.find_last_not_of(" \t");

		return _res.substr(_begin, _end - _begin + 1);
	}

	// 重複しないプレハブ名を作る : 被っていたら末尾に _01, _02... と加算していく
	std::string MakeUniquePrefabName(const Resource::AssetDatabase& a_assetDB, const std::string& a_name)
	{
		const std::string _baseName = SanitizePrefabName(a_name);
		if (_baseName.empty()) return "";
		if (!IsUsedPrefabName(a_assetDB, _baseName)) return _baseName;

		for (UINT _i = 1; _i < 1000; ++_i)
		{
			char _suffix[8] = {};
			std::snprintf(_suffix, sizeof(_suffix), "_%02u", _i);

			std::string _candidate = _baseName + _suffix;
			if (!IsUsedPrefabName(a_assetDB, _candidate)) return _candidate;
		}

		// 空きが見つからなかった
		return "";
	}

	// 実行中だけの印か : プレハブには保存しない
	// ・フェーズタグは実体化時に付け直される
	// ・TransientTag は出したもの(スポナーのプレイヤーなど)の印。
	//   持ち込むと、そのプレハブをシーンへ置いても保存されなくなる
	bool IsRuntimeOnlyTag(ECS::World* a_pWorld, ECS::ComponentTypeID a_typeID)
	{
		return
			a_typeID == a_pWorld->GetCompTypeID<App::Component::PostDeserializeTag>() ||
			a_typeID == a_pWorld->GetCompTypeID<App::Component::AwakeTag>() ||
			a_typeID == a_pWorld->GetCompTypeID<App::Component::StartTag>() ||
			a_typeID == a_pWorld->GetCompTypeID<App::Component::ActiveTag>() ||
			a_typeID == a_pWorld->GetCompTypeID<App::Component::ReleaseTag>() ||
			a_typeID == a_pWorld->GetCompTypeID<Engine::ECS::TransientTag>();
	}

	// エンティティのGUIDを引く(持っていなければ既定値)
	Core::GUID GetEntityGUID(ECS::World* a_pWorld, const ECS::Entity& a_entity)
	{
		if (!a_pWorld->HasComponent<Engine::ECS::GUIDComponent>(a_entity)) return Core::GUID{};

		const auto* _pGUIDComp = a_pWorld->RefData<Engine::ECS::GUIDComponent>(a_entity);
		return _pGUIDComp ? _pGUIDComp->guid : Core::GUID{};
	}

	// プレハブの中で各ノードを見分ける鍵(保存時GUID)を作る。
	// GUID を持つならそれを使い、持たないもの(スポナーが出したプレイヤーなど)には新しく振る。
	// 鍵が無いと、スロットがどの子を指していたかをプレハブに残せない
	Core::GUID MakePrefabKey(ECS::World* a_pWorld, const ECS::Entity& a_entity)
	{
		Core::GUID _key = GetEntityGUID(a_pWorld, a_entity);
		if (!_key.IsValid()) _key.Create();
		return _key;
	}

	// アタッチメントのスロットを、指している相手の鍵へ書き換える。
	// GUID を持たない相手はスロットに id しか入っていないので、ここで鍵を入れておく
	// (実体化のときに、この鍵から同じインスタンスの中の相手を引き直す)
	void RekeyAttachmentSlots(
		uint8_t* a_pSlotsData,
		const std::unordered_map<ECS::Entity, Core::GUID>& a_keyMap)
	{
		if (!a_pSlotsData) return;

		App::Component::AttachmentSlotsComponent _slots = {};
		std::memcpy(&_slots, a_pSlotsData, sizeof(_slots));

		App::Component::ForEachAttachmentSlot(_slots,
			[&](App::Component::AttachmentSlot& a_slot)
			{
				auto _it = a_keyMap.find(a_slot.id);
				if (_it != a_keyMap.end()) a_slot.guid = _it->second;

				// id はシーン上の実体を指しているので持ち込まない
				a_slot.id = ECS::Limits::INVALID_ENTITY;
			}
		);

		std::memcpy(a_pSlotsData, &_slots, sizeof(_slots));
	}

	// 指定エンティティのコンポーネントをプレハブ用のバイト列へ写す
	void CopyComponents(
		ECS::World* a_pWorld,
		const ECS::Entity& a_entity,
		ECS::Signature& a_outSig,
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>& a_outDataMap)
	{
		const ECS::Signature _sig = a_pWorld->GetSignature(a_entity);

		for (size_t _i = 0; _i < _sig.size(); ++_i)
		{
			if (!_sig.test(_i)) continue;

			auto _compTypeID = static_cast<ECS::ComponentTypeID>(_i);
			if (IsRuntimeOnlyTag(a_pWorld, _compTypeID)) continue;

			const uint8_t* _pSrc = a_pWorld->NRefData(a_entity, _compTypeID);
			if (!_pSrc) continue;

			a_outSig.set(_compTypeID);

			auto& _buffer = a_outDataMap[_compTypeID];
			_buffer.assign(a_pWorld->GetComponentMetaData(_compTypeID).compAlignSize, 0);
			std::memcpy(_buffer.data(), _pSrc, a_pWorld->GetComponentMetaData(_compTypeID).compSize);
		}
	}

	// 親 → 子の対応表を作る(HierarchyComponent は親しか持たないので毎回組み立てる)
	std::unordered_map<ECS::Entity, std::vector<ECS::Entity>> BuildChildMap(ECS::World* a_pWorld)
	{
		std::unordered_map<ECS::Entity, std::vector<ECS::Entity>> _childMap = {};

		a_pWorld->ForEach<const App::Component::HierarchyComponent>(
			[&_childMap](
				ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const App::Component::HierarchyComponent* a_hierarchyArray)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					const App::Component::HierarchyComponent& _hierarchy = a_hierarchyArray[_i];
					if (_hierarchy.parentID == ECS::Limits::INVALID_ENTITY) continue;

					_childMap[_hierarchy.parentID].push_back(a_pChunk->entityData[_i]);
				}
			}
		);

		return _childMap;
	}

	// エンティティの現在の状態をプレハブアセットとして保存する
	// 子エンティティ(孫以降も含む)も一緒に覚える
	void CreatePrefabFromEntity(ECS::World* a_pWorld, const ECS::Entity& a_entity, const std::string& a_name)
	{
		if (!a_pWorld || a_entity == ECS::Limits::INVALID_ENTITY) return;

		// 名前の重複を解決する
		const std::string _prefabName = MakeUniquePrefabName(*a_pWorld->RefEngineServices()->pAssetDatabase, a_name);
		if (_prefabName.empty())
		{
			ENGINE_LOG("プレハブ名を決定できませんでした : %s", a_name.c_str());
			return;
		}

		// ---- 所持コンポーネントをまるごとコピーする ----
		Resource::Prefab _prefab;

		const ECS::Signature _sig = a_pWorld->GetSignature(a_entity);
		for (size_t _i = 0; _i < _sig.size(); ++_i)
		{
			if (!_sig.test(_i)) continue;

			auto _compTypeID = static_cast<ECS::ComponentTypeID>(_i);
			if (IsRuntimeOnlyTag(a_pWorld, _compTypeID)) continue;

			const uint8_t* _pSrc = a_pWorld->NRefData(a_entity, _compTypeID);
			if (!_pSrc) continue;

			_prefab.AddComponentData(a_pWorld, _compTypeID, _pSrc);
		}

		// ---- 実体固有の値はテンプレートに持ち込まない ----
		// GUIDは実体化時に振り直すので空にしておく。
		// ただし子から親・スロットから相手を指す鍵として必要なので、鍵をプレハブ側に覚えさせる
		// エンティティ → 鍵(スロットの書き換えに使う)
		std::unordered_map<ECS::Entity, Core::GUID> _keyMap = {};
		_keyMap[a_entity] = MakePrefabKey(a_pWorld, a_entity);
		_prefab.SetSavedGUID(_keyMap[a_entity]);

		if (uint8_t* _pGUIDData = _prefab.RefData(a_pWorld->GetCompTypeID<Engine::ECS::GUIDComponent>()))
		{
			Engine::ECS::GUIDComponent _guidComp = {};
			std::memcpy(_pGUIDData, &_guidComp, sizeof(_guidComp));
		}
		// 親はシーン上のエンティティを指しているので、ルート扱いへ戻す
		if (uint8_t* _pHierarchyData = _prefab.RefData(a_pWorld->GetCompTypeID<App::Component::HierarchyComponent>()))
		{
			App::Component::HierarchyComponent _hierarchyComp = {};
			std::memcpy(_pHierarchyData, &_hierarchyComp, sizeof(_hierarchyComp));
		}

		//------------------------------------------------------------------
		// ---- 子エンティティ(孫以降も)を一緒に覚える ----
		//------------------------------------------------------------------
		// 親から辿れる順(幅優先)に積む。親が子より先に並ぶので、
		// 実体化するときも上から順に作れば親子リンクが必ず解決できる。
		//
		// 子の GUID は保存したまま残す。実体化のたびにプレハブ側が振り直し、
		// 親リンクやアタッチメントの参照もそこで張り替わる。
		//------------------------------------------------------------------
		const auto _childMap = BuildChildMap(a_pWorld);

		// ルートの深さ。子の depth をここからの相対に直すために使う
		UINT _rootDepth = 0;
		if (a_pWorld->HasComponent<App::Component::HierarchyComponent>(a_entity))
		{
			if (const auto* _pRootHierarchy = a_pWorld->RefData<App::Component::HierarchyComponent>(a_entity))
			{
				_rootDepth = _pRootHierarchy->depth;
			}
		}

		// エンティティ → プレハブ内での位置(-1 = ルート)
		std::unordered_map<ECS::Entity, int> _indexMap = { { a_entity, -1 } };

		std::vector<ECS::Entity> _queue = { a_entity };

		// 子の材料。スロットを鍵で指し直してからプレハブへ積むので、いったんここに貯める
		std::vector<Resource::PrefabChild> _childVec = {};
		for (size_t _head = 0; _head < _queue.size(); ++_head)
		{
			const ECS::Entity _parent = _queue[_head];

			auto _it = _childMap.find(_parent);
			if (_it == _childMap.end()) continue;

			for (const ECS::Entity& _child : _it->second)
			{
				// 循環していても無限に積まないよう、一度見た相手は飛ばす
				if (_indexMap.find(_child) != _indexMap.end()) continue;

				Resource::PrefabChild _prefabChild = {};
				CopyComponents(a_pWorld, _child, _prefabChild.sig, _prefabChild.dataMap);

				_keyMap[_child] = MakePrefabKey(a_pWorld, _child);
				_prefabChild.savedGUID   = _keyMap[_child];
				_prefabChild.parentIndex = _indexMap[_parent];

				//------------------------------------------------------
				// 階層の値は「ルートを 0 とした」相対に直す
				//------------------------------------------------------
				// parentID はシーン上の実体を指したままなので必ず捨てる。
				// (親リンクは parentGUID から HierarchyLinkSystem が繋ぎ直す)
				// depth も元の階層のままだと、プレハブから出したときに
				// ルートが 0 なのに子だけ深いという食い違いが起きる。
				auto _hierarchyIt = _prefabChild.dataMap.find(a_pWorld->GetCompTypeID<App::Component::HierarchyComponent>());
				if (_hierarchyIt != _prefabChild.dataMap.end() &&
					_hierarchyIt->second.size() >= sizeof(App::Component::HierarchyComponent))
				{
					App::Component::HierarchyComponent _hierarchyComp = {};
					std::memcpy(&_hierarchyComp, _hierarchyIt->second.data(), sizeof(_hierarchyComp));

					_hierarchyComp.parentID = ECS::Limits::INVALID_ENTITY;
					_hierarchyComp.depth    = static_cast<UINT>(_rootDepth < _hierarchyComp.depth
						? (_hierarchyComp.depth - _rootDepth)
						: 1);

					std::memcpy(_hierarchyIt->second.data(), &_hierarchyComp, sizeof(_hierarchyComp));
				}

				_indexMap[_child] = static_cast<int>(_childVec.size());
				_childVec.push_back(std::move(_prefabChild));

				_queue.push_back(_child);
			}
		}

		// ---- スロットを鍵で指し直す(全員の鍵が揃ってから) ----
		const ECS::ComponentTypeID _slotsTypeID = a_pWorld->GetCompTypeID<App::Component::AttachmentSlotsComponent>();
		RekeyAttachmentSlots(_prefab.RefData(_slotsTypeID), _keyMap);
		for (Resource::PrefabChild& _prefabChild : _childVec)
		{
			auto _slotsIt = _prefabChild.dataMap.find(_slotsTypeID);
			if (_slotsIt == _prefabChild.dataMap.end() || _slotsIt->second.size() < sizeof(App::Component::AttachmentSlotsComponent)) continue;

			RekeyAttachmentSlots(_slotsIt->second.data(), _keyMap);
		}

		for (Resource::PrefabChild& _prefabChild : _childVec)
		{
			_prefab.AddChild(std::move(_prefabChild));
		}

		// ---- アセットとして登録して保存する ----
		const std::string _basePath = MakePrefabBasePath(_prefabName);

		// 書き出すだけでよい。
		// メタファイルとGUIDは、AssetDatabase の監視が新しいファイルを見つけて用意する
		_prefab.Save(a_pWorld, _basePath);

		ENGINE_LOG("プレハブを作成しました : %s", _basePath.c_str());
	}

	// プレハブ化ボタン + 名前入力ポップアップ
	void CreatePrefabButton(EditorContext& a_editContext, ECS::World* a_pWorld)
	{
		// インスペクターは関数なので、ポップアップは関数の中に置いて使い回す
		static NameInputPopup s_createPrefabPopup("Create Prefab", "Prefab Name##PrefabName", "Save");

		// 実際に保存される名前(被っていたら _01 が加算されたもの)を出しておく
		s_createPrefabPopup.SetHint(
			[&a_editContext](const std::string& a_name) -> std::string
			{
				const std::string _saveName = MakeUniquePrefabName(*a_editContext.pServices->pAssetDatabase, a_name);
				if (_saveName.empty()) return "Input prefab name...";
				return "Save to : " + MakePrefabBasePath(_saveName);
			});

		// 決定の中身は今のフレームの選択をつかむので、毎フレーム預け直す
		s_createPrefabPopup.SetOnConfirm(
			[&a_editContext, a_pWorld](const std::string& a_name)
			{
				CreatePrefabFromEntity(a_pWorld, a_editContext.GetPrimaryEntity(), a_name);
				return true;
			});

		if (Engine::EditorField::CreateButton("Create Prefab"))
		{
			// 入力の初期値はエンティティ名にしておく
			std::string _defaultName = "NewPrefab";

			const ECS::Entity _entity = a_editContext.GetPrimaryEntity();
			if (a_pWorld->HasComponent<App::Component::NameComponent>(_entity))
			{
				const char* _pName = a_pWorld->RefData<App::Component::NameComponent>(_entity)->name;
				if (_pName && _pName[0] != '\0') _defaultName = _pName;
			}

			s_createPrefabPopup.Open(_defaultName);
		}

		s_createPrefabPopup.Draw();
	}

	void EntityInspector(EditorContext& a_editContext)
	{
		// ワールド取得
		Engine::ECS::World* _pWorld = a_editContext.pServices->pSceneManager->RefWorld();
		if (!_pWorld || !_pWorld->IsInit()) return;

		// 削除は選択中のエンティティすべてが対象
		const int _selectedCount = static_cast<int>(a_editContext.selectedEntities.size());
		const std::string _removeLabel = (_selectedCount > 1)
			? ("RemoveEntity (" + std::to_string(_selectedCount) + ")")
			: std::string("RemoveEntity");

		if (Engine::EditorField::DeleteButton(_removeLabel.c_str()))
		{
			// 解放予約だけしておく。実際に消えるのは次の BeginFrame で、
			// その前に Release フェーズが走るので、借りているもの
			// (サウンドのボイス・ポーズ行列など)を返してから消える。
			// この場でチャンクは動かないため、選択リストをそのまま舐めてよい。
			for (const Engine::ECS::Entity& _removeEntity : a_editContext.selectedEntities)
			{
				if (_removeEntity == Engine::ECS::Limits::INVALID_ENTITY) continue;
				_pWorld->ReserveReleaseEntity(_removeEntity);
			}
			a_editContext.ClearEntitySelection();
		}

		const Engine::ECS::Entity _entity = a_editContext.GetPrimaryEntity();
		if (_entity == Engine::ECS::Limits::INVALID_ENTITY)
		{
			return;
		}

		// 前のシーンのIDを持ち越していないか確かめる。
		// 世代が違うだけの生きた添え字だと、別のエンティティの中身を
		// そのまま編集してしまうのでここで止める
		if (!_pWorld->IsAliveEntity(_entity))
		{
			a_editContext.ClearEntitySelection();
			Engine::EditorField::HelpText("No selected");
			return;
		}

		// 選択中のエンティティをプレハブとして保存する(対象はプライマリ選択の1体)
		CreatePrefabButton(a_editContext, _pWorld);

		Engine::EditorField::Value("Entity ID", "%llu", _entity);

		// 複数選択中は、コンポーネント編集の対象が先頭1体だけであることを明示しておく
		// (移動と削除は選択中すべてに効く)
		if (_selectedCount > 1)
		{
			Engine::EditorField::HelpText("(%d selected / editing the first)", _selectedCount);
		}

		// エンティティが持っているコンポーネントを羅列する
		Engine::ECS::Signature _sig = _pWorld->GetSignature(_entity);

		ECS::CompEditContext _compEditContext = {};
		_compEditContext.pWorld = _pWorld;

		for (size_t _typeID = 0; _typeID < _sig.size(); ++_typeID)
		{
			// 持っているコンポーネントのみ表示
			if (_sig.test(_typeID))
			{
				// ツリーノード表示
				const auto _compTypeID = static_cast<Engine::ECS::ComponentTypeID>(_typeID);
				auto& _metaData = _pWorld->GetComponentMetaData(_compTypeID);

				if (ImGui::TreeNodeEx(_metaData.name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Framed))
				{
					// コンポーネントごとの特殊エディター処理を入れる
					auto _func = _pWorld->GetCompFunc(_compTypeID).edit;
					_compEditContext.entity = _entity;
					_compEditContext.pData = _pWorld->NRefData(_entity, _compTypeID);
					if (_func)
					{
						_func(_compEditContext);
					}

					// コンポーネントを削除するボタン
					SubmitCommponent(a_editContext,_pWorld, _compTypeID);

					ImGui::TreePop();
				}
			}
		}

		// エンティティに対してコンポーネントを増やす
		AddComponent(a_editContext,_pWorld);
	}
}
