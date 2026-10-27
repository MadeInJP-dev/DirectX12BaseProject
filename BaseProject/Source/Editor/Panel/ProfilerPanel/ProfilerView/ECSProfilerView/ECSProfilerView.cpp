#include "ECSProfilerView.h"

#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/ECS/World/World.h"
#include "Engine/ECS/World/Profile/ECSWorldProfiler.h"

namespace Editor
{
	namespace
	{
		// バイト数を読みやすい単位にする
		std::string FormatBytes(size_t a_bytes)
		{
			char _buf[64] = {};
			if (a_bytes >= 1024 * 1024)
			{
				snprintf(_buf, sizeof(_buf), "%.2f MB", static_cast<double>(a_bytes) / (1024.0 * 1024.0));
			}
			else if (a_bytes >= 1024)
			{
				snprintf(_buf, sizeof(_buf), "%.1f KB", static_cast<double>(a_bytes) / 1024.0);
			}
			else
			{
				snprintf(_buf, sizeof(_buf), "%zu B", a_bytes);
			}
			return _buf;
		}

		// 名前を区切って1行にする
		std::string JoinNames(const std::vector<std::string>& a_names, const char* a_separator = ", ")
		{
			std::string _out = {};
			for (size_t _i = 0; _i < a_names.size(); ++_i)
			{
				if (_i > 0) _out += a_separator;
				_out += a_names[_i];
			}
			return _out;
		}

		// アーキタイプのコンポーネント名を1行にする
		std::string ArchetypeLabel(const ECS::ECSArchetypeProfile& a_archetype)
		{
			if (a_archetype.components.empty()) return "(no component)";

			std::string _out = {};
			for (size_t _i = 0; _i < a_archetype.components.size(); ++_i)
			{
				if (_i > 0) _out += ", ";
				_out += a_archetype.components[_i].name;
			}
			return _out;
		}

		// 割合(0 除算を避ける)
		float Ratio(double a_value, double a_max)
		{
			return (a_max > 0.0) ? static_cast<float>(a_value / a_max) : 0.0f;
		}

		// 表のフラグ
		constexpr ImGuiTableFlags TABLE_FLAGS =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
	}

	//======================================================================================
	// 表示
	//======================================================================================
	void ECSProfilerView::Draw(EditorContext& a_editContext)
	{
		Scene::SceneManager* _pSceneManager = a_editContext.pServices ? a_editContext.pServices->pSceneManager : nullptr;
		ECS::World* _pWorld = _pSceneManager ? _pSceneManager->RefWorld() : nullptr;
		if (!_pWorld || !_pWorld->IsInit())
		{
			Engine::EditorField::HelpText("World is not available.");
			return;
		}

		// 見るときにだけ付ける。
		// シーンが変わるとワールドごと作り直されるので、そのたびにここで付け直される
		_pWorld->EnableProfiler();
		ECS::ECSWorldProfiler* _pProfiler = _pWorld->RefProfiler();
		if (!_pProfiler) return;

		// 操作
		Engine::EditorField::Field("Pause", m_isPaused);
		if (ImGui::Button("Reset Timings"))
		{
			_pProfiler->ResetTaskTimings();
		}

		// 止めていても、付けたばかりで何も取れていなければ1回は取る
		if (!m_isPaused || _pProfiler->GetSnapshot().captureCount == 0)
		{
			_pProfiler->Capture();
		}
		const ECS::ECSWorldSnapshot& _snapshot = _pProfiler->GetSnapshot();

		Engine::EditorField::Line();

		if (ImGui::BeginTabBar("ECSProfilerTab"))
		{
			if (ImGui::BeginTabItem("Overview"))
			{
				DrawOverview(_snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Archetypes"))
			{
				DrawArchetypes(_snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Components"))
			{
				DrawComponents(_snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Systems"))
			{
				DrawSystems(_snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Resources"))
			{
				DrawResources(_snapshot);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}

	//======================================================================================
	// 概要
	//======================================================================================
	void ECSProfilerView::DrawOverview(const ECS::ECSWorldSnapshot& a_snapshot)
	{
		//------------------------------------------------------------------
		// エンティティ
		//------------------------------------------------------------------
		Engine::EditorField::Header("Entity");
		Engine::EditorField::Value("Alive", "%u", a_snapshot.entity.aliveCount);
		Engine::EditorField::Value("Slots", "%zu", a_snapshot.entity.slotCount);
		Engine::EditorField::Value("Recyclable", "%zu", a_snapshot.entity.recycleCount);

		//------------------------------------------------------------------
		// アーキタイプ
		//------------------------------------------------------------------
		size_t _emptyArchetypeCount = 0;
		size_t _chunkCount = 0;
		size_t _freeChunkCount = 0;
		for (const auto& _archetype : a_snapshot.archetypes)
		{
			if (_archetype.entityCount == 0) _emptyArchetypeCount++;
			_chunkCount += _archetype.chunks.size();
			for (const auto& _chunk : _archetype.chunks)
			{
				if (_chunk.isFreeChunk) _freeChunkCount++;
			}
		}

		Engine::EditorField::Header("Archetype");
		Engine::EditorField::Value("Archetypes", "%zu (empty %zu)", a_snapshot.archetypes.size(), _emptyArchetypeCount);
		Engine::EditorField::Value("Chunks", "%zu (kept empty %zu)", _chunkCount, _freeChunkCount);
		Engine::EditorField::Value("Generation", "%llu", static_cast<unsigned long long>(a_snapshot.archetypeGeneration));

		//------------------------------------------------------------------
		// チャンクのメモリ
		//------------------------------------------------------------------
		const ECS::ECSChunkMemoryProfile& _memory = a_snapshot.memory;

		Engine::EditorField::Header("Chunk Memory");
		Engine::EditorField::Value("Blocks", "%zu (x %zu chunks, %s / chunk)", _memory.blockCount, _memory.blockChunkNum, FormatBytes(_memory.chunkBytes).c_str());
		Engine::EditorField::Value("Reserved", "%s", FormatBytes(_memory.reservedBytes).c_str());

		// 貸し出し中のチャンク / 確保済みのチャンク
		{
			char _overlay[64] = {};
			snprintf(_overlay, sizeof(_overlay), "%zu / %zu chunks", _memory.usedChunkCount, _memory.totalChunkCount);
			Engine::EditorField::ProgressBar("Chunk Use", Ratio(static_cast<double>(_memory.usedChunkCount), static_cast<double>(_memory.totalChunkCount)), _overlay);
		}

		// 貸し出し中のチャンクのうち、生きているエンティティが使っている割合
		{
			const size_t _usedBytes = _memory.usedChunkCount * _memory.chunkBytes;
			char _overlay[64] = {};
			snprintf(_overlay, sizeof(_overlay), "%s / %s", FormatBytes(_memory.liveBytes).c_str(), FormatBytes(_usedBytes).c_str());
			Engine::EditorField::ProgressBar("Fill", Ratio(static_cast<double>(_memory.liveBytes), static_cast<double>(_usedBytes)), _overlay);
		}

		//------------------------------------------------------------------
		// 構造変更
		//------------------------------------------------------------------
		const ECS::ECSStructuralChangeProfile& _structural = a_snapshot.structural;

		Engine::EditorField::Header("Structural Change");
		Engine::EditorField::HelpText("Applied since last capture / Pending now");
		Engine::EditorField::Value("Create", "%zu / %zu", _structural.created, _structural.pendingCreate);
		Engine::EditorField::Value("Change", "%zu / %zu", _structural.changed, _structural.pendingChange);
		Engine::EditorField::Value("Remove", "%zu / %zu", _structural.removed, _structural.pendingRemove);
		Engine::EditorField::Value("Refresh", "- / %zu", _structural.pendingRefresh);

		//------------------------------------------------------------------
		// システム・リソース
		//------------------------------------------------------------------
		double _totalMs = 0.0;
		for (const auto& _task : a_snapshot.systemTasks)
		{
			_totalMs += _task.lastMs;
		}

		size_t _jobTaskCount = 0;
		size_t _waitCount = 0;
		size_t _skippedWaitCount = 0;
		for (const auto& _task : a_snapshot.systemTasks)
		{
			if (_task.isJob) _jobTaskCount++;
			_waitCount += _task.waitNames.size();
			_skippedWaitCount += _task.skippedWaitNames.size();
		}

		Engine::EditorField::Header("System / Resource");
		Engine::EditorField::Value("Tasks", "%zu (job %zu / last total %.3f ms)", a_snapshot.systemTasks.size(), _jobTaskCount, _totalMs);
		// アーキタイプが重ならず、待たずに済んだ待ち合わせ(直近の実行)
		Engine::EditorField::Value("Job waits", "%zu (skipped %zu)", _waitCount, _skippedWaitCount);
		Engine::EditorField::Value("Resources", "%zu", a_snapshot.resources.size());

		//------------------------------------------------------------------
		// 並びの診断
		//------------------------------------------------------------------
		size_t _failedPhaseCount = 0;
		size_t _ambiguityCount = 0;
		for (const auto& _schedule : a_snapshot.schedules)
		{
			if (!_schedule.isSorted) _failedPhaseCount++;
			_ambiguityCount += _schedule.ambiguities.size();
		}

		Engine::EditorField::Header("Schedule");
		if (_failedPhaseCount > 0)
		{
			Engine::EditorField::ErrorText("Sort failed in %zu phase(s) (dependency cycle). See Systems tab.", _failedPhaseCount);
		}
		else
		{
			Engine::EditorField::HelpText("All phases sorted.");
		}
		if (_ambiguityCount > 0)
		{
			Engine::EditorField::WarningText("%zu conflicting pair(s) are ordered only by registration order.", _ambiguityCount);
		}
	}

	//======================================================================================
	// アーキタイプ
	//======================================================================================
	void ECSProfilerView::DrawArchetypes(const ECS::ECSWorldSnapshot& a_snapshot)
	{
		m_archetypeFilter.Draw("Filter (component)", 200.0f);
		Engine::EditorField::SameLine();
		Engine::EditorField::Field("Hide Empty", m_isHideEmptyArchetype);
		Engine::EditorField::Field("Sort by Entities", m_isSortArchetypeByEntity);
		Engine::EditorField::Line();

		// 表示順
		std::vector<const ECS::ECSArchetypeProfile*> _archetypeVec = {};
		_archetypeVec.reserve(a_snapshot.archetypes.size());
		for (const auto& _archetype : a_snapshot.archetypes)
		{
			if (m_isHideEmptyArchetype && _archetype.entityCount == 0) continue;
			_archetypeVec.push_back(&_archetype);
		}
		if (m_isSortArchetypeByEntity)
		{
			std::stable_sort(_archetypeVec.begin(), _archetypeVec.end(),
				[](const ECS::ECSArchetypeProfile* a_l, const ECS::ECSArchetypeProfile* a_r) { return a_l->entityCount > a_r->entityCount; });
		}

		if (!ImGui::BeginChild("ArchetypeList"))
		{
			ImGui::EndChild();
			return;
		}

		for (const ECS::ECSArchetypeProfile* _pArchetype : _archetypeVec)
		{
			const ECS::ECSArchetypeProfile& _archetype = *_pArchetype;
			const std::string _label = ArchetypeLabel(_archetype);

			if (!m_archetypeFilter.PassFilter(_label.c_str())) continue;

			// 見出し : 番号 / エンティティ数 / チャンク数 / コンポーネント
			// 番号を ID にして、並び替えても開閉状態が付いて回るようにする
			ImGui::PushID(static_cast<int>(_archetype.index));
			const bool _isOpen = ImGui::TreeNodeEx("##Archetype", ImGuiTreeNodeFlags_SpanAvailWidth,
				"#%zu  [%u entities / %zu chunks]  %s",
				_archetype.index, _archetype.entityCount, _archetype.chunks.size(), _label.c_str());

			if (_isOpen)
			{
				// 容量とレイアウト
				Engine::EditorField::Text("Capacity : %u / chunk   Stride : %zu B / entity   Max Align : %zu", _archetype.chunkCapacity, _archetype.entityStride, _archetype.maxAlign);

				const size_t _chunkBytes = a_snapshot.memory.chunkBytes;
				{
					char _overlay[64] = {};
					snprintf(_overlay, sizeof(_overlay), "Layout %s / %s",
						FormatBytes(_archetype.layoutBytes).c_str(), FormatBytes(_chunkBytes).c_str());
					Engine::EditorField::ProgressBar("Layout", Ratio(static_cast<double>(_archetype.layoutBytes), static_cast<double>(_chunkBytes)), _overlay);
				}

				// コンポーネント配列の配置(チャンク内の並び順)
				if (ImGui::TreeNodeEx("Layout", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (ImGui::BeginTable("LayoutTable", 6, TABLE_FLAGS))
					{
						ImGui::TableSetupColumn("Component");
						ImGui::TableSetupColumn("Offset");
						ImGui::TableSetupColumn("Stride");
						ImGui::TableSetupColumn("Size");
						ImGui::TableSetupColumn("Align");
						ImGui::TableSetupColumn("Array");
						ImGui::TableHeadersRow();

						// 先頭はエンティティ配列
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("(Entity)");
						ImGui::TableSetColumnIndex(1); ImGui::Text("0");
						ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", sizeof(ECS::Entity));
						ImGui::TableSetColumnIndex(3); ImGui::Text("%zu", sizeof(ECS::Entity));
						ImGui::TableSetColumnIndex(4); ImGui::Text("%zu", alignof(ECS::Entity));
						ImGui::TableSetColumnIndex(5); ImGui::Text("%s", FormatBytes(sizeof(ECS::Entity) * _archetype.chunkCapacity).c_str());

						for (const auto& _comp : _archetype.components)
						{
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0); ImGui::Text("%s", _comp.name.c_str());
							ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", _comp.offset);
							ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", _comp.stride);
							ImGui::TableSetColumnIndex(3); ImGui::Text("%zu", _comp.size);
							ImGui::TableSetColumnIndex(4); ImGui::Text("%zu", _comp.align);
							ImGui::TableSetColumnIndex(5); ImGui::Text("%s", FormatBytes(_comp.stride * _archetype.chunkCapacity).c_str());
						}
						ImGui::EndTable();
					}
					ImGui::TreePop();
				}

				// チャンクごとの埋まり具合
				if (ImGui::TreeNodeEx("Chunks", ImGuiTreeNodeFlags_DefaultOpen, "Chunks (%zu)", _archetype.chunks.size()))
				{
					for (size_t _i = 0; _i < _archetype.chunks.size(); ++_i)
					{
						const ECS::ECSChunkProfile& _chunk = _archetype.chunks[_i];

						char _overlay[64] = {};
						snprintf(_overlay, sizeof(_overlay), "%u / %u%s",
							_chunk.count, _archetype.chunkCapacity, _chunk.isFreeChunk ? " (kept empty)" : "");

						Engine::EditorField::Text("[%zu]", _i);
						Engine::EditorField::SameLine();
						ImGui::ProgressBar(Ratio(_chunk.count, _archetype.chunkCapacity), ImVec2(-1.0f, 0.0f), _overlay);
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("Address : 0x%016llx", static_cast<unsigned long long>(_chunk.address));
						}
					}
					ImGui::TreePop();
				}

				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		ImGui::EndChild();
	}

	//======================================================================================
	// コンポーネント
	//======================================================================================
	void ECSProfilerView::DrawComponents(const ECS::ECSWorldSnapshot& a_snapshot)
	{
		Engine::EditorField::Field("Hide Unused", m_isHideUnusedComponent);
		Engine::EditorField::Line();

		if (!ImGui::BeginTable("ComponentTable", 6, TABLE_FLAGS | ImGuiTableFlags_ScrollY)) return;

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("ID");
		ImGui::TableSetupColumn("Component");
		ImGui::TableSetupColumn("Size");
		ImGui::TableSetupColumn("Align");
		ImGui::TableSetupColumn("Archetypes");
		ImGui::TableSetupColumn("Entities");
		ImGui::TableHeadersRow();

		for (const auto& _comp : a_snapshot.components)
		{
			if (m_isHideUnusedComponent && _comp.archetypeCount == 0) continue;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::Text("%u", static_cast<uint32_t>(_comp.typeID));
			ImGui::TableSetColumnIndex(1); ImGui::Text("%s", _comp.name.c_str());
			ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", _comp.size);
			ImGui::TableSetColumnIndex(3); ImGui::Text("%zu", _comp.align);
			ImGui::TableSetColumnIndex(4); ImGui::Text("%u", _comp.archetypeCount);
			ImGui::TableSetColumnIndex(5); ImGui::Text("%u", _comp.entityCount);
		}
		ImGui::EndTable();
	}

	//======================================================================================
	// システム
	//======================================================================================
	void ECSProfilerView::DrawSystems(const ECS::ECSWorldSnapshot& a_snapshot)
	{
		Engine::EditorField::HelpText("Timings are measured while the profiler is attached.");

		if (!ImGui::BeginChild("SystemList"))
		{
			ImGui::EndChild();
			return;
		}

		// タスクはフェーズ順 → 実行順に並んでいるので、フェーズの切れ目で区切る
		const auto& _taskVec = a_snapshot.systemTasks;
		size_t _begin = 0;
		while (_begin < _taskVec.size())
		{
			const ECS::ESystemType _phase = _taskVec[_begin].phase;

			size_t _end = _begin;
			double _phaseMs = 0.0;
			while (_end < _taskVec.size() && _taskVec[_end].phase == _phase)
			{
				_phaseMs += _taskVec[_end].lastMs;
				++_end;
			}

			// このフェーズの並びの診断
			const ECS::ECSPhaseScheduleProfile* _pSchedule = nullptr;
			for (const auto& _schedule : a_snapshot.schedules)
			{
				if (_schedule.phase == _phase) { _pSchedule = &_schedule; break; }
			}

			// 見出しに状態を出す(閉じたままでも気付けるように)
			char _status[64] = {};
			if (_pSchedule && !_pSchedule->isSorted)
			{
				snprintf(_status, sizeof(_status), "  SORT FAILED");
			}
			else if (_pSchedule && !_pSchedule->ambiguities.empty())
			{
				snprintf(_status, sizeof(_status), "  ambiguous %zu", _pSchedule->ambiguities.size());
			}

			ImGui::PushID(static_cast<int>(_phase));
			const bool _isOpen = ImGui::TreeNodeEx("##Phase", ImGuiTreeNodeFlags_SpanAvailWidth,
				"%s  [%zu tasks / %.3f ms]%s", magic_enum::enum_name(_phase).data(), _end - _begin, _phaseMs, _status);

			if (_isOpen)
			{
				if (_pSchedule && !_pSchedule->isSorted)
				{
					Engine::EditorField::ErrorText("Sort failed: dependency cycle. Tasks below marked (cycle) run in registration order.");
				}

				if (ImGui::BeginTable("TaskTable", 10, TABLE_FLAGS))
				{
					ImGui::TableSetupColumn("#");
					ImGui::TableSetupColumn("Task");
					ImGui::TableSetupColumn("Exec");
					ImGui::TableSetupColumn("Waits");
					ImGui::TableSetupColumn("Last(ms)");
					ImGui::TableSetupColumn("Avg(ms)");
					ImGui::TableSetupColumn("Max(ms)");
					ImGui::TableSetupColumn("Chunks");
					ImGui::TableSetupColumn("Entities");
					ImGui::TableSetupColumn("R / W");
					ImGui::TableHeadersRow();

					for (size_t _i = _begin; _i < _end; ++_i)
					{
						const ECS::ECSSystemTaskProfile& _task = _taskVec[_i];

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::Text("%u", _task.order);

						// 循環に巻き込まれたもの / 前後が依存で決まっていないものは目立たせる
						ImGui::TableSetColumnIndex(1);
						if (_task.isCyclic)
						{
							Engine::EditorField::ErrorText("%s (cycle)", _task.name.c_str());
						}
						else if (_task.ambiguityCount > 0)
						{
							Engine::EditorField::WarningText("%s (ambiguous %u)", _task.name.c_str(), _task.ambiguityCount);
						}
						else
						{
							ImGui::Text("%s", _task.name.c_str());
						}

						// 実行のされ方 : 待つ相手は数だけ出して中身はツールチップ
						ImGui::TableSetColumnIndex(2);
						if (_task.isJob) ImGui::Text("Job"); else ImGui::TextDisabled("Main");
						ImGui::TableSetColumnIndex(3);
						if (_task.waitNames.empty())
						{
							ImGui::TextDisabled("-");
						}
						else
						{
							// アーキタイプが重ならず待たなかったものは差し引いて出す
							if (_task.skippedWaitNames.empty())
							{
								ImGui::Text("%zu", _task.waitNames.size());
							}
							else
							{
								ImGui::Text("%zu (-%zu)", _task.waitNames.size(), _task.skippedWaitNames.size());
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								Engine::EditorField::Value("Wait for", "%s", JoinNames(_task.waitNames).c_str());
								if (!_task.perArchetypeWaitNames.empty())
								{
									Engine::EditorField::Value("Per archetype", "%s", JoinNames(_task.perArchetypeWaitNames).c_str());
								}
								if (!_task.skippedWaitNames.empty())
								{
									Engine::EditorField::Value("Skipped (no shared archetype)", "%s", JoinNames(_task.skippedWaitNames).c_str());
								}
								ImGui::EndTooltip();
							}
						}

						// 1度も回っていないものは時間を出しても意味が無い
						ImGui::TableSetColumnIndex(4);
						if (_task.callCount > 0) ImGui::Text("%.3f", _task.lastMs); else ImGui::TextDisabled("-");
						ImGui::TableSetColumnIndex(5);
						if (_task.callCount > 0) ImGui::Text("%.3f", _task.averageMs); else ImGui::TextDisabled("-");
						ImGui::TableSetColumnIndex(6);
						if (_task.callCount > 0) ImGui::Text("%.3f", _task.maxMs); else ImGui::TextDisabled("-");

						// クエリを持たないもの(カスタムタスク・未実行)は伏せる
						ImGui::TableSetColumnIndex(7);
						if (_task.hasQuery) ImGui::Text("%zu", _task.matchedChunkCount); else ImGui::TextDisabled("-");
						ImGui::TableSetColumnIndex(8);
						if (!_task.hasQuery)			ImGui::TextDisabled("-");
						else if (_task.isQueryStale)	ImGui::TextDisabled("(stale)");
						else							ImGui::Text("%u", _task.matchedEntityCount);

						// 依存 : 数だけ出して中身はツールチップ
						ImGui::TableSetColumnIndex(9);
						ImGui::Text("%zu / %zu", _task.readNames.size() + _task.readResourceNames.size(),
							_task.writeNames.size() + _task.writeResourceNames.size());
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							Engine::EditorField::Value("Read", "%s", _task.readNames.empty() ? "-" : JoinNames(_task.readNames).c_str());
							Engine::EditorField::Value("Write", "%s", _task.writeNames.empty() ? "-" : JoinNames(_task.writeNames).c_str());
							Engine::EditorField::Value("Read Res", "%s", _task.readResourceNames.empty() ? "-" : JoinNames(_task.readResourceNames).c_str());
							Engine::EditorField::Value("Write Res", "%s", _task.writeResourceNames.empty() ? "-" : JoinNames(_task.writeResourceNames).c_str());
							Engine::EditorField::Value("After", "%s", _task.afterNames.empty() ? "-" : JoinNames(_task.afterNames).c_str());
							Engine::EditorField::Value("Before", "%s", _task.beforeNames.empty() ? "-" : JoinNames(_task.beforeNames).c_str());
							ImGui::EndTooltip();
						}
					}
					ImGui::EndTable();
				}

				//----------------------------------------------------------
				// 前後が依存で決まっていない衝突
				//
				// 衝突しているのに RAW の経路でつながっていない組。
				// 今の並びは Kahn法の段と登録順で決まっているだけなので、
				// 登録位置やシステムの追加で黙って入れ替わりうる
				//----------------------------------------------------------
				//----------------------------------------------------------
				// 見つからなかった順序の宣言(名前の書き間違い)
				//----------------------------------------------------------
				if (_pSchedule)
				{
					for (const std::string& _unknown : _pSchedule->unknownOrders)
					{
						Engine::EditorField::ErrorText("Unknown order: %s", _unknown.c_str());
					}
				}

				//----------------------------------------------------------
				// 明示の順序で打ち消した RAW
				//
				// 読み書きが往復する組を、After / Before で向きを決めたもの。
				// 「読む側 <- 書く側」の読む側が先に走る(書かれる前の値を読む)
				//----------------------------------------------------------
				if (_pSchedule && !_pSchedule->overriddenRaws.empty())
				{
					if (ImGui::TreeNodeEx("##Overridden", ImGuiTreeNodeFlags_SpanAvailWidth,
						"Overridden read-after-write (%zu)", _pSchedule->overriddenRaws.size()))
					{
						Engine::EditorField::HelpText("Reader runs first because of an explicit After / Before (reads the value before the writer).");
						for (const std::string& _text : _pSchedule->overriddenRaws)
						{
							ImGui::BulletText("%s", _text.c_str());
						}
						ImGui::TreePop();
					}
				}

				if (_pSchedule && !_pSchedule->ambiguities.empty())
				{
					if (ImGui::TreeNodeEx("##Ambiguity", ImGuiTreeNodeFlags_SpanAvailWidth,
						"Ambiguous order (%zu)", _pSchedule->ambiguities.size()))
					{
						Engine::EditorField::HelpText("Conflicting pairs ordered only by level / registration order (not by a read-after-write path).");

						if (ImGui::BeginTable("AmbiguityTable", 3, TABLE_FLAGS))
						{
							ImGui::TableSetupColumn("Runs first");
							ImGui::TableSetupColumn("Runs later");
							ImGui::TableSetupColumn("Conflict");
							ImGui::TableHeadersRow();

							for (const auto& _amb : _pSchedule->ambiguities)
							{
								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0); ImGui::Text("%s", _amb.earlierName.c_str());
								ImGui::TableSetColumnIndex(1); ImGui::Text("%s", _amb.laterName.c_str());
								ImGui::TableSetColumnIndex(2); ImGui::TextWrapped("%s", JoinNames(_amb.conflictNames).c_str());
							}
							ImGui::EndTable();
						}
						ImGui::TreePop();
					}
				}

				ImGui::TreePop();
			}
			ImGui::PopID();

			_begin = _end;
		}

		ImGui::EndChild();
	}

	//======================================================================================
	// リソース
	//======================================================================================
	void ECSProfilerView::DrawResources(const ECS::ECSWorldSnapshot& a_snapshot)
	{
		Engine::EditorField::HelpText("Size is sizeof(T). Heap memory owned by the resource is not included.");

		if (!ImGui::BeginTable("ResourceTable", 3, TABLE_FLAGS)) return;

		ImGui::TableSetupColumn("ID");
		ImGui::TableSetupColumn("Type");
		ImGui::TableSetupColumn("Size");
		ImGui::TableHeadersRow();

		for (const auto& _resource : a_snapshot.resources)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::Text("%u", _resource.id);
			ImGui::TableSetColumnIndex(1); ImGui::Text("%s", _resource.name.c_str());
			ImGui::TableSetColumnIndex(2); ImGui::Text("%s", FormatBytes(_resource.size).c_str());
		}
		ImGui::EndTable();
	}
}
