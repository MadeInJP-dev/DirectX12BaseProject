#include "PanelManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "../Panel/RenderGraphResourceViewPanel/RenderGraphResourceViewPanel.h"
#include "../Panel/AssetDataBasePanel/AssetDataBasePanel.h"
#include "../Panel/InspectorPanel/InspectorPanel.h"
#include "../Panel/HierarchyPanel/HierarchyPanel.h"
#include "../Panel/GameObjectHierarchyPanel/GameObjectHierarchyPanel.h"
#include "../Panel/SceneViewPanel/SceneViewPanel.h"
#include "../Panel/ConfigPanel/ConfigPanel.h"
#include "../Panel/SceneSettingsPanel/SceneSettingsPanel.h"
#include "../Panel/ProfilerPanel/ProfilerPanel.h"
#include "../Panel/LogPanel/LogPanel.h"
#include "../Panel/BuildPanel/BuildPanel.h"

#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/ECS/World/World.h"
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"

namespace Editor
{
	void PanelManager::Init(EditorCamera* a_pEditorCamera, EffectEditor* a_pEffectEditor, PrefabEditor* a_pPrefabEditor, Profiler* a_pProfiler, ECS::EngineServices* a_pServices)
	{
		// 並びは画面の左から右、上から下の順。
		// 同じ場所に入るパネルは、ここで先に登録したものが前のタブになる(DockLayout)。
		// 描く順でもあるので、選ぶ側(一覧・シーンビュー)をインスペクターより先に置く
		RegisterPanel<HierarchyPanel>();				// 左
		RegisterPanel<GameObjectHierarchyPanel>();
		RegisterPanel<SceneViewPanel>();				// 真ん中
		RegisterPanel<InspectorPanel>();				// 右
		RegisterPanel<SceneSettingsPanel>();			// 右下
		RegisterPanel<ConfigPanel>();
		RegisterPanel<AssetDataBasePanel>();			// 下
		RegisterPanel<LogPanel>();
		RegisterPanel<ProfilerPanel>();
		RegisterPanel<RenderGraphResourceViewPanel>();
		RegisterPanel<BuildPanel>();

		m_editContext.pEditorCamera = a_pEditorCamera;
		m_editContext.pEffectEditor = a_pEffectEditor;
		m_editContext.pPrefabEditor = a_pPrefabEditor;
		m_editContext.pProfiler = a_pProfiler;
		m_editContext.pServices = a_pServices;

		m_mainMenuBar.Init(a_pServices);
	}

	void PanelManager::OnDrawPanels()
	{
		// 複数選択の修飾キー状態をフレーム頭で確定させる。
		// 各パネルが個別にキーを見ると、パネルごとに判定タイミングがずれるので
		// ここで一度だけ取って全パネルで共有する。
		m_editContext.m_isSelecting = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);

		// 選択中のものが前のシーンの遺物になっていないか確かめる
		ValidateContext();

		// 土台とメニューバー。配置の組み直しは DockSpace の前に済ませる
		m_dockLayout.BeginHost();
		if (m_mainMenuBar.Draw(m_upPanelVec))
		{
			m_dockLayout.RequestReset();
		}
		m_dockLayout.DockSpace(m_upPanelVec);
		m_dockLayout.EndHost();

		// メニューから開くポップアップは土台の外で出す(ID スタックが素の状態のところ)
		m_mainMenuBar.DrawPopups();

		for (auto& _panel : m_upPanelVec)
		{
			// 閉じたパネルは描かない(開き直すのはメニューの Window から)
			if (!_panel->RefIsOpen()) continue;

			if (ImGui::Begin(_panel->GetName(),&_panel->RefIsOpen(),_panel->GetFlags()))
			{
				_panel->OnDrawImGui(m_editContext);
			}
			ImGui::End();
		}
	}

	void PanelManager::ClearSceneContext()
	{
		m_editContext.ClearSceneContext();
	}

	void PanelManager::ValidateContext()
	{
		//------------------------------------------------------------------
		// 選択中のアセット
		//
		// 実体はアセットデータベースの持ち物で、ランタイム中にファイルが
		// 消えるとその要素ごと消える。前フレームのポインタをそのまま使うと
		// 解放済みメモリを描きにいくので、毎フレームGUIDから引き直す。
		// 引けなければそのアセットは消えているので選択を外す
		//------------------------------------------------------------------
		if (m_editContext.selectedAssetGUID.IsValid())
		{
			m_editContext.pAssetProp = (m_editContext.pServices && m_editContext.pServices->pAssetDatabase)
				? m_editContext.pServices->pAssetDatabase->FindAssetProperty(m_editContext.selectedAssetGUID)
				: nullptr;

			if (!m_editContext.pAssetProp)
			{
				m_editContext.ClearAssetSelection();

				// インスペクターが Asset のまま「選択なし」で残らないようにする
				if (m_editContext.eInspectorType == EInspectorType::Asset)
				{
					m_editContext.eInspectorType = EInspectorType::None;
				}
			}
		}
		else
		{
			// GUIDが無いのにポインタだけ残っている状態を作らない
			m_editContext.pAssetProp = nullptr;
		}

		//------------------------------------------------------------------
		// ECSエンティティ
		//------------------------------------------------------------------
		Engine::ECS::World* _pWorld = m_editContext.pServices->pSceneManager->RefWorld();
		if (!_pWorld || !_pWorld->IsInit())
		{
			m_editContext.ClearEntitySelection();
		}
		else
		{
			// 死んだものだけを取り除く。
			// 全消しにすると、複数選択中に1体だけ消えたときまで選択が飛ぶ
			std::erase_if(
				m_editContext.selectedEntities,
				[_pWorld](const ECS::Entity& a_entity)
				{
					if (a_entity == ECS::Limits::INVALID_ENTITY) return false;	// 未選択のダミーは残す
					return !_pWorld->IsAliveEntity(a_entity);
				}
			);

			// 空にはしない(未選択のときは INVALID を1つだけ持つ)という不変条件を守る
			if (m_editContext.selectedEntities.empty())
			{
				m_editContext.ClearEntitySelection();
			}
		}

		//------------------------------------------------------------------
		// ECS外オブジェクト
		//------------------------------------------------------------------
		if (!m_editContext.pGameObject) return;

		auto* _pObjManager = m_editContext.pServices->pSceneManager->RefGameObjectManager();
		if (!_pObjManager || !_pObjManager->IsManaged(m_editContext.pGameObject))
		{
			m_editContext.pGameObject = nullptr;

			// インスペクターが Game のまま「選択なし」で残らないようにする
			if (m_editContext.eInspectorType == EInspectorType::Game)
			{
				m_editContext.eInspectorType = EInspectorType::None;
			}
		}
	}
}
