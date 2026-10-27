#include "GameObjectHierarchyPanel.h"

#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/GameObject/BaseObject/BaseObject.h"
#include "Engine/GameObject/ObjectMetaRegistry/ObjectMetaRegistry.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include <nlohmannJSON/json.hpp>	// プレハブの中身を読む

#include "Application/Object/UI/UIBase.h"	// UI を親の飾りへまとめる

#include "../../Helper/EditorHelper.h"

//==========================================================================================
// GameObjectHierarchyPanel
//
// ECS外オブジェクトの一覧。ドラッグ&ドロップで親子関係を組める。
//
// ・親子は BaseObject の親子そのもの
//     子は親の行列に追従する(UI なら親の位置・回転・倍率に付いて動く)。
//     付け替えても見た目の置き場所は変わらない(ローカルの値を直す)。
//     親を消すと子も一緒に消える。
//
// ・見出しはオブジェクトの名前。名前が無ければクラス名
//
// ・プレハブ(ObjectPrefab)
//     右クリックの Save As Prefab で、そのオブジェクトと下を Asset/ObjectPrefab/ へ書き出す。
//     置くのは AddObject の From Prefab(根へ)か、右クリックの Place Prefab Here(その下へ)。
//     置いた時点で写すだけで、元のアセットとはつながらない(GUID は振り直される)
//
// ・UI は右クリックで親の飾りへまとめられる(Merge Into Parent)
//     別の UI にしておく理由(環境/Docs/UIGuidelines.md)が無いものだけ。
//     まとめられないときは、メニューに理由を出す
//
// ・検索中は平らに並べる
//     階層をたどると、一致した子が閉じた親の中に隠れて見つけられないため。
//==========================================================================================
namespace Editor
{
	namespace
	{
		// ドラッグ&ドロップで受け渡す種類の名札
		constexpr const char* DRAG_PAYLOAD_NAME = "GameObject";

		// プレハブの置き場と拡張子(AssetDatabase に "ObjectPrefab" として登録してある)
		constexpr const char* PREFAB_DIR = "Asset/ObjectPrefab";
		constexpr const char* PREFAB_EXT = "objprfb";

		// プレハブを読んで置く : 読めなければ nullptr
		GameObject::BaseObject* PlacePrefab(
			GameObject::GameObjectManager* a_pManager,
			const Engine::Resource::AssetProperty& a_prop,
			GameObject::BaseObject* a_pParent)
		{
			const std::string _path = a_prop.filePath + ".oj" + PREFAB_EXT;

			std::ifstream _ifs(_path);
			if (!_ifs.is_open())
			{
				ENGINE_WARNING("[GameObjectHierarchy] プレハブを開けません : %s", _path.c_str());
				return nullptr;
			}

			const nlohmann::json _json = nlohmann::json::parse(_ifs, nullptr, false);
			if (_json.is_discarded())
			{
				ENGINE_WARNING("[GameObjectHierarchy] プレハブの中身が読めません : %s", _path.c_str());
				return nullptr;
			}

			return a_pManager->InstantiatePrefab(_json, a_pParent);
		}

		// プレハブの一覧を出し、選ばれたものを返す(選ばれなければ nullptr)
		const Engine::Resource::AssetProperty* DrawPrefabList(EditorContext& a_editContext, const char* a_searchID)
		{
			return EditorHelper::DrawAssetSelectList(
				a_editContext.pServices->pAssetDatabase->GetTypeMetaVec("ObjectPrefab"), {}, a_searchID);
		}

		// 検索に当たるか : 名前とクラス名のどちらかに含まれていればよい
		bool IsMatchObject(const std::string& a_search, const GameObject::BaseObject* a_pObject)
		{
			return EditorHelper::IsMatchSearch(a_search, a_pObject->GetDisplayName())
				|| EditorHelper::IsMatchSearch(a_search, a_pObject->GetEditorName());
		}
	}

	void GameObjectHierarchyPanel::OnDrawImGui(EditorContext& a_editContext)
	{
		// 開いている間は、シーンビューのクリックとギズモをオブジェクト(UI など)が受け取る
		a_editContext.ReportHierarchyShown(ESceneViewTarget::GameObject, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));

		// 現在のシーンのオブジェクトマネージャーを取得
		auto* _pManager = a_editContext.pServices->pSceneManager->RefGameObjectManager();
		if (!_pManager)
		{
			Engine::EditorField::HelpText("No GameObjectManager");
			a_editContext.pGameObject = nullptr;
			return;
		}

		// ------------------------------------------------------------------
		// AddObject : クラスメタマネージャーに登録済みのクラスを選んでシーンへ追加する
		// ------------------------------------------------------------------
		if (Engine::EditorField::CreateButton("AddObject"))
		{
			ImGui::OpenPopup("AddObjectPopup");
		}
		if (ImGui::BeginPopup("AddObjectPopup"))
		{
			Engine::EditorField::HelpText("Select Class");
			Engine::EditorField::Line();

			const auto* _pRegistry = a_editContext.pServices ? a_editContext.pServices->pObjectRegistry : nullptr;
			if (!_pRegistry || _pRegistry->GetAllMeta().empty())
			{
				Engine::EditorField::HelpText("No registered class");
			}
			else
			{
				// 数が増えると探せなくなるのでクラス名で絞り込めるようにする
				const std::string& _search = EditorHelper::DrawSearchBox();
				const auto& _allMeta = _pRegistry->GetAllMeta();

				// クラス名順に並べて表示(map は順不同のため一旦ソート)
				// タイプIDは登録名のハッシュなので、IDで並べても意味のある順にはならない
				std::vector<GameObject::ObjectTypeID> _ids;
				_ids.reserve(_allMeta.size());
				for (const auto& [_id, _meta] : _allMeta) _ids.push_back(_id);
				std::sort(_ids.begin(), _ids.end(),
					[&_allMeta](GameObject::ObjectTypeID a_lhs, GameObject::ObjectTypeID a_rhs)
					{
						return _allMeta.at(a_lhs).name < _allMeta.at(a_rhs).name;
					});

				for (GameObject::ObjectTypeID _id : _ids)
				{
					const auto& _meta = _allMeta.at(_id);
					if (!EditorHelper::IsMatchSearch(_search, _meta.name)) continue;

					std::string _label = _meta.name + "##addobj" + std::to_string(_id);
					if (ImGui::Selectable(_label.c_str()))
					{
						// 生成してそのまま選択状態にする
						GameObject::BaseObject* _pNew = _pManager->AddObjectByTypeID(_id);
						if (_pNew)
						{
							a_editContext.SelectGameObject(_pNew);
						}
						ImGui::CloseCurrentPopup();
					}
				}
			}

			// プレハブから(根へ置く)
			Engine::EditorField::Line();
			Engine::EditorField::HelpText("From Prefab");
			if (const auto* _pProp = DrawPrefabList(a_editContext, "##AddPrefabSearch"))
			{
				if (GameObject::BaseObject* _pNew = PlacePrefab(_pManager, *_pProp, nullptr))
				{
					a_editContext.SelectGameObject(_pNew);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		const auto& _objects = _pManager->GetObjects();

		Engine::EditorField::Value("ObjectNum", "%d", static_cast<int>(_objects.size()));
		Engine::EditorField::Line();

		// 名前でオブジェクトを探す。出しっぱなしの欄なので入力は消さない
		const std::string& _search = EditorHelper::DrawSearchBox("##ObjectSearch", "Search object...", false);

		Engine::EditorField::Line();

		// 選択中ポインタがまだ生きているか検証(破棄やシーン切り替えでダングリング化するのを防ぐ)
		// 絞り込みで一覧から外れただけの相手を「消えた」と誤判定しないよう、検証は絞り込みの前に行う
		bool _selectedStillAlive = false;

		// 根(親の居ないもの)を集める。子は各オブジェクトが持っている一覧から辿る
		std::vector<GameObject::BaseObject*> _rootVec;

		for (const auto& _upObject : _objects)
		{
			GameObject::BaseObject* _pObject = _upObject.get();
			if (!_pObject) continue;

			if (_pObject == a_editContext.pGameObject) _selectedStillAlive = true;

			if (_pObject->GetParent() == nullptr) _rootVec.push_back(_pObject);
		}

		ImGui::BeginChild("GameObjectList");
		{
			if (_search.empty())
			{
				// 階層で出す
				for (GameObject::BaseObject* _pRoot : _rootVec)
				{
					DrawObjectNode(a_editContext, _pManager, _pRoot, _selectedStillAlive);
				}
			}
			else
			{
				//------------------------------------------------------
				// 検索中は平らに並べる
				//
				// 階層のままだと、一致した子が閉じた親の中に隠れてしまう
				//------------------------------------------------------
				for (const auto& _upObject : _objects)
				{
					GameObject::BaseObject* _pObject = _upObject.get();
					if (!_pObject) continue;
					if (!IsMatchObject(_search, _pObject)) continue;

					ImGui::PushID(_pObject);

					const bool _isSelected = (a_editContext.pGameObject == _pObject);
					ImGui::Selectable(_pObject->GetDisplayName(), _isSelected);

					HandleSelect(a_editContext, _pObject, _selectedStillAlive);
					HandleContextMenu(a_editContext, _pManager, _pObject);

					ImGui::PopID();
				}
			}

			//----------------------------------------------------------
			// 余白へ落とすと根へ戻す
			//
			// 親から外す手段が右クリックメニューだけだと、
			// 深いところから引っ張り出すのに手間がかかる
			//----------------------------------------------------------
			ImVec2 _rest = ImGui::GetContentRegionAvail();
			_rest.y = std::max(_rest.y, ImGui::GetTextLineHeight());
			ImGui::Dummy(_rest);

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* _pPayload = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD_NAME))
				{
					const Core::GUID _childGUID = *static_cast<const Core::GUID*>(_pPayload->Data);

					if (auto* _pChild = _pManager->FindByGUID(_childGUID))
					{
						_pChild->SetParent(nullptr);
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::EndChild();

		// 選択中オブジェクトが一覧に無くなっていたら選択解除
		if (a_editContext.pGameObject && !_selectedStillAlive)
		{
			a_editContext.pGameObject = nullptr;
		}
	}

	//======================================================================================
	// 1件ぶんのノード
	//======================================================================================
	void GameObjectHierarchyPanel::DrawObjectNode(
		EditorContext& a_editContext,
		GameObject::GameObjectManager* a_pManager,
		GameObject::BaseObject* a_pObject,
		bool& a_inoutIsSelectedAlive)
	{
		if (a_pObject == nullptr) return;

		// 子の一覧は写しを取って回す。
		// 下の項目へのドラッグ&ドロップで付け替えると、回している最中の一覧が書き換わるため
		const std::vector<GameObject::BaseObject*> _children = a_pObject->GetChildren();

		ImGuiTreeNodeFlags _flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanFullWidth |
			ImGuiTreeNodeFlags_DefaultOpen;

		if (a_editContext.pGameObject == a_pObject) _flags |= ImGuiTreeNodeFlags_Selected;

		// 子が居ないものは開けない見た目にする(三角が出ない)
		if (_children.empty()) _flags |= ImGuiTreeNodeFlags_Leaf;

		// ID はポインタで固定する。名前だけだと同じ名前が並んだときにぶつかる
		const bool _isOpen = ImGui::TreeNodeEx(a_pObject, _flags, "%s", a_pObject->GetDisplayName());

		// ポップアップは直前の項目の状態を消費するので、必ず最後に回す
		HandleSelect(a_editContext, a_pObject, a_inoutIsSelectedAlive);
		HandleDragAndDrop(a_pManager, a_pObject);
		HandleContextMenu(a_editContext, a_pManager, a_pObject);

		if (!_isOpen) return;

		for (GameObject::BaseObject* _pChild : _children)
		{
			DrawObjectNode(a_editContext, a_pManager, _pChild, a_inoutIsSelectedAlive);
		}

		ImGui::TreePop();
	}

	//======================================================================================
	// 選択
	//======================================================================================
	void GameObjectHierarchyPanel::HandleSelect(
		EditorContext& a_editContext,
		GameObject::BaseObject* a_pObject,
		bool& a_inoutIsSelectedAlive)
	{
		// 折りたたみの三角を押しただけのときは選択を変えない
		if (!ImGui::IsItemClicked() || ImGui::IsItemToggledOpen()) return;

		// ゲームオブジェクト選択に切り替え(ECSエンティティの選択は解除)
		a_editContext.SelectGameObject(a_pObject);
		a_inoutIsSelectedAlive = true;
	}

	//======================================================================================
	// 右クリックメニュー
	//--------------------------------------------------------------------------------------
	// ポップアップは「直前の項目」の状態を消費するので、
	// 選択判定とドラッグ&ドロップより後に呼ぶこと
	//======================================================================================
	void GameObjectHierarchyPanel::HandleContextMenu(
		EditorContext& a_editContext,
		GameObject::GameObjectManager* a_pManager,
		GameObject::BaseObject* a_pObject)
	{
		if (!ImGui::BeginPopupContextItem()) return;

		//------------------------------------------------------------------
		// UI : 親の飾りへまとめる
		//
		// まとめられるかは開いている間だけ調べる(GUID で指されていないかを全員の保存値から探すので重い)
		//------------------------------------------------------------------
		if (auto* _pUI = Core::TypeInfo::Cast<App::Object::UIBase>(a_pObject); _pUI && a_pObject->GetParent())
		{
			GameObject::ObjectContext& _context = a_pManager->RefObjectContext();

			std::string _reason = {};
			const bool _isMergeable = _pUI->CanMergeIntoParent(_context, _reason);

			if (ImGui::MenuItem("Merge Into Parent", nullptr, false, _isMergeable))
			{
				GameObject::BaseObject* _pParent = a_pObject->GetParent();
				if (_pUI->MergeIntoParent(_context) && a_editContext.pGameObject == a_pObject)
				{
					// 自分は消えるので、まとめ先を選び直す
					a_editContext.SelectGameObject(_pParent);
				}
			}
			if (!_isMergeable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("まとめられない : %s", _reason.c_str());
			}
			Engine::EditorField::Line();
		}

		//------------------------------------------------------------------
		// プレハブ
		//------------------------------------------------------------------
		if (ImGui::BeginMenu("Save As Prefab"))
		{
			// 開いた最初は表示名を入れておく(同じ名前があれば上書きする)
			if (m_prefabNameOwner != a_pObject)
			{
				m_prefabNameOwner = a_pObject;
				m_prefabName = a_pObject->GetDisplayName();
			}

			Engine::EditorField::Field("Name", m_prefabName);
			Engine::EditorField::HelpText("%s/%s.oj%s", PREFAB_DIR, m_prefabName.c_str(), PREFAB_EXT);

			if (!m_prefabName.empty() && ImGui::Button("Save"))
			{
				{
					Engine::Persistence::Archive _ar(
						Engine::Persistence::Archive::EMode::Save, PREFAB_DIR, m_prefabName, PREFAB_EXT,
						Engine::Persistence::Archive::EArchiveFormat::Json);
					a_pManager->SavePrefab(a_pObject, _ar);
				}
				ENGINE_LOG("[GameObjectHierarchy] プレハブを書き出しました : %s", m_prefabName.c_str());
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Place Prefab Here"))
		{
			if (const auto* _pProp = DrawPrefabList(a_editContext, "##PlacePrefabSearch"))
			{
				if (GameObject::BaseObject* _pNew = PlacePrefab(a_pManager, *_pProp, a_pObject))
				{
					a_editContext.SelectGameObject(_pNew);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndMenu();
		}
		Engine::EditorField::Line();

		if (a_pObject->GetParent())
		{
			if (ImGui::MenuItem("Unparent"))
			{
				a_pObject->SetParent(nullptr);
			}
			Engine::EditorField::Line();
		}

		// 子も一緒に消える(GameObjectManager::PreUpdate)
		if (ImGui::MenuItem("Destroy"))
		{
			a_pObject->ReserveDestroy();
			if (a_editContext.pGameObject == a_pObject)
			{
				a_editContext.pGameObject = nullptr;
			}
		}

		ImGui::EndPopup();
	}

	//======================================================================================
	// ドラッグ&ドロップで親を付け替える
	//======================================================================================
	void GameObjectHierarchyPanel::HandleDragAndDrop(
		GameObject::GameObjectManager* a_pManager,
		GameObject::BaseObject* a_pObject)
	{
		// 自分をドラッグする側
		if (ImGui::BeginDragDropSource())
		{
			const Core::GUID _guid = a_pObject->GetGUID();
			ImGui::SetDragDropPayload(DRAG_PAYLOAD_NAME, &_guid, sizeof(Core::GUID));

			Engine::EditorField::Text("%s", a_pObject->GetDisplayName());
			ImGui::EndDragDropSource();
		}

		// 自分の上に落とされる側
		if (!ImGui::BeginDragDropTarget()) return;

		if (const ImGuiPayload* _pPayload = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD_NAME))
		{
			const Core::GUID _childGUID = *static_cast<const Core::GUID*>(_pPayload->Data);

			// 自分自身・自分の子孫を親にする付け替えは SetParent が断る(輪ができるため)。
			// 見た目の置き場所はそのままで、ローカルの値が新しい親から見たものに直る
			if (auto* _pChild = a_pManager->FindByGUID(_childGUID))
			{
				_pChild->SetParent(a_pObject);
			}
		}

		ImGui::EndDragDropTarget();
	}
}
