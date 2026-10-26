#pragma once
#include "Editor/EditorCommon.h"

namespace Editor
{
	class IPanel;

	//======================================================================================
	//
	// パネルを並べる土台(画面いっぱいのドック)
	//
	// 土台の窓はメニューバーを持つので、メニューは BeginHost と EndHost の間に描く。
	//
	// 配置は imgui.ini に残るので、ふだんは前回の並びのまま開く。
	// 土台がまだ無いとき(初めて起動したとき)と Reset Layout のときだけ、
	// 各パネルの GetDefaultDock を見て既定の配置を組む。
	// 以前は既定の配置が無く、ini を消すとパネルが全部重なって浮いた状態で出ていた
	//
	//======================================================================================
	class DockLayout
	{
	public:

		// 土台の窓を開く(画面いっぱい・動かせない)
		void BeginHost();

		// ドックの場所を作る。初回と Reset のときは既定の配置を組んでから作る
		void DockSpace(const std::vector<std::unique_ptr<IPanel>>& a_panelVec);

		// 土台の窓を閉じる
		void EndHost();

		// 次のフレームで既定の配置へ戻す
		void RequestReset() { m_isResetRequested = true; }

	private:

		// 既定の配置を組む
		void BuildDefaultLayout(ImGuiID a_dockSpaceID, const std::vector<std::unique_ptr<IPanel>>& a_panelVec);

	private:

		bool m_isResetRequested = false;
	};
}
