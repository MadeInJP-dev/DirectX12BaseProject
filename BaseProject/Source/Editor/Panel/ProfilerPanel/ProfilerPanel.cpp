#include "ProfilerPanel.h"

#include "ProfilerView/OverviewProfilerView/OverviewProfilerView.h"
#include "ProfilerView/ScopeTimerProfilerView/ScopeTimerProfilerView.h"
#include "ProfilerView/ECSProfilerView/ECSProfilerView.h"
#include "ProfilerView/ThreadProfilerView/ThreadProfilerView.h"
#include "ProfilerView/GraphicsProfilerView/GraphicsProfilerView.h"
#include "ProfilerView/ParticleProfilerView/ParticleProfilerView.h"

#include "Engine/MainEngine.h"

namespace Editor
{
	namespace
	{
		// 左の一覧の幅(コンフィグパネルと揃える)
		constexpr float VIEW_LIST_WIDTH = 120.0f;
	}

	ProfilerPanel::ProfilerPanel()
	{
		// 並びは「全体 → 中身」の順。最初に開いたときは先頭(Overview)が出る
		RegisterView<OverviewProfilerView>();
		RegisterView<ScopeTimerProfilerView>();
		RegisterView<ECSProfilerView>();
		RegisterView<ThreadProfilerView>();
		RegisterView<GraphicsProfilerView>();
		RegisterView<ParticleProfilerView>();
	}

	//======================================================================================
	// パネル描画
	// ウィンドウのBegin/EndはPanelManagerが行うのでここでは触らない
	//======================================================================================
	void ProfilerPanel::OnDrawImGui(EditorContext& a_editContext)
	{
		DrawToolbar();
		ImGui::Separator();

		// 左 : 表示 / 右 : 中身
		if (ImGui::BeginChild("##ProfilerViewList", ImVec2(VIEW_LIST_WIDTH, 0.0f), ImGuiChildFlags_Borders))
		{
			DrawViewList();
		}
		ImGui::EndChild();

		ImGui::SameLine();

		if (ImGui::BeginChild("##ProfilerViewContent", ImVec2(0.0f, 0.0f)))
		{
			DrawSelectedView(a_editContext);
		}
		ImGui::EndChild();
	}

	void ProfilerPanel::DrawToolbar()
	{
		auto& _engine = MainEngine::Instance();

		Engine::EditorField::Text("FPS %d", _engine.GetFPS());
		Engine::EditorField::SameLine();
		Engine::EditorField::HelpText("|  %.2f ms", _engine.GetDeltaTime() * 1000.0f);
	}

	void ProfilerPanel::DrawViewList()
	{
		for (size_t _index = 0; _index < m_upViewVec.size(); ++_index)
		{
			if (Engine::EditorField::Selectable(m_upViewVec[_index]->GetName(), _index == m_selectedIndex))
			{
				m_selectedIndex = _index;
			}
		}
	}

	void ProfilerPanel::DrawSelectedView(EditorContext& a_editContext)
	{
		if (m_selectedIndex >= m_upViewVec.size()) return;

		IProfilerView& _view = *m_upViewVec[m_selectedIndex];

		ImGui::SeparatorText(_view.GetName());
		Engine::EditorField::HelpText("%s", _view.GetDescription());
		ImGui::Spacing();

		// 表示ごとに ID を分ける(同じ名前の欄やタブがあっても混ざらない)
		ImGui::PushID(_view.GetName());
		_view.Draw(a_editContext);
		ImGui::PopID();
	}
}
