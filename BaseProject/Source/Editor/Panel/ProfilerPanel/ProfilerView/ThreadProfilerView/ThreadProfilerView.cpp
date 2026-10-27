#include "ThreadProfilerView.h"

#include "Engine/MainEngine.h"
#include "Engine/JobSystem/JobSystem.h"
#include "Engine/JobSystem/Profile/ThreadProfiler.h"

namespace Editor
{
	namespace
	{
		// 状態ごとの色 : 帯グラフと凡例で揃える
		const ImVec4 BUSY_COLOR		= ImVec4(0.30f, 0.75f, 0.35f, 1.0f);
		const ImVec4 JOB_WAIT_COLOR	= ImVec4(0.95f, 0.60f, 0.20f, 1.0f);
		const ImVec4 IDLE_COLOR		= ImVec4(0.35f, 0.35f, 0.38f, 1.0f);

		// 気になる点として出す閾値
		constexpr double BALANCE_WARNING		= 1.5;	// ワーカーの Busy が 最大 / 平均 でこれを超えたら偏っている
		constexpr double MIN_WORKER_BUSY_MS		= 0.1;	// これより暇なら偏りは見ない(誤差で騒がない)
		constexpr double MAIN_JOB_WAIT_WARNING	= 0.2;	// メインのジョブ待ちがフレームのこの割合を超えたら
		constexpr double MAIN_BOTTLENECK		= 0.9;	// メインがこれ以上動いていて
		constexpr double WORKER_IDLE			= 0.2;	// ワーカーがこれ未満しか動いていなければメイン律速

		// 表のフラグ
		constexpr ImGuiTableFlags TABLE_FLAGS =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

		// 表示名 : 先頭がメイン
		std::string ThreadLabel(size_t a_index)
		{
			if (a_index == 0) return "Main";
			return "Worker " + std::to_string(a_index - 1);
		}

		// 凡例の色見本
		void DrawLegendItem(const ImVec4& a_color, const char* a_label)
		{
			ImGui::ColorButton(a_label, a_color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(10.0f, 10.0f));
			Engine::EditorField::SameLine();
			ImGui::TextUnformatted(a_label);
		}

		/// <summary>
		/// 1フレームの内訳を横一本の帯で描く : Busy / JobWait / Idle
		/// 各スレッドの帯を縦に並べると、どこが空いているかが一目で分かる
		/// </summary>
		void DrawBreakdownBar(const Thread::ThreadProfileStats& a_stats)
		{
			const double _total = a_stats.busyMs + a_stats.jobWaitMs + a_stats.idleMs;

			const float _width = (std::max)(ImGui::GetContentRegionAvail().x, 1.0f);
			const float _height = ImGui::GetTextLineHeight();
			const ImVec2 _min = ImGui::GetCursorScreenPos();

			// 描画範囲を確保(ホバーの判定にも使う)
			ImGui::Dummy(ImVec2(_width, _height));

			ImDrawList* _pDrawList = ImGui::GetWindowDrawList();
			_pDrawList->AddRectFilled(_min, ImVec2(_min.x + _width, _min.y + _height), ImGui::GetColorU32(ImGuiCol_FrameBg));
			if (_total <= 0.0) return;

			// 左から Busy -> JobWait -> Idle の順に積む
			const struct { double ms; ImVec4 color; } _segments[] = {
				{ a_stats.busyMs,		BUSY_COLOR },
				{ a_stats.jobWaitMs,	JOB_WAIT_COLOR },
				{ a_stats.idleMs,		IDLE_COLOR },
			};

			float _x = _min.x;
			for (const auto& _segment : _segments)
			{
				const float _segmentWidth = static_cast<float>(_segment.ms / _total) * _width;
				if (_segmentWidth <= 0.0f) continue;

				_pDrawList->AddRectFilled(
					ImVec2(_x, _min.y),
					ImVec2(_x + _segmentWidth, _min.y + _height),
					ImGui::GetColorU32(_segment.color));
				_x += _segmentWidth;
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(
					"Busy     : %.3f ms\nJob Wait : %.3f ms\nIdle     : %.3f ms",
					a_stats.busyMs, a_stats.jobWaitMs, a_stats.idleMs);
			}
		}
	}

	//======================================================================================
	// 表示
	//======================================================================================
	void ThreadProfilerView::Draw(EditorContext& /*a_editContext*/)
	{
		Thread::JobSystem* _pJobSystem = MainEngine::Instance().RefJobSystem();
		Thread::ThreadProfiler* _pProfiler = _pJobSystem ? _pJobSystem->RefThreadProfiler() : nullptr;
		if (!_pProfiler)
		{
			Engine::EditorField::HelpText("JobSystem is not running.");
			return;
		}

		// 平均を取り直す間隔
		int _averageRate = _pProfiler->GetAverageRate();
		if (Engine::EditorField::Field("Average Rate (frame)", _averageRate, 1.0f, 1, 600))
		{
			_pProfiler->SetAverageRate(_averageRate);
		}
		if (ImGui::Button("Reset"))
		{
			_pProfiler->Reset();
		}

		const Thread::ThreadProfileSnapshot& _snapshot = _pProfiler->GetSnapshot();
		if (_snapshot.publishCount == 0 || _snapshot.threads.empty())
		{
			Engine::EditorField::HelpText("Collecting... (%d frames per sample)", _pProfiler->GetAverageRate());
			return;
		}

		Engine::EditorField::Line();
		DrawSummary(_snapshot);

		Engine::EditorField::Line();

		DrawThreadTable(_snapshot);
	}

	//======================================================================================
	// 全体の要約
	//
	// 表の数字を眺めるだけでは「正常かどうか」が分かりにくいので、
	// 判断に使う指標をまとめて上に出す
	//======================================================================================
	void ThreadProfilerView::DrawSummary(const Thread::ThreadProfileSnapshot& a_snapshot)
	{
		const auto& _threads = a_snapshot.threads;
		const Thread::ThreadProfileStats& _main = _threads[0];
		const size_t _workerCount = _threads.size() - 1;

		// ワーカーの集計
		double _workerBusySum = 0.0;
		double _workerBusyMax = 0.0;
		double _workerBusyMin = (std::numeric_limits<double>::max)();
		double _workerJobSum = 0.0;
		double _workerStealSum = 0.0;
		for (size_t _i = 1; _i < _threads.size(); ++_i)
		{
			const auto& _stats = _threads[_i];
			_workerBusySum += _stats.busyMs;
			_workerBusyMax = (std::max)(_workerBusyMax, _stats.busyMs);
			_workerBusyMin = (std::min)(_workerBusyMin, _stats.busyMs);
			_workerJobSum += _stats.jobCount;
			_workerStealSum += _stats.stealCount;
		}
		if (_workerCount == 0) _workerBusyMin = 0.0;

		const double _workerBusyAvg = (_workerCount > 0) ? _workerBusySum / static_cast<double>(_workerCount) : 0.0;
		const double _workerUtilization = (a_snapshot.frameMs > 0.0) ? _workerBusyAvg / a_snapshot.frameMs : 0.0;

		// 偏り : 一番働いたワーカーが平均の何倍か。1.0 なら均等
		const double _balance = (_workerBusyAvg > 0.0) ? _workerBusyMax / _workerBusyAvg : 0.0;

		// 並列度 : 全スレッドの Busy の合計がフレーム何本ぶんか = 平均して何コア使えているか
		const double _parallelism = (a_snapshot.frameMs > 0.0)
			? (_main.busyMs + _workerBusySum) / a_snapshot.frameMs
			: 0.0;

		Engine::EditorField::Text("Averaged over %u frames  /  Frame : %.3f ms", a_snapshot.sampleFrameCount, a_snapshot.frameMs);

		if (ImGui::BeginTable("ThreadSummary", 2, TABLE_FLAGS))
		{
			auto _row = [](const char* a_label, const char* a_tooltip)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(a_label);
					if (a_tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a_tooltip);
					ImGui::TableSetColumnIndex(1);
				};

			_row("Parallelism", "Sum of busy time of all threads / frame time.\nHow many cores are effectively used.");
			ImGui::Text("%.2f / %zu threads", _parallelism, _threads.size());

			_row("Main Busy", "Main thread time excluding job waits and GPU / vsync / frame-limit waits.");
			ImGui::Text("%.3f ms (%.1f%%)", _main.busyMs, _main.utilization * 100.0);

			_row("Main Job Wait", "Time the main thread was blocked in WaitFor / WaitForAll.");
			ImGui::Text("%.3f ms", _main.jobWaitMs);

			_row("Worker Busy (avg)", nullptr);
			ImGui::Text("%.3f ms (%.1f%%)  min %.3f / max %.3f", _workerBusyAvg, _workerUtilization * 100.0, _workerBusyMin, _workerBusyMax);

			_row("Worker Balance", "Busiest worker / average worker. 1.00 means perfectly even.");
			ImGui::Text("%.2f", _balance);

			_row("Jobs / frame", nullptr);
			ImGui::Text("%.1f  (stolen %.1f)", _workerJobSum, _workerStealSum);

			ImGui::EndTable();
		}

		//------------------------------------------------------------------
		// 気になる点 : 閾値は目安。引っかかったら表で中身を確かめる
		//------------------------------------------------------------------
		bool _isHealthy = true;
		auto _warn = [&_isHealthy](const char* a_format, auto... a_args)
			{
				_isHealthy = false;
				Engine::EditorField::WarningText(a_format, a_args...);
			};

		if (_workerJobSum < 0.5)
		{
			_warn("! No jobs are running on the workers.");
		}
		if (_workerBusyAvg >= MIN_WORKER_BUSY_MS && _balance > BALANCE_WARNING)
		{
			_warn("! Work is concentrated on a few workers (max / avg = %.2f).", _balance);
		}
		if (a_snapshot.frameMs > 0.0 && _main.jobWaitMs / a_snapshot.frameMs > MAIN_JOB_WAIT_WARNING)
		{
			_warn("! Main thread waits for jobs %.1f%% of the frame. Check dependencies / job granularity.",
				_main.jobWaitMs / a_snapshot.frameMs * 100.0);
		}
		if (_main.utilization > MAIN_BOTTLENECK && _workerUtilization < WORKER_IDLE)
		{
			_warn("! Main thread is the bottleneck while workers are mostly idle.");
		}
		if (_isHealthy)
		{
			Engine::EditorField::HelpText("No issues detected.");
		}
	}

	//======================================================================================
	// スレッドごとの表
	//======================================================================================
	void ThreadProfilerView::DrawThreadTable(const Thread::ThreadProfileSnapshot& a_snapshot)
	{
		// 凡例
		DrawLegendItem(BUSY_COLOR, "Busy");
		ImGui::SameLine(0.0f, 16.0f);
		DrawLegendItem(JOB_WAIT_COLOR, "Job Wait");
		ImGui::SameLine(0.0f, 16.0f);
		DrawLegendItem(IDLE_COLOR, "Idle");

		if (!ImGui::BeginTable("ThreadTable", 8, TABLE_FLAGS)) return;

		ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Frame Breakdown", ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn("Busy(ms)");
		ImGui::TableSetupColumn("Util");
		ImGui::TableSetupColumn("Max(ms)");
		ImGui::TableSetupColumn("Wait(ms)");
		ImGui::TableSetupColumn("Jobs/f");
		ImGui::TableSetupColumn("Steal/f");
		ImGui::TableHeadersRow();

		for (size_t _i = 0; _i < a_snapshot.threads.size(); ++_i)
		{
			const Thread::ThreadProfileStats& _stats = a_snapshot.threads[_i];
			const bool _isMain = (_i == 0);

			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(ThreadLabel(_i).c_str());

			ImGui::TableSetColumnIndex(1);
			DrawBreakdownBar(_stats);

			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.3f", _stats.busyMs);

			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%.1f%%", _stats.utilization * 100.0);

			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%.3f", _stats.maxBusyMs);

			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%.3f", _stats.jobWaitMs);

			// メインはジョブを実行しないので件数は出さない
			ImGui::TableSetColumnIndex(6);
			if (_isMain) ImGui::TextDisabled("-");
			else		 ImGui::Text("%.1f", _stats.jobCount);

			ImGui::TableSetColumnIndex(7);
			if (_isMain) ImGui::TextDisabled("-");
			else		 ImGui::Text("%.1f", _stats.stealCount);
		}

		ImGui::EndTable();
	}
}
