#include "GraphicsProfilerView.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Profile/GraphicsProfiler.h"

namespace Editor
{
	namespace
	{
		// 埋まり具合の閾値 : これを超えたら色を変え、気になる点にも出す
		constexpr double WARNING_RATIO	= 0.8;
		constexpr double ERROR_RATIO	= 0.95;

		// 空きはあるのに、一番大きい空き領域が全体のこの割合を切ったら断片化として知らせる
		constexpr double FRAGMENT_RATIO	= 0.05;

		// 埋まり具合ごとの色(バーと文字で揃える)
		const ImVec4 OK_COLOR		= ImVec4(0.30f, 0.70f, 0.35f, 1.0f);
		const ImVec4 WARNING_COLOR	= ImVec4(0.95f, 0.65f, 0.20f, 1.0f);
		const ImVec4 ERROR_COLOR	= ImVec4(0.95f, 0.30f, 0.30f, 1.0f);

		// 表のフラグ
		constexpr ImGuiTableFlags TABLE_FLAGS =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

		constexpr double KB = 1024.0;
		constexpr double MB = 1024.0 * 1024.0;

		double ToRatio(uint64_t a_used, uint64_t a_capacity)
		{
			if (a_capacity == 0) return 0.0;
			return static_cast<double>(a_used) / static_cast<double>(a_capacity);
		}

		const ImVec4& RatioColor(double a_ratio)
		{
			if (a_ratio >= ERROR_RATIO) return ERROR_COLOR;
			if (a_ratio >= WARNING_RATIO) return WARNING_COLOR;
			return OK_COLOR;
		}

		// バイト数を読みやすい単位で
		std::string FormatBytes(double a_bytes)
		{
			char _buf[64] = {};
			if (a_bytes >= MB)		std::snprintf(_buf, sizeof(_buf), "%.2f MB", a_bytes / MB);
			else if (a_bytes >= KB)	std::snprintf(_buf, sizeof(_buf), "%.1f KB", a_bytes / KB);
			else					std::snprintf(_buf, sizeof(_buf), "%.0f B", a_bytes);
			return _buf;
		}

		/// <summary>
		/// 埋まり具合のバー : 色は閾値で変わり、中に「使用 / 容量 (割合)」を出す
		/// </summary>
		void DrawUsageBar(uint64_t a_used, uint64_t a_capacity, const char* a_overlay = nullptr)
		{
			const double _ratio = ToRatio(a_used, a_capacity);

			char _buf[96] = {};
			if (!a_overlay)
			{
				std::snprintf(_buf, sizeof(_buf), "%llu / %llu  (%.1f%%)",
					static_cast<unsigned long long>(a_used),
					static_cast<unsigned long long>(a_capacity),
					_ratio * 100.0);
				a_overlay = _buf;
			}

			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, RatioColor(_ratio));
			ImGui::ProgressBar(static_cast<float>((std::min)(_ratio, 1.0)), ImVec2(-FLT_MIN, 0.0f), a_overlay);
			ImGui::PopStyleColor();
		}

		// 割合の欄 : 閾値を超えていれば色を付ける
		void DrawRatioText(double a_ratio)
		{
			ImGui::TextColored(RatioColor(a_ratio), "%.1f%%", a_ratio * 100.0);
		}

		// 履歴の折れ線
		void DrawHistoryPlot(const char* a_label, const std::vector<float>& a_values, const char* a_format)
		{
			if (a_values.empty()) return;

			char _overlay[64] = {};
			std::snprintf(_overlay, sizeof(_overlay), a_format, a_values.back());

			ImGui::PlotLines(a_label, a_values.data(), static_cast<int>(a_values.size()), 0, _overlay,
				0.0f, FLT_MAX, ImVec2(-FLT_MIN, 48.0f));
		}

		// 容量のあるものの表(ディスクリプタヒープ・毎フレームの構造体バッファで共通)
		void DrawUsageTable(const char* a_id, const std::vector<Graphics::GraphicsUsageProfile>& a_rows, bool a_isShowBytes)
		{
			const int _columnCount = a_isShowBytes ? 5 : 4;
			if (!ImGui::BeginTable(a_id, _columnCount, TABLE_FLAGS)) return;

			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Peak", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Peak %", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			if (a_isShowBytes) ImGui::TableSetupColumn("Capacity Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableHeadersRow();

			for (const auto& _row : a_rows)
			{
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(_row.name.c_str());

				ImGui::TableSetColumnIndex(1);
				DrawUsageBar(_row.used, _row.capacity);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%llu", static_cast<unsigned long long>(_row.peak));

				ImGui::TableSetColumnIndex(3);
				DrawRatioText(ToRatio(_row.peak, _row.capacity));

				if (a_isShowBytes)
				{
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(FormatBytes(static_cast<double>(_row.capacity * _row.strideBytes)).c_str());
				}
			}
			ImGui::EndTable();
		}
	}

	//======================================================================================
	// 履歴
	//======================================================================================
	void GraphicsProfilerView::History::Push(float a_value)
	{
		if (values.size() != HISTORY_LENGTH) values.assign(HISTORY_LENGTH, 0.0f);

		values[head] = a_value;
		head = (head + 1) % HISTORY_LENGTH;
		count = (std::min)(count + 1, HISTORY_LENGTH);
	}

	std::vector<float> GraphicsProfilerView::History::ToOrdered() const
	{
		std::vector<float> _ordered = {};
		_ordered.reserve(count);

		// 埋まりきるまでは 0 から、埋まった後は head(一番古いもの)から並ぶ
		const size_t _start = (count < HISTORY_LENGTH) ? 0 : head;
		for (size_t _i = 0; _i < count; ++_i)
		{
			_ordered.push_back(values[(_start + _i) % HISTORY_LENGTH]);
		}
		return _ordered;
	}

	void GraphicsProfilerView::PushHistory(const Graphics::GraphicsSnapshot& a_snapshot)
	{
		if (a_snapshot.captureCount == m_lastCaptureCount) return;
		m_lastCaptureCount = a_snapshot.captureCount;

		m_vramHistory.Push(static_cast<float>(static_cast<double>(a_snapshot.videoMemory.localUsage) / MB));

		float _srv = 0.0f;
		for (const auto& _view : a_snapshot.descriptorHeap.views)
		{
			if (_view.name == "SRV") _srv = static_cast<float>(_view.used);
		}
		m_srvHistory.Push(_srv);

		m_cbHistory.Push(static_cast<float>(static_cast<double>(a_snapshot.constantBuffer.graphicsUsedBytes) / KB));
		m_drawItemHistory.Push(static_cast<float>(a_snapshot.drawCount.drawItemCount));
	}

	//======================================================================================
	// 表示
	//======================================================================================
	void GraphicsProfilerView::Draw(EditorContext& /*a_editContext*/)
	{
		Graphics::GraphicsEngine* _pGE = MainEngine::Instance().RefGraphicsEngine();
		Graphics::GraphicsProfiler* _pProfiler = _pGE ? _pGE->RefProfiler() : nullptr;
		if (!_pProfiler)
		{
			Engine::EditorField::HelpText("GraphicsEngine is not running.");
			return;
		}

		// 表示しているあいだだけ取り直してもらう(結果が出るのは次の描画の終わり)
		if (!m_isPaused) _pProfiler->RequestCapture();

		Engine::EditorField::Field("Pause", m_isPaused);
		Engine::EditorField::Tooltip("取り直しを止めて、今の結果を眺める");

		if (ImGui::Button("Reset Peaks"))
		{
			_pProfiler->ResetPeaks();
		}
		Engine::EditorField::Tooltip("定数バッファと毎フレームのバッファの最大値を捨てる。\nディスクリプタヒープとメガバッファの最大値は作ってからの値なので消えない");

		const Graphics::GraphicsSnapshot& _snapshot = _pProfiler->GetSnapshot();
		if (_snapshot.captureCount == 0)
		{
			Engine::EditorField::HelpText("Collecting...");
			return;
		}

		PushHistory(_snapshot);

		Engine::EditorField::Line();

		if (!ImGui::BeginTabBar("GraphicsProfilerTabs")) return;

		if (ImGui::BeginTabItem("Overview"))
		{
			DrawOverview(_snapshot);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Video Memory"))
		{
			DrawVideoMemory(_snapshot);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Descriptor Heap"))
		{
			DrawDescriptorHeap(_snapshot);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Mega Buffer"))
		{
			DrawMegaBuffers(_snapshot);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Frame Buffer"))
		{
			DrawFrameBuffers(_snapshot);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Draw"))
		{
			DrawDrawCounts(_snapshot);
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	//======================================================================================
	// 全体 : VRAM・定数バッファと、気になる点
	//======================================================================================
	void GraphicsProfilerView::DrawOverview(const Graphics::GraphicsSnapshot& a_snapshot)
	{
		Engine::EditorField::Header("Video Memory");

		const auto& _vram = a_snapshot.videoMemory;
		if (_vram.isValid)
		{
			const std::string _local = "VRAM : " + FormatBytes(static_cast<double>(_vram.localUsage)) + " / " + FormatBytes(static_cast<double>(_vram.localBudget));
			DrawUsageBar(_vram.localUsage, _vram.localBudget, _local.c_str());
			Engine::EditorField::Tooltip("このプロセスが使っている量 / OS が許している量(Budget)。\n超えるとリソースがシステムメモリへ追い出されて急に遅くなる");

			const std::string _nonLocal = "Shared : " + FormatBytes(static_cast<double>(_vram.nonLocalUsage)) + " / " + FormatBytes(static_cast<double>(_vram.nonLocalBudget));
			DrawUsageBar(_vram.nonLocalUsage, _vram.nonLocalBudget, _nonLocal.c_str());

			DrawHistoryPlot("##VRAMHistory", m_vramHistory.ToOrdered(), "VRAM %.1f MB");
		}
		else
		{
			Engine::EditorField::HelpText("(ビデオメモリの使用量を取得できませんでした)");
		}

		Engine::EditorField::Header("Constant Buffer (per frame)");

		const auto& _cb = a_snapshot.constantBuffer;
		const std::string _graphics = "Graphics : " + FormatBytes(static_cast<double>(_cb.graphicsUsedBytes)) + " / " + FormatBytes(static_cast<double>(_cb.graphicsCapacityBytes));
		DrawUsageBar(_cb.graphicsUsedBytes, _cb.graphicsCapacityBytes, _graphics.c_str());
		Engine::EditorField::Tooltip("ルートCBVで渡す値を積むアップロード領域。溢れるとその定数は送られない");
		Engine::EditorField::Value("Graphics Peak", "%s", FormatBytes(static_cast<double>(_cb.graphicsPeakBytes)).c_str());

		const std::string _compute = "Compute : " + FormatBytes(static_cast<double>(_cb.computeUsedBytes)) + " / " + FormatBytes(static_cast<double>(_cb.computeCapacityBytes));
		DrawUsageBar(_cb.computeUsedBytes, _cb.computeCapacityBytes, _compute.c_str());
		Engine::EditorField::Value("Compute Peak", "%s", FormatBytes(static_cast<double>(_cb.computePeakBytes)).c_str());

		DrawHistoryPlot("##CBHistory", m_cbHistory.ToOrdered(), "Graphics %.1f KB");

		Engine::EditorField::Header("Warnings");
		DrawWarnings(a_snapshot);
	}

	//======================================================================================
	// ビデオメモリの用途別の内訳
	//
	// 大きい順に並べ、DXGI の使用量に対する割合をバーで出す。
	// 数えていないもの(ディスクリプタヒープ・PSO・ドライバ内部など)は差として最後に出す
	//======================================================================================
	void GraphicsProfilerView::DrawVideoMemory(const Graphics::GraphicsSnapshot& a_snapshot)
	{
		const auto& _breakdown = a_snapshot.videoMemoryBreakdown;
		if (!_breakdown.isValid)
		{
			Engine::EditorField::HelpText("(用途別の集計がありません)");
			return;
		}

		Engine::EditorField::HelpText("リソースを作ったときに付けた用途ごとの、今生きているぶんの合計。\n固定で確保した容量は、使っていなくてもそのまま出る");

		// 割合の分母 : 取れていれば DXGI の使用量、取れなければ数えたぶんの合計
		const auto& _vram = a_snapshot.videoMemory;
		const uint64_t _localDenominator = (_vram.isValid && _vram.localUsage > 0) ? _vram.localUsage : _breakdown.trackedLocalBytes;

		// VRAM 側の大きい順
		std::vector<const Graphics::VideoMemoryCategoryProfile*> _sorted = {};
		_sorted.reserve(_breakdown.categories.size());
		for (const auto& _row : _breakdown.categories) _sorted.push_back(&_row);
		std::stable_sort(_sorted.begin(), _sorted.end(),
			[](const Graphics::VideoMemoryCategoryProfile* a_l, const Graphics::VideoMemoryCategoryProfile* a_r)
			{
				if (a_l->localBytes != a_r->localBytes) return a_l->localBytes > a_r->localBytes;
				return a_l->nonLocalBytes > a_r->nonLocalBytes;
			});

		if (!ImGui::BeginTable("VideoMemoryTable", 4, TABLE_FLAGS)) return;

		ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn("VRAM", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Shared", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableHeadersRow();

		// 1 行ぶん : VRAM はバーの中に大きさと割合を出す
		auto _drawRow = [_localDenominator](const char* a_name, uint64_t a_localBytes, uint64_t a_nonLocalBytes, const char* a_count)
			{
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(a_name);

				ImGui::TableSetColumnIndex(1);
				char _overlay[64] = {};
				std::snprintf(_overlay, sizeof(_overlay), "%s  (%.1f%%)",
					FormatBytes(static_cast<double>(a_localBytes)).c_str(),
					ToRatio(a_localBytes, _localDenominator) * 100.0);

				// 割合は埋まり具合ではないので、閾値の色分けはしない
				ImGui::ProgressBar(static_cast<float>((std::min)(ToRatio(a_localBytes, _localDenominator), 1.0)), ImVec2(-FLT_MIN, 0.0f), _overlay);

				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(FormatBytes(static_cast<double>(a_nonLocalBytes)).c_str());

				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(a_count);
			};

		for (const Graphics::VideoMemoryCategoryProfile* _pRow : _sorted)
		{
			const std::string _count = std::to_string(_pRow->objectCount);
			_drawRow(_pRow->name.c_str(), _pRow->localBytes, _pRow->nonLocalBytes, _count.c_str());
		}

		// 数えたぶんの合計
		_drawRow("Tracked Total", _breakdown.trackedLocalBytes, _breakdown.trackedNonLocalBytes, "");

		// DXGI の使用量との差 : 数えていないぶん
		if (_vram.isValid)
		{
			const uint64_t _untrackedLocal = (_vram.localUsage > _breakdown.trackedLocalBytes) ? _vram.localUsage - _breakdown.trackedLocalBytes : 0;
			const uint64_t _untrackedNonLocal = (_vram.nonLocalUsage > _breakdown.trackedNonLocalBytes) ? _vram.nonLocalUsage - _breakdown.trackedNonLocalBytes : 0;
			_drawRow("Untracked", _untrackedLocal, _untrackedNonLocal, "");
			Engine::EditorField::Tooltip("DXGI の使用量から数えたぶんを引いた残り。\nディスクリプタヒープ・PSO・コマンドアロケーター・ドライバ内部、\nGPU の使い終わりを待って解放されたばかりのメモリなどが入る");
		}

		ImGui::EndTable();

		Engine::EditorField::HelpText("Other は用途を付けていないもの。大きければ付け先を探す");

		Engine::EditorField::Value("BLAS Compaction", "%u 件 / %s 減",
			_breakdown.blasCompactedCount,
			FormatBytes(static_cast<double>(_breakdown.blasCompactionSavedBytes)).c_str());
		Engine::EditorField::Tooltip("静的 BLAS をビルドの後で小さく作り直した数と、それで減った大きさ(起動してからの累計)");
	}

	//======================================================================================
	// 気になる点
	//
	// 容量の決まっているものが閾値を超えていないか、メガバッファが断片化していないかを並べる
	//======================================================================================
	void GraphicsProfilerView::DrawWarnings(const Graphics::GraphicsSnapshot& a_snapshot)
	{
		bool _isAny = false;

		auto _check = [&_isAny](const char* a_category, const std::string& a_name, uint64_t a_used, uint64_t a_capacity)
			{
				const double _ratio = ToRatio(a_used, a_capacity);
				if (_ratio < WARNING_RATIO) return;

				_isAny = true;
				if (_ratio >= ERROR_RATIO)
				{
					Engine::EditorField::ErrorText("%s / %s : %.1f%% (%llu / %llu)", a_category, a_name.c_str(), _ratio * 100.0,
						static_cast<unsigned long long>(a_used), static_cast<unsigned long long>(a_capacity));
				}
				else
				{
					Engine::EditorField::WarningText("%s / %s : %.1f%% (%llu / %llu)", a_category, a_name.c_str(), _ratio * 100.0,
						static_cast<unsigned long long>(a_used), static_cast<unsigned long long>(a_capacity));
				}
			};

		for (const auto& _view : a_snapshot.descriptorHeap.views)
		{
			// 一度でも上限近くまで行ったなら、今は下がっていても危ない
			_check("Descriptor", _view.name, _view.peak, _view.capacity);
		}
		for (const auto& _mega : a_snapshot.megaBuffers)
		{
			// 使われ方を写しているだけのものは、写し元で知らせるので重ねない
			if (_mega.isSharedUsage) continue;

			_check("Mega Buffer", _mega.name, _mega.allocated, _mega.capacity);

			// 空きはあるのに大きな塊が無い : 大きいメッシュが入らなくなる
			const uint32_t _free = _mega.capacity - _mega.allocated;
			if (_free > 0 && _mega.capacity > 0 &&
				ToRatio(_mega.largestFreeBlock, _mega.capacity) < FRAGMENT_RATIO &&
				_mega.largestFreeBlock < _free)
			{
				_isAny = true;
				Engine::EditorField::WarningText("Mega Buffer / %s : 断片化 (空き %u のうち最大の塊 %u、%u 個に分かれている)",
					_mega.name.c_str(), _free, _mega.largestFreeBlock, _mega.freeBlockCount);
			}
		}
		for (const auto& _frame : a_snapshot.frameBuffers)
		{
			_check("Frame Buffer", _frame.name, _frame.peak, _frame.capacity);
		}

		const auto& _cb = a_snapshot.constantBuffer;
		_check("Constant Buffer", "Graphics", _cb.graphicsPeakBytes, _cb.graphicsCapacityBytes);
		_check("Constant Buffer", "Compute", _cb.computePeakBytes, _cb.computeCapacityBytes);

		const auto& _vram = a_snapshot.videoMemory;
		if (_vram.isValid) _check("Video Memory", "VRAM", _vram.localUsage, _vram.localBudget);

		if (!_isAny)
		{
			Engine::EditorField::HelpText("(閾値 %.0f%% を超えているものはありません)", WARNING_RATIO * 100.0);
		}
	}

	//======================================================================================
	// ディスクリプタヒープ
	//======================================================================================
	void GraphicsProfilerView::DrawDescriptorHeap(const Graphics::GraphicsSnapshot& a_snapshot)
	{
		DrawUsageTable("DescriptorHeapTable", a_snapshot.descriptorHeap.views, false);

		Engine::EditorField::Value("Pending Free", "%llu", static_cast<unsigned long long>(a_snapshot.descriptorHeap.pendingFreeCount));
		Engine::EditorField::Tooltip("解放を預かっている席。GPUが使い終わったフレームの頭で空きへ戻る。\n増え続けるなら戻す処理が回っていない");

		DrawHistoryPlot("##SRVHistory", m_srvHistory.ToOrdered(), "SRV %.0f");
	}

	//======================================================================================
	// メガバッファ
	//======================================================================================
	void GraphicsProfilerView::DrawMegaBuffers(const Graphics::GraphicsSnapshot& a_snapshot)
	{
		Engine::EditorField::HelpText("メッシュをロードするときに切り出す領域。Usage〜Largest Free の単位は要素数");

		if (!ImGui::BeginTable("MegaBufferTable", 9, TABLE_FLAGS)) return;

		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Pending", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Peak %", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn("Free Blocks", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Largest Free", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Mapped", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Reserved", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableHeadersRow();

		uint64_t _totalDataBytes = 0;
		uint64_t _totalMappedBytes = 0;
		uint64_t _totalReservedBytes = 0;

		for (const auto& _row : a_snapshot.megaBuffers)
		{
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(_row.name.c_str());
			if (_row.isSharedUsage)
			{
				Engine::EditorField::Tooltip("自分では配らず、Animated Vertex と同じオフセットで使う。\nUsage〜Largest Free は Animated Vertex のもの");
			}

			ImGui::TableSetColumnIndex(1);
			DrawUsageBar(_row.allocated, _row.capacity);

			// 返されたが GPU が使い終わるのを待っている数(使用中に含まれている)
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%u", _row.pending);

			ImGui::TableSetColumnIndex(3);
			DrawRatioText(ToRatio(_row.peakAllocated, _row.capacity));

			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%u", _row.freeBlockCount);

			// 一度に確保できる上限
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%u", _row.largestFreeBlock);

			// 配っている中身・実メモリ・仮想アドレス(バイト)
			const uint64_t _dataBytes = static_cast<uint64_t>(_row.allocated) * _row.strideBytes;
			const uint64_t _usedEndBytes = static_cast<uint64_t>(_row.usedEnd) * _row.strideBytes;
			_totalDataBytes += _dataBytes;
			_totalMappedBytes += _row.mappedBytes;
			_totalReservedBytes += _row.reservedBytes;

			ImGui::TableSetColumnIndex(6);
			ImGui::TextUnformatted(FormatBytes(static_cast<double>(_dataBytes)).c_str());

			ImGui::TableSetColumnIndex(7);
			ImGui::TextUnformatted(FormatBytes(static_cast<double>(_row.mappedBytes)).c_str());
			Engine::EditorField::Tooltip("実メモリを割り当てた大きさ。使っている範囲の終わり(%s)を 4MB 単位に切り上げ、\n余裕を 1 単位足したところまで残し、それより後ろは空きが続くと返す",
				FormatBytes(static_cast<double>(_usedEndBytes)).c_str());

			ImGui::TableSetColumnIndex(8);
			ImGui::TextUnformatted(FormatBytes(static_cast<double>(_row.reservedBytes)).c_str());
			Engine::EditorField::Tooltip(_row.isReserved
				? "予約リソース : 仮想アドレスだけで、実メモリは Mapped のぶんだけ"
				: "committed : 最初から全体の実メモリを持っている(予約リソースが使えない GPU)");
		}
		ImGui::EndTable();

		Engine::EditorField::Value("Total", "Data %s / Mapped %s / Reserved %s",
			FormatBytes(static_cast<double>(_totalDataBytes)).c_str(),
			FormatBytes(static_cast<double>(_totalMappedBytes)).c_str(),
			FormatBytes(static_cast<double>(_totalReservedBytes)).c_str());
		Engine::EditorField::Tooltip("VRAM を実際に使っているのは Mapped。Data との差は断片化と、後ろに残している余裕");
	}

	//======================================================================================
	// 毎フレーム詰め直す構造体バッファ
	//======================================================================================
	void GraphicsProfilerView::DrawFrameBuffers(const Graphics::GraphicsSnapshot& a_snapshot)
	{
		Engine::EditorField::HelpText("フレームごとに描画要求を詰め直すバッファ。容量を超えたぶんは描かれない");

		DrawUsageTable("FrameBufferTable", a_snapshot.frameBuffers, true);
	}

	//======================================================================================
	// 描画要求の数・PSO
	//======================================================================================
	void GraphicsProfilerView::DrawDrawCounts(const Graphics::GraphicsSnapshot& a_snapshot)
	{
		Engine::EditorField::Value("Render Size", "%u x %u", a_snapshot.renderWidth, a_snapshot.renderHeight);

		Engine::EditorField::Header("Draw Requests");

		const auto& _draw = a_snapshot.drawCount;
		Engine::EditorField::Value("Draw Items", "%llu", static_cast<unsigned long long>(_draw.drawItemCount));
		Engine::EditorField::Tooltip("サブセット × それを受け取るパスの数。カメラが増えるとパスのぶん増える");
		DrawHistoryPlot("##DrawItemHistory", m_drawItemHistory.ToOrdered(), "%.0f items");

		Engine::EditorField::Value("Instances", "%llu", static_cast<unsigned long long>(_draw.instanceCount));
		Engine::EditorField::Value("Materials", "%llu", static_cast<unsigned long long>(_draw.materialCount));
		Engine::EditorField::Value("Skinning Dispatches", "%llu", static_cast<unsigned long long>(_draw.skinningCount));
		Engine::EditorField::Value("Dynamic Ray Requests", "%llu", static_cast<unsigned long long>(_draw.dynamicRayRequestCount));
		Engine::EditorField::Value("Bone Matrices", "%llu", static_cast<unsigned long long>(_draw.boneMatrixCount));
		Engine::EditorField::Value("UI", "%llu", static_cast<unsigned long long>(_draw.uiCount));
		Engine::EditorField::Value("Debug Lines", "%llu", static_cast<unsigned long long>(_draw.debugLineCount));
		Engine::EditorField::Value("Ground Impulses", "%llu", static_cast<unsigned long long>(_draw.groundImpulseCount));

		Engine::EditorField::Header("Pipeline State");

		Engine::EditorField::Value("PSO", "%llu", static_cast<unsigned long long>(a_snapshot.pipelineState.psoCount));
		Engine::EditorField::Value("Root Signature", "%llu", static_cast<unsigned long long>(a_snapshot.pipelineState.rootSignatureCount));
	}
}
