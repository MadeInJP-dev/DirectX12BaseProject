#include "HitchMonitor.h"

#include <ctime>

namespace Engine::Time
{
	namespace
	{
		// 書き出す先(作業ディレクトリからの相対)。起動のたびに作り直す
		constexpr const char* HITCH_LOG_FILE_PATH = "HitchLog.txt";

		// 出す数の上限
		constexpr size_t MAX_SCOPE_LINE = 12;
		constexpr size_t MAX_SYSTEM_LINE = 10;
		constexpr size_t MAX_EVENT_LINE = 40;

		// これより短い段・システムは出さない
		constexpr double MIN_LINE_MS = 0.5;

		// GPU を待った時間がこれを超えたら「GPU 側が重い」と見立てる
		constexpr double GPU_WAIT_HINT_MS = 3.0;

		// 間隔と処理の差がこれを超えたら「処理の外で止まった」と見立てる
		constexpr double OUTSIDE_HINT_MS = 5.0;

		// 名前ごとに足し合わせたもの
		struct SumEntry
		{
			std::string	name = {};
			double		ms = 0.0;
			uint32_t	count = 0;
		};

		std::string FormatText(const char* a_fmt, ...)
		{
			char _buf[1024];
			va_list _args;
			va_start(_args, a_fmt);
			vsnprintf(_buf, sizeof(_buf), a_fmt, _args);
			va_end(_args);
			return std::string(_buf);
		}

		// 同じ名前を足し合わせ、長い順に並べる
		std::vector<SumEntry> SumByName(const std::vector<const std::string*>& a_nameVec, const std::vector<double>& a_msVec)
		{
			std::vector<SumEntry> _entryVec = {};
			std::unordered_map<std::string_view, size_t> _indexMap = {};

			for (size_t _i = 0; _i < a_nameVec.size(); ++_i)
			{
				const std::string& _name = *a_nameVec[_i];
				auto [_it, _isInserted] = _indexMap.try_emplace(_name, _entryVec.size());
				if (_isInserted) _entryVec.push_back(SumEntry{ _name, 0.0, 0 });

				SumEntry& _entry = _entryVec[_it->second];
				_entry.ms += a_msVec[_i];
				++_entry.count;
			}

			std::sort(_entryVec.begin(), _entryVec.end(),
				[](const SumEntry& a_lhs, const SumEntry& a_rhs) { return a_lhs.ms > a_rhs.ms; });
			return _entryVec;
		}
	}

	void HitchMonitor::Init()
	{
		m_mainThreadID = std::this_thread::get_id();
		m_startTime = std::chrono::steady_clock::now();
		m_recordVec.reserve(256);

		// 受け口は終了まで差し替えない(ワーカーからも呼ばれるため)。
		// 記録するかどうかは SetEnabled が旗で切り替える
		Core::Debug::SetHitchCallback([this](const Core::Debug::HitchEvent& a_event) { OnEvent(a_event); });
	}

	void HitchMonitor::Release()
	{
		Core::Debug::SetHitchRecording(false);
		Core::Debug::SetHitchCallback(nullptr);

		// 一度も割らなかった起動ではファイルを作らない
		if (m_reportCount == 0 && m_loadingHitchCount == 0) return;

		WriteLine(FormatText("[Hitch] まとめ : 60FPS を割ったフレーム %u 回(読み込み中を除く)、読み込み中 %u 回、最長 %.1fms",
			m_reportCount, m_loadingHitchCount, m_worstMs));

		if (m_file.is_open()) m_file.close();
	}

	void HitchMonitor::SetEnabled(bool a_isEnabled)
	{
		if (m_isEnabled == a_isEnabled) return;
		m_isEnabled = a_isEnabled;

		Core::Debug::SetHitchRecording(a_isEnabled);

		// 入れ直したときに、切っていた間をまたいだ間隔を測らない
		m_hasPrevFrame = false;
		std::lock_guard _lock(m_mutex);
		m_recordVec.clear();
		m_droppedCount = 0;
	}

	void HitchMonitor::MarkWorkEnd()
	{
		m_workEnd = std::chrono::steady_clock::now();
	}

	void HitchMonitor::EndFrame(const std::string& a_sceneName, bool a_isLoading)
	{
		const auto _now = std::chrono::steady_clock::now();
		++m_frameIndex;

		if (!m_isEnabled) return;

		// このフレームのぶんを受け取って空にする(次のフレームの出来事と混ぜない)
		std::vector<Record> _recordVec = {};
		size_t _droppedCount = 0;
		{
			std::lock_guard _lock(m_mutex);
			_recordVec.swap(m_recordVec);
			_droppedCount = m_droppedCount;
			m_droppedCount = 0;
			m_recordVec.reserve(_recordVec.capacity());
		}

		const bool _hasPrev = m_hasPrevFrame;
		const auto _prevEnd = m_prevFrameEnd;
		m_prevFrameEnd = _now;
		m_hasPrevFrame = true;
		if (!_hasPrev) return;

		const double _intervalMs = std::chrono::duration<double, std::milli>(_now - _prevEnd).count();
		if (_intervalMs < THRESHOLD_MS) return;

		if (a_isLoading)
		{
			++m_loadingHitchCount;
			return;
		}

		m_worstMs = std::max(m_worstMs, _intervalMs);
		++m_reportCount;
		if (m_reportCount > MAX_REPORT_COUNT)
		{
			// 上限を超えたら数えるだけ。超えた瞬間に一度だけ知らせる
			if (m_reportCount == MAX_REPORT_COUNT + 1)
			{
				WriteLine(FormatText("[Hitch] 書き出しの上限(%u フレーム)に達したので、以降は数えるだけにします", MAX_REPORT_COUNT));
			}
			return;
		}

		const double _workMs = std::chrono::duration<double, std::milli>(m_workEnd - _prevEnd).count();
		WriteReport(_recordVec, _intervalMs, _workMs, a_sceneName);

		if (_droppedCount > 0)
		{
			WriteLine(FormatText("    (出来事が多すぎて %zu 件を捨てました)", _droppedCount));
		}

		// 書き出しで次のフレームが割れたように見えないよう、ここから測り直す
		m_prevFrameEnd = std::chrono::steady_clock::now();
	}

	void HitchMonitor::OnEvent(const Core::Debug::HitchEvent& a_event)
	{
		const bool _isWorker = (std::this_thread::get_id() != m_mainThreadID);

		// 計測スコープはメインスレッドのものだけ(ロックを取る前に弾く)
		const bool _isScope = (std::strcmp(a_event.category, "Scope") == 0);
		if (_isScope && _isWorker) return;

		std::lock_guard _lock(m_mutex);
		if (m_recordVec.size() >= MAX_RECORD_COUNT)
		{
			++m_droppedCount;
			return;
		}
		m_recordVec.push_back(Record{ a_event.category, a_event.label, a_event.ms, _isWorker });
	}

	void HitchMonitor::WriteReport(const std::vector<Record>& a_recordVec, double a_intervalMs, double a_workMs, const std::string& a_sceneName)
	{
		const double _elapsedSec = std::chrono::duration<double>(m_prevFrameEnd - m_startTime).count();

		WriteLine(FormatText("[Hitch] #%u フレーム %llu(起動から %.1f 秒) 間隔 %.1fms / 処理 %.1fms  シーン : %s",
			m_reportCount, static_cast<unsigned long long>(m_frameIndex), _elapsedSec,
			a_intervalMs, a_workMs, a_sceneName.empty() ? "(なし)" : a_sceneName.c_str()));

		//------------------------------------------------------------------------------
		// 種類ごとに分ける : 段とシステムは名前で足し合わせ、それ以外は起きた順のまま
		//------------------------------------------------------------------------------
		std::vector<const std::string*> _scopeNameVec = {};
		std::vector<double> _scopeMsVec = {};
		std::vector<const std::string*> _systemNameVec = {};
		std::vector<double> _systemMsVec = {};
		std::vector<const Record*> _eventVec = {};

		for (const Record& _record : a_recordVec)
		{
			if (std::strcmp(_record.category, "Scope") == 0)
			{
				_scopeNameVec.push_back(&_record.label);
				_scopeMsVec.push_back(_record.ms);
			}
			else if (std::strcmp(_record.category, "System") == 0)
			{
				_systemNameVec.push_back(&_record.label);
				_systemMsVec.push_back(_record.ms);
			}
			else
			{
				_eventVec.push_back(&_record);
			}
		}

		const std::vector<SumEntry> _scopeVec = SumByName(_scopeNameVec, _scopeMsVec);
		const std::vector<SumEntry> _systemVec = SumByName(_systemNameVec, _systemMsVec);

		// 段(メインスレッド)
		double _gpuWaitMs = 0.0;
		size_t _lineCount = 0;
		for (const SumEntry& _entry : _scopeVec)
		{
			if (_entry.name == "GPUFrameWait") _gpuWaitMs = _entry.ms;
			if (_entry.ms < MIN_LINE_MS || _lineCount >= MAX_SCOPE_LINE) continue;

			if (_lineCount == 0) WriteLine("  段(メインスレッド・長い順):");
			WriteLine(FormatText("    %7.2fms  %s", _entry.ms, _entry.name.c_str()));
			++_lineCount;
		}

		// システム : ジョブで分けて回したものは各バッチの合計(CPU 時間)なので、経過時間より長く出る
		_lineCount = 0;
		for (const SumEntry& _entry : _systemVec)
		{
			if (_entry.ms < MIN_LINE_MS || _lineCount >= MAX_SYSTEM_LINE) break;

			if (_lineCount == 0) WriteLine("  ECS のシステム(長い順・ジョブは CPU 時間の合計):");
			WriteLine(FormatText("    %7.2fms  %s", _entry.ms, _entry.name.c_str()));
			++_lineCount;
		}

		// 出来事(起きた順)
		for (size_t _i = 0; _i < _eventVec.size() && _i < MAX_EVENT_LINE; ++_i)
		{
			const Record& _record = *_eventVec[_i];
			if (_i == 0) WriteLine("  出来事(起きた順):");
			WriteLine(FormatText("    %7.2fms  [%s]%s %s",
				_record.ms, _record.category, _record.isWorker ? "(ワーカー)" : "", _record.label.c_str()));
		}
		if (_eventVec.size() > MAX_EVENT_LINE)
		{
			WriteLine(FormatText("    ほか %zu 件", _eventVec.size() - MAX_EVENT_LINE));
		}

		//------------------------------------------------------------------------------
		// 見立て : 数字だけでは読みにくいものに一言添える
		//------------------------------------------------------------------------------
		if (_gpuWaitMs >= GPU_WAIT_HINT_MS)
		{
			WriteLine(FormatText("  見立て : GPU を %.1fms 待っています。GPU 側(描画)が重いフレームです", _gpuWaitMs));
		}
		if (a_intervalMs - a_workMs >= OUTSIDE_HINT_MS)
		{
			WriteLine("  見立て : 処理の外(FPS 制限の待ち・ウィンドウの操作など)で止まっています");
		}
	}

	void HitchMonitor::WriteLine(const std::string& a_line)
	{
		ENGINE_LOG("%s", a_line.c_str());

		// ファイルは初めて書くときに作る(割らなかった起動では前回のものを残す)
		if (!m_file.is_open())
		{
			m_file.open(HITCH_LOG_FILE_PATH, std::ios::out | std::ios::trunc);
			if (!m_file.is_open()) return;

			const std::time_t _now = std::time(nullptr);
			std::tm _local = {};
			localtime_s(&_local, &_now);
			char _dateBuf[64] = {};
			std::strftime(_dateBuf, sizeof(_dateBuf), "%Y-%m-%d %H:%M:%S", &_local);

			m_file << "# 60FPS を割ったフレームの記録 " << _dateBuf
				<< "(閾値 " << THRESHOLD_MS << "ms。起動設定の isHitchLog で切り替え)\n";
		}

		m_file << a_line << '\n';
		m_file.flush();
	}
}
