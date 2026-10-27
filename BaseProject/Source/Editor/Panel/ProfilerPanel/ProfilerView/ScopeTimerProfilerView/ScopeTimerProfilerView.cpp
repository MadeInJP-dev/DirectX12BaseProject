#include "ScopeTimerProfilerView.h"

#include "../../../../Profiler/Profiler.h"

namespace Editor
{
	//======================================================================================
	// スコープごとの計測結果
	//
	// ENGINE_PROFILE_SCOPE が投げてくるのは「名前と1回ぶんの時間」だけで、
	// 平均や最小最大の組み立てと並べ替えは Profiler が済ませている。
	// ここは受け取った順に並べるだけ
	//======================================================================================
	void ScopeTimerProfilerView::Draw(EditorContext& a_editContext)
	{
		Profiler* _pProfiler = a_editContext.pProfiler;

		if (!_pProfiler)
		{
			Engine::EditorField::HelpText("Profiler is not available.");
			return;
		}

		// 平均を取り直す間隔
		int _avelageRate = _pProfiler->GetAvelageRate();
		if (Engine::EditorField::Field("Avelage Rate (frame)", _avelageRate, 1.0f, 1, 600))
		{
			_pProfiler->SetAvelageRate(_avelageRate);
		}

		// 全体でリセット
		if (ImGui::Button("Reset"))
		{
			_pProfiler->ResetAll();
		}
		Engine::EditorField::Line();

		const auto& _results = _pProfiler->GetResults();
		if (_results.empty())
		{
			Engine::EditorField::HelpText("No scope has been measured yet.");
			return;
		}

		// 描画
		constexpr ImGuiTableFlags TABLE_FLAGS =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

		if (ImGui::BeginTable("TimerTable", 7, TABLE_FLAGS))
		{
			ImGui::TableSetupColumn("Title");
			ImGui::TableSetupColumn("CPU(ms)");
			ImGui::TableSetupColumn("Avg(ms)");
			ImGui::TableSetupColumn("Min(ms)");
			ImGui::TableSetupColumn("Max(ms)");
			ImGui::TableSetupColumn("Calls");
			ImGui::TableSetupColumn("Total");
			ImGui::TableHeadersRow();

			for (const auto& _result : _results)
			{
				const ScopeTimer& _timer = _result.timer;

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s", _result.name.c_str());

				// このフレームで通らなかったものは、直近の値を出しても嘘になるので伏せる
				ImGui::TableSetColumnIndex(1);
				if (_timer.callCount > 0)
				{
					ImGui::Text("%.3f", _timer.time);
				}
				else
				{
					ImGui::TextDisabled("-");
				}

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.3f", _timer.averageTime);

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%.3f", _timer.minTime);

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.3f", _timer.maxTime);

				// このフレームで通った回数 : ループの中で計っているものはここが伸びる
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%d", _timer.callCount);

				// リセット以降の総計測回数
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%d", _timer.totalCallCount);
			}
			ImGui::EndTable();
		}
	}
}
