#include "SystemManager.h"

#include "../World/Profile/ECSWorldProfiler.h"

#include "../../JobSystem/JobSystem.h"

namespace Engine::ECS
{

	namespace
	{
		// 同時に走らせると壊れる組み合わせ : 向きは問わない。コンポーネントもリソースも同じ見方
		bool IsConflict(const SystemTask& a_lhs,const SystemTask& a_rhs)
		{
			return (a_lhs.writeSig & (a_rhs.readSig | a_rhs.writeSig)).any()	// 書く * 読む / 書く * 書く
				|| (a_lhs.readSig & a_rhs.writeSig).any()						// 読む * 書く
				|| (a_lhs.resWriteSig & (a_rhs.resReadSig | a_rhs.resWriteSig)).any()
				|| (a_lhs.resReadSig & a_rhs.resWriteSig).any();
		}

		//------------------------------------------------------------------------------------------
		// 衝突をアーキタイプで見分けてよいか : どちらもクエリの配列越しにしか触っていない
		//
		// ・リソースの衝突は対象のエンティティと関係が無い
		// ・カスタムタスクは中で何を回すか分からない(クエリを持たない)
		// ・TaskAccess::Reads / Writes の読み書きは別のエンティティに届きうる。
		//   ただし読み同士はぶつからないので、衝突の組ごとに片方でも配列越しでないかを見る
		//------------------------------------------------------------------------------------------
		bool CanJudgeByArchetype(const SystemTask& a_prev, const SystemTask& a_cur)
		{
			const bool _isResourceConflict =
				(a_prev.resWriteSig & (a_cur.resReadSig | a_cur.resWriteSig)).any() ||
				(a_prev.resReadSig & a_cur.resWriteSig).any();
			if (_isResourceConflict) return false;

			if (!a_prev.prepareFunc || !a_cur.prepareFunc) return false;

			// 衝突の組(書く * 読む / 書く * 書く)のうち、どちらかが配列越しでないもの
			auto _nonLocalConflict = [](const SystemTask& a_lhs, const SystemTask& a_rhs)
				{
					return (a_lhs.lookupWriteSig & (a_rhs.readSig | a_rhs.writeSig)) |
						(a_lhs.lookupReadSig & a_rhs.writeSig);
				};
			return (_nonLocalConflict(a_prev, a_cur) | _nonLocalConflict(a_cur, a_prev)).none();
		}

		// a が読むものを b が書く(RAW) : b の後に a を並べる辺。コンポーネントもリソースも数える
		bool IsReadAfterWrite(const SystemTask& a_reader, const SystemTask& a_writer)
		{
			return (a_reader.readSig & a_writer.writeSig).any()
				|| (a_reader.resReadSig & a_writer.resWriteSig).any();
		}

		// 処理を回して、所要時間を a_pOutNs へ足し込む : 同期・ジョブ関係なく
		// 分割したタスクは複数のワーカーから同じ置き場へ足すので、代入ではなく加算にする。
		// 読むのは全ジョブを待ち終えたメインスレッドなので、順序は WaitFor 側で揃っている
		template<typename Func>
		void Measure(std::atomic<int64_t>* a_pOutNs, Func&& a_func)
		{
			// 計測しないときは時計も読まない
			if (!a_pOutNs)
			{
				a_func();
				return;
			}

			const auto _begin = std::chrono::steady_clock::now();
			a_func();
			const auto _end = std::chrono::steady_clock::now();

			a_pOutNs->fetch_add(
				std::chrono::duration_cast<std::chrono::nanoseconds>(_end - _begin).count(),
				std::memory_order_relaxed);
		}

		// タスクを丸ごと1回実行する
		void ExecuteTask(SystemTask& a_task, const SystemContext& a_context, std::atomic<int64_t>* a_pOutNs)
		{
			Measure(a_pOutNs, [&]() { a_task.executeFunc(a_task, a_context); });
		}

		// タスクのチャンク [begin, end) を実行する
		void ExecuteTaskRange(SystemTask& a_task, const SystemContext& a_context, uint32_t a_begin, uint32_t a_end, std::atomic<int64_t>* a_pOutNs)
		{
			Measure(a_pOutNs, [&]() { a_task.executeRangeFunc(a_task, a_context, a_begin, a_end); });
		}
	}

	void SystemManager::Hold(std::unique_ptr<ISystem> a_upSystem)
	{
		if (!a_upSystem) return;
		m_upSystemVec.push_back(std::move(a_upSystem));
	}

	void SystemManager::Init()
	{
		m_upSystemVec.clear();
	}

	void SystemManager::RunSystem(const ESystemType& a_type, const SystemContext& a_context, ECSWorldProfiler* a_pProfiler)
	{
		// フェーズ検索
		auto _cit = m_compiledTaskMap.find(a_type);
		if (_cit == m_compiledTaskMap.end()) return;

		// フェーズ内システムのチェック
		auto& _compiledVec = _cit->second;
		const uint32_t _taskCount = static_cast<uint32_t>(_compiledVec.size());
		if (_taskCount == 0) return;

		// ジョブシステムがない ・ 止まっているときは全部同期で回す
		Thread::JobSystem* _pJobSystem = a_context.pServices ? a_context.pServices->pJobSystem : nullptr;
		if (_pJobSystem && !_pJobSystem->IsRunning()) _pJobSystem = nullptr;

		// 測るのはプロファイラがあるときと、遅いフレームを記録しているとき
		const bool _isHitchRecording = Core::Debug::IsHitchRecording();
		const bool _isMeasure = (a_pProfiler != nullptr) || _isHitchRecording;

		// このフェーズ用に作り直す : ジョブが要素のアドレスを持つため、以下で resize しない
		m_jobScratch.assign(_taskCount,nullptr);

		// 計測の置き場 : ここより下ではジョブが要素のアドレスを持つので作り直さない
		if (_isMeasure)
		{
			if (m_taskNsCapacity < _taskCount)
			{
				m_upTaskNsScratch = std::make_unique<std::atomic<int64_t>[]>(_taskCount);
				m_taskNsCapacity = _taskCount;
			}
			for (uint32_t _i = 0; _i < _taskCount; ++_i)
			{
				m_upTaskNsScratch[_i].store(0, std::memory_order_relaxed);
			}
		}

		for (uint32_t _j = 0; _j < _taskCount; ++_j)
		{
			CompileTask& _compiled = _compiledVec[_j];
			SystemTask* _pTask = _compiled.pTask;
			std::atomic<int64_t>* _pOutNs = _isMeasure ? &m_upTaskNsScratch[_j] : nullptr;

			// クエリはこのタスクの番で解決する(待つ相手の見分けとチャンク分割の両方に使う)。
			// 構造はフェーズの途中で変わらないので、前のタスクのクエリも同じ世代で揃っている
			const uint32_t _chunkNum = _pTask->prepareFunc ? _pTask->prepareFunc(*_pTask, a_context) : 0;

			// 待つ相手を、今フレームのJob*に引き直す
			// nullptr == 同期で走った・積めなかった → もう終わっているので待たない
			m_depScratch.clear();
			_compiled.isSkippedVec.assign(_compiled.waitVec.size(), 0);
			for (size_t _w = 0; _w < _compiled.waitVec.size(); ++_w)
			{
				const TaskWait& _wait = _compiled.waitVec[_w];
				Thread::Job* _pJob = m_jobScratch[_wait.index];
				if (!_pJob) continue;

				// 対象のアーキタイプが重ならなければ、同じ型でも触るエンティティは別
				if (_wait.isPerArchetype &&
					!QueryCache::IsOverlap(_compiledVec[_wait.index].pTask->query, _pTask->query))
				{
					_compiled.isSkippedVec[_w] = 1;
					continue;
				}

				m_depScratch.push_back(_pJob);
			}

			// 待つ相手が終わるまでメインスレッドで待つ : 同期で回すときに使う
			auto _waitDependencies = [&]()
				{
					// 待つ相手があるなら _pJobSystem は必ず非 null
					for (Thread::Job* _pDep : m_depScratch)
					{
						_pJobSystem->WaitFor(_pDep);
					}
					m_depScratch.clear();
				};

			// Jobタスク : 待つ相手の後続に積む メインスレッドは止まらない
			if (_pTask->exec == ETaskExec::Job && _pJobSystem)
			{
				// 通常のタスク : チャンクを分けて複数のジョブで回す
				if (_pTask->executeRangeFunc)
				{
					// チャンク一覧はメインスレッドで確定させてある(上の prepareFunc)
					if (_chunkNum == 0) continue;

					const uint32_t _batchNum = std::min(_chunkNum, _pJobSystem->GetWorkerCount());
					const uint32_t _per = (_chunkNum + _batchNum - 1) / _batchNum;

					m_batchScratch.clear();
					for (uint32_t _b = 0; _b < _chunkNum; _b += _per)
					{
						const uint32_t _e = std::min(_b + _per, _chunkNum);

						Thread::Job* _pBatch = _pJobSystem->PushFrameJob(
							[_pTask, _context = a_context, _b, _e, _pOutNs]()
							{
								ExecuteTaskRange(*_pTask, _context, _b, _e, _pOutNs);
							},
							m_depScratch			// 各バッチが待つ相手の後続になる
						);

						if (_pBatch)
						{
							m_batchScratch.push_back(_pBatch);
							continue;
						}

						// 積めなかった(ジョブシステムが止まった) :
						// フェンスは nullptr を無視して完了扱いにするので、黙って飛ばされないよう
						// 待つ相手を待ってからこの範囲をその場で回す
						_waitDependencies();
						ExecuteTaskRange(*_pTask, a_context, _b, _e, _pOutNs);
					}

					// 後ろのタスクはバッチごとではなくこのフェンスを待つようにする
					if (m_batchScratch.empty())
					{
						// 全部その場で回した
						m_jobScratch[_j] = nullptr;
					}
					else if (m_batchScratch.size() == 1)
					{
						m_jobScratch[_j] = m_batchScratch[0];
					}
					else
					{
						m_jobScratch[_j] = _pJobSystem->PushFrameJob([] {}, m_batchScratch);

						// フェンスを積めなかった : 後ろから待てないので、ここで全バッチを待ち切る
						if (!m_jobScratch[_j])
						{
							for (Thread::Job* _pBatch : m_batchScratch)
							{
								_pJobSystem->WaitFor(_pBatch);
							}
						}
					}
					continue;
				}

				// カスタムタスク : 分けられないので1ジョブで積む
				m_jobScratch[_j] = _pJobSystem->PushFrameJob(
					[_pTask, _context = a_context, _pOutNs]() { ExecuteTask(*_pTask, _context, _pOutNs); },
					m_depScratch);

				// 積めなかった → 下の同期実行へ
				if (m_jobScratch[_j]) continue;
			}

			// 同期タスク : ぶつかるジョブが終わるのを直前で待つ
			_waitDependencies();

			ExecuteTask(*_pTask, a_context, _pOutNs);
		}

		// フェーズの終わりの待ち合わせ
		// フェーズの間には物理の更新やBeginFrameの構造変更が入るので持ち越さない
		if (_pJobSystem)
		{
			for (Thread::Job* _pJob : m_jobScratch)
			{
				if (_pJob) _pJobSystem->WaitFor(_pJob);
			}
		}

		// 計測の反映はメインスレッドで全部終わってから行う。
		// 分割したタスクは各バッチの時間の合計(CPU時間)になるので、実際の経過時間より長く出る
		if (_isMeasure)
		{
			for (uint32_t _i = 0; _i < _taskCount; ++_i)
			{
				const int64_t _ns = m_upTaskNsScratch[_i].load(std::memory_order_relaxed);
				const double _ms = static_cast<double>(_ns) / 1'000'000.0;
				if (a_pProfiler) a_pProfiler->RecordTaskTime(_compiledVec[_i].pTask, _ms);

				// 遅いフレームの記録 : 短いものは送らない(毎フレーム全タスクぶん文字列を作らない)
				constexpr double HITCH_MIN_MS = 0.3;
				if (_isHitchRecording && _ms >= HITCH_MIN_MS)
				{
					Core::Debug::ReportHitch("System", _compiledVec[_i].pTask->name, _ms);
				}
			}
		}

		// Job* をフェーズの外へ持ち出さない
		m_jobScratch.clear();
	}

	namespace
	{
		//------------------------------------------------------------------------------------------
		// 推移閉包 : [a][b] = a から辿って b に届く(a の後に b が走ることが保証されている)
		// フェーズあたりのタスクは数十なので、素直に辿ってよい
		//------------------------------------------------------------------------------------------
		ScheduleAdjacency BuildReach(const ScheduleAdjacency& a_adj)
		{
			const size_t _num = a_adj.size();
			ScheduleAdjacency _reach(_num, std::vector<uint8_t>(_num, 0));

			for (size_t _from = 0; _from < _num; ++_from)
			{
				std::vector<size_t> _stack = { _from };
				while (!_stack.empty())
				{
					const size_t _cur = _stack.back();
					_stack.pop_back();

					for (size_t _to = 0; _to < _num; ++_to)
					{
						if (!a_adj[_cur][_to] || _reach[_from][_to]) continue;

						_reach[_from][_to] = 1;
						_stack.push_back(_to);
					}
				}
			}
			return _reach;
		}

		//------------------------------------------------------------------------------------------
		// Kahn法で並べる : 並べられた数を返す(足りなければ循環している)
		//
		// 同じ段のものは先入れ先出しで、最初の段は登録順。
		// 明示の順序が無いフェーズでは、以前の TopologicalSort と同じ並びになる
		//------------------------------------------------------------------------------------------
		size_t SortByGraph(
			const std::vector<SystemTask*>& a_taskVec,
			const ScheduleAdjacency& a_adj,
			std::vector<SystemTask*>& a_outVec)
		{
			const size_t _num = a_taskVec.size();

			std::vector<uint32_t> _indegree(_num, 0);
			for (size_t _from = 0; _from < _num; ++_from)
			{
				for (size_t _to = 0; _to < _num; ++_to)
				{
					if (a_adj[_from][_to]) _indegree[_to]++;
				}
			}

			std::queue<size_t> _queue;
			for (size_t _i = 0; _i < _num; ++_i)
			{
				if (_indegree[_i] == 0) _queue.push(_i);
			}

			a_outVec.clear();
			while (!_queue.empty())
			{
				const size_t _cur = _queue.front();
				_queue.pop();
				a_outVec.push_back(a_taskVec[_cur]);

				for (size_t _to = 0; _to < _num; ++_to)
				{
					if (!a_adj[_cur][_to]) continue;
					if (--_indegree[_to] == 0) _queue.push(_to);
				}
			}
			return a_outVec.size();
		}
	}

	void SystemManager::Sort()
	{
		// 変更がなければソートしない
		if (!m_isChange) return;

		// ソート対象を生ポインタへ並べ直すための入れ物 : フェーズごとに使い回す
		std::vector<SystemTask*> _taskVec = {};

		// システムフェーズごとに配列をＤＡＧグラフにする
		for (auto& [_systemPhase, _systemTaskVec] : m_systemTaskMap)
		{
			_taskVec.clear();
			_taskVec.reserve(_systemTaskVec.size());
			for (auto& _upTask : _systemTaskVec)
			{
				_taskVec.push_back(_upTask.get());
			}

			PhaseScheduleReport& _report = m_scheduleReportMap[_systemPhase];
			_report = {};

			// 明示の順序 + RAW でグラフを組んで並べる
			ScheduleAdjacency _adj = {};
			BuildPhaseGraph(_taskVec, _adj, _report);

			auto& _sortedVec = m_compileTaskMap[_systemPhase];
			const size_t _sortedCount = SortByGraph(_taskVec, _adj, _sortedVec);

			// 足りない＝依存が循環している。
			// ソート結果には循環に巻き込まれたタスクが入らないので、
			// そのまま使うとシステムが黙って実行されなくなる。
			// 何が落ちたのかを出したうえで、登録順で後ろに足して実行だけは続けさせる。
			if (_sortedCount < _taskVec.size())
			{
				ReportSortFailure(_systemPhase, _taskVec, _sortedVec, _adj);
			}

			// 並びの診断(実行には影響しない)
			BuildScheduleReport(_systemPhase, _taskVec, _sortedVec, _sortedCount, _adj, _report);

			// 循環はログを出した後で止める(After / Before で向きを決めること)
			//assert(_sortedCount == _taskVec.size() && "システムの依存が循環しています(ECS プロファイラの Systems を参照)");

			// 待つ相手の組み立て
			auto& _compiledVec = m_compiledTaskMap[_systemPhase];
			_compiledVec.clear();					// Sortはタスクが増えるたびに走りなおす必要があるので空にする
			_compiledVec.resize(_sortedVec.size());

			for (uint32_t _j = 0; _j < _sortedVec.size(); ++_j)
			{
				CompileTask& _compiled = _compiledVec[_j];
				_compiled.pTask = _sortedVec[_j];

				// 自身より前のシステムを基準として判断
				for (uint32_t _i = 0; _i < _j; ++_i)
				{
					const SystemTask* _pPrev = _sortedVec[_i];

					// 前にある同期タスクは、自分の番が来た時点ですでに終わっている想定
					if (_pPrev->exec != ETaskExec::Job) continue;

					// Jobのみ待つのかどうか判断
					if (IsConflict(*_pPrev, *_compiled.pTask))
					{
						TaskWait& _wait = _compiled.waitVec.emplace_back();
						_wait.index = _i;
						_wait.isPerArchetype = CanJudgeByArchetype(*_pPrev, *_compiled.pTask);
					}
				}
			}
		}

		// 組み直しが済んだので、次にタスクが増えるまでは何もしない。
		// 増えたときは AddSystemTask がフラグを立て直すので、
		// 次の BeginFrame で必ずここを通る
		m_isChange = false;
	}

	//----------------------------------------------------------------------------------------------
	// 並べ方の元になるグラフ
	//
	//   明示の順序 : After(相手) なら 相手 → 自分、Before(相手) なら 自分 → 相手
	//   RAW        : 読む側を書く側の後へ。ただし明示の順序(推移も含む)で
	//                読む側が先と決まっている組では使わない
	//
	// RAW を打ち消すのは、読み書きが往復する組を明示の順序で解くため。
	// 打ち消したものは診断へ残す(何を明示で上書きしたかが分かるように)
	//----------------------------------------------------------------------------------------------
	void SystemManager::BuildPhaseGraph(
		const std::vector<SystemTask*>& a_taskVec,
		ScheduleAdjacency& a_outAdj,
		PhaseScheduleReport& a_report)
	{
		const size_t _num = a_taskVec.size();

		//------------------------------------------------------------------
		// 明示の順序
		//------------------------------------------------------------------
		ScheduleAdjacency _explicit(_num, std::vector<uint8_t>(_num, 0));

		// 同じ名前のタスクが複数あれば全部に掛ける。見つからなければ false
		auto _forEachByName = [&a_taskVec](const std::string& a_name, auto&& a_func)
			{
				bool _isFound = false;
				for (size_t _k = 0; _k < a_taskVec.size(); ++_k)
				{
					if (a_taskVec[_k]->name != a_name) continue;
					a_func(_k);
					_isFound = true;
				}
				return _isFound;
			};

		for (size_t _i = 0; _i < _num; ++_i)
		{
			const SystemTask& _task = *a_taskVec[_i];

			for (const std::string& _name : _task.afterNames)
			{
				const bool _isFound = _forEachByName(_name, [&](size_t a_k) { if (a_k != _i) _explicit[a_k][_i] = 1; });
				if (!_isFound) a_report.unknownOrderVec.push_back(_task.name + " -> After(" + _name + ")");
			}
			for (const std::string& _name : _task.beforeNames)
			{
				const bool _isFound = _forEachByName(_name, [&](size_t a_k) { if (a_k != _i) _explicit[_i][a_k] = 1; });
				if (!_isFound) a_report.unknownOrderVec.push_back(_task.name + " -> Before(" + _name + ")");
			}
		}

		const ScheduleAdjacency _explicitReach = BuildReach(_explicit);

		//------------------------------------------------------------------
		// RAW : 読む側 _r を書く側 _w の後へ
		//------------------------------------------------------------------
		a_outAdj = _explicit;
		for (size_t _r = 0; _r < _num; ++_r)
		{
			for (size_t _w = 0; _w < _num; ++_w)
			{
				if (_r == _w) continue;
				if (!IsReadAfterWrite(*a_taskVec[_r], *a_taskVec[_w])) continue;

				// 明示の順序で読む側が先と決まっている
				if (_explicitReach[_r][_w])
				{
					a_report.overriddenRawVec.push_back({ a_taskVec[_r], a_taskVec[_w] });
					continue;
				}

				a_outAdj[_w][_r] = 1;
			}
		}
	}

	void SystemManager::ReportSortFailure(
		ESystemType a_phase,
		const std::vector<SystemTask*>& a_allTaskVec,
		std::vector<SystemTask*>& a_sortedTaskVec,
		const ScheduleAdjacency& a_adj)
	{
		ENGINE_LOG("[ECS] システムのトポロジカルソートに失敗しました (phase = %s)", magic_enum::enum_name(a_phase).data());
		ENGINE_LOG("[ECS] 依存が循環しています。read/write を見直すか、After / Before で向きを決めてください");

		// 並べられなかった＝循環に巻き込まれたタスク
		std::vector<size_t> _cyclicIndexVec = {};
		for (size_t _i = 0; _i < a_allTaskVec.size(); ++_i)
		{
			SystemTask* _pTask = a_allTaskVec[_i];
			if (!_pTask) continue;

			const bool _isSorted =
				std::find(a_sortedTaskVec.begin(), a_sortedTaskVec.end(), _pTask) != a_sortedTaskVec.end();
			if (!_isSorted) _cyclicIndexVec.push_back(_i);
		}

		for (size_t _i : _cyclicIndexVec)
		{
			ENGINE_LOG("[ECS]   循環: %s", a_allTaskVec[_i]->name.c_str());

			// 循環の中で、このタスクの後に並べたい相手(辺の向き)を出す
			for (size_t _k : _cyclicIndexVec)
			{
				if (_k == _i || !a_adj[_i][_k]) continue;
				ENGINE_LOG("[ECS]     -> %s", a_allTaskVec[_k]->name.c_str());
			}

			// 実行だけは続けさせる(登録順で末尾に足す)
			a_sortedTaskVec.push_back(a_allTaskVec[_i]);
		}
	}

	//----------------------------------------------------------------------------------------------
	// 並びの診断
	//
	// 「衝突はしているが、順序(明示・RAW)の経路で前後がつながっていない」組は、
	// Kahn法の段と登録順でたまたまその並びになっているだけになる。
	// 登録の位置やシステムの追加で黙って入れ替わりうるので、ここで拾って見えるようにする。
	// 見つけたら After / Before で向きを決めること。
	//
	// 拾えるのは宣言(read / write / TaskAccess)に出ているものだけ
	//----------------------------------------------------------------------------------------------
	void SystemManager::BuildScheduleReport(
		ESystemType a_phase,
		const std::vector<SystemTask*>& a_allTaskVec,
		const std::vector<SystemTask*>& a_sortedTaskVec,
		size_t a_sortedCount,
		const ScheduleAdjacency& a_adj,
		PhaseScheduleReport& a_report)
	{
		const size_t _num = a_allTaskVec.size();
		a_report.isSorted = (a_sortedCount >= _num);

		// 循環に巻き込まれ、末尾に足されたもの
		for (size_t _i = a_sortedCount; _i < a_sortedTaskVec.size(); ++_i)
		{
			a_report.cyclicTaskVec.push_back(a_sortedTaskVec[_i]);
		}

		// 順序の経路で届くか
		const ScheduleAdjacency _reach = BuildReach(a_adj);

		//------------------------------------------------------------------
		// 今の並びで前後にある衝突の組のうち、経路でつながっていないもの
		//------------------------------------------------------------------
		// 登録順の添え字(_reach の添え字)を引けるようにしておく
		std::unordered_map<const SystemTask*, size_t> _indexMap = {};
		for (size_t _i = 0; _i < _num; ++_i)
		{
			_indexMap[a_allTaskVec[_i]] = _i;
		}

		for (size_t _e = 0; _e < a_sortedTaskVec.size(); ++_e)
		{
			const SystemTask* _pEarlier = a_sortedTaskVec[_e];
			const size_t _ei = _indexMap[_pEarlier];

			for (size_t _l = _e + 1; _l < a_sortedTaskVec.size(); ++_l)
			{
				const SystemTask* _pLater = a_sortedTaskVec[_l];
				if (!IsConflict(*_pEarlier, *_pLater)) continue;

				const size_t _li = _indexMap[_pLater];

				// どちらかの向きに経路があれば前後は決まっている。
				// 両向きにある(循環の中)ものは循環側で報告しているので数えない
				if (_reach[_ei][_li] || _reach[_li][_ei]) continue;

				ScheduleAmbiguity& _amb = a_report.ambiguityVec.emplace_back();
				_amb.pEarlier = _pEarlier;
				_amb.pLater = _pLater;
				_amb.conflictSig =
					(_pEarlier->writeSig & (_pLater->readSig | _pLater->writeSig)) |
					(_pEarlier->readSig & _pLater->writeSig);
				_amb.resConflictSig =
					(_pEarlier->resWriteSig & (_pLater->resReadSig | _pLater->resWriteSig)) |
					(_pEarlier->resReadSig & _pLater->resWriteSig);
			}
		}

		// 見つからなかった順序の宣言は書き間違いなので警告する
		for (const std::string& _unknown : a_report.unknownOrderVec)
		{
			ENGINE_WARNING("[ECS] %s : 同じフェーズに無いタスクへの順序です (%s)",
				magic_enum::enum_name(a_phase).data(), _unknown.c_str());
		}

		// 件数だけログへ出す(中身は ECS プロファイラの Systems で見る)
		if (!a_report.isSorted || !a_report.ambiguityVec.empty())
		{
			ENGINE_LOG("[ECS] %s : ソート%s / 循環 %zu 件 / 前後が決まっていない衝突 %zu 組",
				magic_enum::enum_name(a_phase).data(),
				a_report.isSorted ? "成功" : "失敗",
				a_report.cyclicTaskVec.size(),
				a_report.ambiguityVec.size());
		}
	}

	SystemTask* SystemManager::AddSystemTask(ESystemType a_systemType, const SystemTask & a_systemTask, const std::string& a_taskName)
	{
		m_isChange = true;

		auto _upTask = std::make_unique<SystemTask>(a_systemTask);

		// 名前が入っていなければ登録時の名前を使う
		if (_upTask->name.empty())
		{
			_upTask->name = a_taskName;
		}

		// 実体は個別に確保しているので、後からタスクが増えてもアドレスは動かない
		SystemTask* _pTask = _upTask.get();
		m_systemTaskMap[a_systemType].push_back(std::move(_upTask));
		return _pTask;
	}

	const std::unordered_map<ESystemType, std::vector<SystemTask*>>& SystemManager::GetCompileTaskMap() const
	{
		return m_compileTaskMap;
	}
}
