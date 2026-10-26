#include "DockLayout.h"

#include "../IPanel.h"

// ドックを組み立てる関数(DockBuilder*)は内部ヘッダーにしか無い。ここだけで読む
#pragma warning(push, 0)
#include <imgui_internal.h>
#pragma warning(pop)

namespace Editor
{
	namespace
	{
		// 土台の名前。配置の作りを変えたら名前を変える(古い ini の配置を引き継がず、既定の配置で開き直す)
		constexpr const char* HOST_WINDOW_NAME = "EditorDockHost";
		constexpr const char* DOCK_SPACE_NAME = "EditorDockSpace_v2";

		// 画面に対する各欄の割合
		constexpr float LEFT_RATIO = 0.17f;				// 左(一覧)の幅
		constexpr float RIGHT_RATIO = 0.25f;			// 右(インスペクター)の幅
		constexpr float RIGHT_BOTTOM_RATIO = 0.38f;		// 右下(設定)の高さ
		constexpr float BOTTOM_RATIO = 0.30f;			// 下(アセット・ログ)の高さ

		// ドックの中でそのパネルのタブを前に出す。
		// タブの ID は ImGui が「窓の ID を種にした "#TAB"」で作る(ImGuiWindow::TabId)
		void SelectTab(ImGuiID a_nodeID, const char* a_windowName)
		{
			ImGuiDockNode* _pNode = ImGui::DockBuilderGetNode(a_nodeID);
			if (!_pNode) return;

			_pNode->SelectedTabId = ImHashStr("#TAB", 0, ImHashStr(a_windowName));
		}
	}

	void DockLayout::BeginHost()
	{
		// ドックの土台をクライアント領域全体に広げる
		//
		// 描画解像度(WindowOptionのwindowWidth/Height)を渡してはいけない。
		// ImGuiの座標系はクライアント領域基準なので、
		// 枠やDPI・ウィンドウモードでクライアント領域が変わった瞬間に
		// ドック領域だけが画面外へはみ出し、パネルのサイズが合わなくなる。
		const ImGuiViewport* _pViewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(_pViewport->WorkPos);
		ImGui::SetNextWindowSize(_pViewport->WorkSize);
		ImGui::SetNextWindowViewport(_pViewport->ID);

		// 土台の縁に余白を取らない(パネルを画面の端まで使う)
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		ImGui::Begin(
			HOST_WINDOW_NAME,
			nullptr,
			ImGuiWindowFlags_MenuBar |
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_NoDocking
		);

		ImGui::PopStyleVar(3);
	}

	void DockLayout::DockSpace(const std::vector<std::unique_ptr<IPanel>>& a_panelVec)
	{
		const ImGuiID _dockSpaceID = ImGui::GetID(DOCK_SPACE_NAME);

		// 組み立ては DockSpace を出す前に済ませる(出した後だと、そのフレームは前の形で描かれる)
		if (m_isResetRequested || ImGui::DockBuilderGetNode(_dockSpaceID) == nullptr)
		{
			BuildDefaultLayout(_dockSpaceID, a_panelVec);
			m_isResetRequested = false;
		}

		// 真ん中(シーンビュー)の後ろは塗らない。シーンの絵をそのまま見せるため
		ImGui::DockSpace(_dockSpaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
	}

	void DockLayout::EndHost()
	{
		ImGui::End();
	}

	//======================================================================================
	// 既定の配置
	//
	//  Left  |     Center      | Right
	//        |                 |-------------
	//        |-----------------| RightBottom
	//        |     Bottom      |
	//
	// 同じ場所に入るパネルはタブで重なる。前に出すのは、その場所に最初に登録されたもの
	//======================================================================================
	void DockLayout::BuildDefaultLayout(ImGuiID a_dockSpaceID, const std::vector<std::unique_ptr<IPanel>>& a_panelVec)
	{
		const ImGuiViewport* _pViewport = ImGui::GetMainViewport();

		ImGui::DockBuilderRemoveNode(a_dockSpaceID);
		ImGui::DockBuilderAddNode(a_dockSpaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(a_dockSpaceID, _pViewport->WorkSize);

		// 左右を先に切ってから、真ん中を上下に切る(下の欄は真ん中の幅だけ)
		ImGuiID _centerID = a_dockSpaceID;
		const ImGuiID _leftID = ImGui::DockBuilderSplitNode(_centerID, ImGuiDir_Left, LEFT_RATIO, nullptr, &_centerID);
		ImGuiID _rightID = ImGui::DockBuilderSplitNode(_centerID, ImGuiDir_Right, RIGHT_RATIO / (1.0f - LEFT_RATIO), nullptr, &_centerID);
		const ImGuiID _rightBottomID = ImGui::DockBuilderSplitNode(_rightID, ImGuiDir_Down, RIGHT_BOTTOM_RATIO, nullptr, &_rightID);
		const ImGuiID _bottomID = ImGui::DockBuilderSplitNode(_centerID, ImGuiDir_Down, BOTTOM_RATIO, nullptr, &_centerID);

		auto _toNodeID = [&](EPanelDock a_dock) -> ImGuiID
			{
				switch (a_dock)
				{
				case EPanelDock::Left:			return _leftID;
				case EPanelDock::Center:		return _centerID;
				case EPanelDock::Right:			return _rightID;
				case EPanelDock::RightBottom:	return _rightBottomID;
				case EPanelDock::Bottom:		return _bottomID;
				}
				return _bottomID;
			};

		// 場所ごとに最初のパネルを覚えておき、そのタブを前に出す
		std::unordered_map<ImGuiID, const char*> _frontPanelMap = {};
		for (const auto& _upPanel : a_panelVec)
		{
			const ImGuiID _nodeID = _toNodeID(_upPanel->GetDefaultDock());
			ImGui::DockBuilderDockWindow(_upPanel->GetName(), _nodeID);
			_frontPanelMap.try_emplace(_nodeID, _upPanel->GetName());

			// 閉じていたパネルも開き直す(初めの形へ戻すため)
			_upPanel->RefIsOpen() = true;
		}

		ImGui::DockBuilderFinish(a_dockSpaceID);

		for (const auto& [_nodeID, _pName] : _frontPanelMap)
		{
			SelectTab(_nodeID, _pName);
		}
	}
}
