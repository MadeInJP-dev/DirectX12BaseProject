#include "ParticleProfilerView.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Particle/ParticleBufferManager.h"
#include "Engine/Graphics/Particle/GPU/EmitterSlotPool/EmitterSlotPool.h"
#include "Engine/Graphics/Particle/GPU/GPUParticlePool/GPUParticlePool.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

namespace Editor
{
	//======================================================================================
	// GPUパーティクル
	//
	// 発生源の席 : 使用中・返却待ちが増え続けていないか(シーンを読み直しても一定か)を見る。
	//              増え続けるなら、どこかで席を返し忘れている。
	// プール     : アセットごとの容量と、準備が済んでいるか、命令があふれたことがあるか
	//======================================================================================
	void ParticleProfilerView::Draw(EditorContext& /*a_editContext*/)
	{
		auto* _pGE = MainEngine::Instance().RefGraphicsEngine();
		auto* _pPM = _pGE ? _pGE->RefParticleManager() : nullptr;
		if (!_pPM)
		{
			Engine::EditorField::HelpText("(パーティクルマネージャーがありません)");
			return;
		}

		//----------------------------------------------------------------------------------
		// 発生源の席
		//----------------------------------------------------------------------------------
		if (const auto* _pSlots = _pPM->GetEmitterSlotPool())
		{
			Engine::EditorField::Text("Emitter Slots");

			// 席 0(単位行列)は予約で常に1つ使っているので、持ち主の数からは外す
			const uint32_t _live = _pSlots->GetLiveCount();
			const uint32_t _pending = _pSlots->GetPendingCount();
			const uint32_t _owned = (_live > _pending + 1) ? (_live - _pending - 1) : 0;

			Engine::EditorField::Value("In Use", "%u", _owned);
			Engine::EditorField::Tooltip("持ち主のエフェクトがいる席(席0を除く)");

			Engine::EditorField::Value("Pending Return", "%u", _pending);
			Engine::EditorField::Tooltip("持ち主は消えたが、出した粒が消えきるまで待っている席");

			Engine::EditorField::Value("Transfer Range", "%u", _pSlots->GetUsedCount());
			Engine::EditorField::Tooltip("毎フレームGPUへ送っている席の数(配ったことのある最大の席番号 + 1)");

			Engine::EditorField::Value("Capacity CPU / GPU", "%u / %u  (block %u)",
				_pSlots->GetCPUCapacity(), _pSlots->GetGPUCapacity(), _pSlots->GetBlockSize());
			Engine::EditorField::Tooltip("足りなくなると block 単位で伸びる。GPU側はそのとき作り直す");

			Engine::EditorField::Value("GPU Size", "%.1f KB",
				static_cast<double>(_pSlots->GetGPUCapacity()) * sizeof(Graphics::Particle::EmitterTransform) / 1024.0);
		}
		else
		{
			Engine::EditorField::HelpText("(発生源の席がまだ作られていません)");
		}

		Engine::EditorField::Line();

		//----------------------------------------------------------------------------------
		// プール一覧
		//
		// マップの並びは毎回変わりうるので、名前で並べてから出す
		//----------------------------------------------------------------------------------
		struct PoolRow
		{
			std::string name;
			UINT capacity = 0;
			size_t requests = 0;
			bool isReady = false;
			bool isAwake = false;
			bool isOverflowed = false;
			bool isGrowPending = false;		// 伸ばす予定がある(次のシミュレーションで伸びる)
			bool isHitLimit = false;		// 上限に届いたことがある
			uint64_t estimated = 0;			// 直近の最大寿命の間に出した数(生きている数の上限)
			uint32_t growCount = 0;			// 伸ばした回数
			int sortOrder = 0;
			double sinceEmit = -1.0;
			bool isLocal = false;
			bool isAlphaBlend = false;
		};

		const auto* _pRM = _pGE->RefResourceManager();

		std::vector<PoolRow> _rows;
		_rows.reserve(_pPM->GetPoolMap().size());

		size_t _totalCapacity = 0;
		size_t _readyCount = 0;
		size_t _awakeCount = 0;		// 更新と描画を回しているプール
		size_t _awakeCapacity = 0;	// そのぶんの容量(更新と描画が実際に走る粒の数)
		for (const auto& [_handle, _upPool] : _pPM->GetPoolMap())
		{
			PoolRow _row = {};

			const auto* _pAsset = _pRM ? _pRM->Get(_handle) : nullptr;
			_row.name = _pAsset ? _pAsset->GetName() : "(不明)";
			_row.capacity = _upPool ? _upPool->GetMaxCapacity() : 0;
			_row.requests = _pPM->GetRequests(_handle).size();
			_row.isReady = _pPM->IsReady(_handle);
			_row.isAwake = _pPM->IsAwake(_handle);
			_row.isOverflowed = _pPM->HasOverflowed(_handle);
			_row.isGrowPending = _pPM->HasGrowTarget(_handle);
			_row.isHitLimit = _pPM->HasHitHardLimit(_handle);
			_row.estimated = _pPM->GetEstimatedLive(_handle);
			_row.growCount = _pPM->GetGrowCount(_handle);
			_row.sortOrder = _pAsset ? _pAsset->GetSortOrder() : 0;
			_row.sinceEmit = _pPM->GetSecondsSinceLastEmit(_handle);
			_row.isLocal = _pAsset && _pAsset->IsLocalSpace();
			_row.isAlphaBlend = _pAsset && (_pAsset->GetBlendMode() == Graphics::Particle::EParticleBlendMode::AlphaBlend);

			_totalCapacity += _row.capacity;
			if (_row.isReady) ++_readyCount;
			if (_row.isReady && _row.isAwake)
			{
				++_awakeCount;
				_awakeCapacity += _row.capacity;
			}

			_rows.push_back(std::move(_row));
		}

		std::sort(_rows.begin(), _rows.end(),
			[](const PoolRow& a_l, const PoolRow& a_r) { return a_l.name < a_r.name; });

		// 粒本体 + デッドリスト。命令バッファとカウンターは小さいので数えない
		constexpr size_t BYTES_PER_PARTICLE = sizeof(Graphics::Particle::ParticleData) + sizeof(uint32_t);

		Engine::EditorField::Value("Pools", "%u  (Ready %u / Loading %u)",
			static_cast<unsigned>(_rows.size()),
			static_cast<unsigned>(_readyCount),
			static_cast<unsigned>(_rows.size() - _readyCount));
		Engine::EditorField::Value("Total Capacity", "%u particles  (%.1f MB)",
			static_cast<unsigned>(_totalCapacity),
			static_cast<double>(_totalCapacity * BYTES_PER_PARTICLE) / (1024.0 * 1024.0));
		Engine::EditorField::Value("Awake", "%u pools  (%u particles)",
			static_cast<unsigned>(_awakeCount),
			static_cast<unsigned>(_awakeCapacity));
		Engine::EditorField::Tooltip(
			"更新と描画を回しているプール。更新はまだ粒の数ではなく、この容量ぶん走っている"
			"(描画は生きている粒の数ぶんだけ)。最後に出してから最大寿命が経ったプールは眠って(飛ばされて)いる");

		// 発生命令のバッファ(全プール共通の1本)。足りなくなると2のべき乗で伸びる
		Engine::EditorField::Value("Emit Buffer", "%u / %u requests  (max %u)",
			static_cast<unsigned>(_pPM->GetFrameEmitCount()),
			static_cast<unsigned>(_pPM->GetEmitBufferCapacity()),
			static_cast<unsigned>(Graphics::Particle::EMIT_BUFFER_MAX_CAPACITY));
		Engine::EditorField::Tooltip("このフレームに送った発生命令の数 / 命令バッファの容量。全プール共通の1本");

		if (_rows.empty()) return;

		constexpr ImGuiTableFlags TABLE_FLAGS =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;

		if (ImGui::BeginTable("ParticlePoolTable", 10, TABLE_FLAGS))
		{
			ImGui::TableSetupColumn("Asset");
			ImGui::TableSetupColumn("Capacity");
			ImGui::TableSetupColumn("Estimated");
			ImGui::TableSetupColumn("Grows");
			ImGui::TableSetupColumn("Space");
			ImGui::TableSetupColumn("Blend");
			ImGui::TableSetupColumn("Order");
			ImGui::TableSetupColumn("Requests");
			ImGui::TableSetupColumn("Last Emit");
			ImGui::TableSetupColumn("State");
			ImGui::TableHeadersRow();

			for (const auto& _row : _rows)
			{
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s", _row.name.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", _row.capacity);

				// 生きている数の上限の見積もり(直近の最大寿命の間に出した数)。容量に近いほど伸びやすい
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%llu", static_cast<unsigned long long>(_row.estimated));

				// 伸ばした回数。上限に届いたことがあれば色を変える
				ImGui::TableSetColumnIndex(3);
				if (_row.isHitLimit)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%u (limit)", _row.growCount);
				}
				else
				{
					ImGui::Text("%u", _row.growCount);
				}

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%s", _row.isLocal ? "Local" : "World");

				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%s", _row.isAlphaBlend ? "Alpha" : "Add");

				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%d", _row.sortOrder);

				// 表示した時点で積まれている命令の数(フレームのどこで描くかで 0 にもなる)。
				// 準備中のプールで上限に届いていたら、溜めきれずに捨てている
				ImGui::TableSetColumnIndex(7);
				if (!_row.isReady && _row.requests >= Graphics::Particle::EMIT_PENDING_REQUEST_MAX)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%u", static_cast<unsigned>(_row.requests));
				}
				else
				{
					ImGui::Text("%u", static_cast<unsigned>(_row.requests));
				}

				// 最後に粒を出してからの秒数
				ImGui::TableSetColumnIndex(8);
				if (_row.sinceEmit < 0.0)
				{
					ImGui::TextDisabled("-");
				}
				else
				{
					ImGui::Text("%.1f s", _row.sinceEmit);
				}

				ImGui::TableSetColumnIndex(9);
				if (!_row.isReady)
				{
					ImGui::TextDisabled("Loading");
				}
				else if (_row.isGrowPending)
				{
					// 足りないと見て、次のシミュレーションで伸ばす
					ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Growing");
				}
				else if (!_row.isAwake)
				{
					ImGui::TextDisabled("Sleep");
				}
				else if (_row.isOverflowed)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Overflowed");
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("1フレームの発生命令が全体の上限(%u 件)を超えて、捨てたことがある", static_cast<unsigned>(Graphics::Particle::EMIT_BUFFER_MAX_CAPACITY));
					}
				}
				else
				{
					ImGui::Text("Ready");
				}
			}
			ImGui::EndTable();
		}
	}
}
