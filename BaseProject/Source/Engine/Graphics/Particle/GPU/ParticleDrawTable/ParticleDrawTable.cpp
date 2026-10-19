#include "Engine/Graphics/Particle/GPU/ParticleDrawTable/ParticleDrawTable.h"

#include "Engine/MainEngine.h"	// GPU が使い終わってからの解放(ReserveRelease)

namespace
{
	// 描画設定の表の GPU 側を最初に用意する数(足りなければ2のべき乗で伸ばす)
	constexpr UINT RECORD_BUFFER_MIN_CAPACITY = 64;

	//======================================================================================
	// GPU が使い終わってから Release() を呼んで捨てる
	//
	// 前のフレームの描画がまだ読んでいるかもしれないので、その場では捨てない。
	// 壊すだけではディスクリプタヒープの席が返らない(返すのは Release())
	//======================================================================================
	template<typename T>
	void ReleaseAfterGPU(T& a_target)
	{
		if (!a_target.GetResource()) return;

		auto _spTarget = std::make_shared<T>(std::move(a_target));
		Engine::MainEngine::Instance().ReserveRelease([_spTarget]() { _spTarget->Release(); });
	}
}

namespace Engine::Graphics::Particle
{
	//======================================================================================
	// 描画設定の表を組む
	//======================================================================================
	void ParticleDrawTable::BeginBuild()
	{
		m_pending.clear();
		m_records.clear();
		m_isAlphaBlendRecords.clear();
		m_recordIndices.clear();
		m_isRecordReady = false;
		m_alphaCapacity = 0;
	}

	void ParticleDrawTable::AddRecord(
		const Handle<Resource::ParticlesAsset>& a_handle,
		const ParticleDrawRecord& a_record,
		bool a_isAlphaBlend,
		UINT a_capacity,
		int a_sortOrder,
		std::string_view a_name)
	{
		PendingRecord _pending = {};
		_pending.handle = a_handle;
		_pending.record = a_record;
		_pending.isAlphaBlend = a_isAlphaBlend;
		_pending.capacity = a_capacity;
		_pending.sortOrder = a_sortOrder;
		_pending.name = a_name;
		m_pending.push_back(_pending);
	}

	void ParticleDrawTable::Commit(
		Graphics::D3D12::Device* a_pDevice,
		Graphics::D3D12::DescriptorHeapManager* a_pHeapManager,
		Graphics::D3D12::GraphicsCommandList* a_pCmdList,
		UINT a_frameIndex)
	{
		if (!a_pDevice || !a_pHeapManager || !a_pCmdList) return;

		//----------------------------------------------------------------------
		// 番号を決める
		//
		// 半透明で距離が同じ粒は、表の番号の小さい方が先(下)に描かれる(並べ替えのキーの次に比べる)。
		// そこで SortOrder の小さい順(同じなら名前順、それも同じならハンドル順)に番号を振る。
		// プールの一覧は unordered_map なので、並べないとアセットを足しただけで番号が入れ替わる
		//----------------------------------------------------------------------
		std::stable_sort(m_pending.begin(), m_pending.end(),
			[](const PendingRecord& a_l, const PendingRecord& a_r)
			{
				if (a_l.sortOrder != a_r.sortOrder) return a_l.sortOrder < a_r.sortOrder;
				if (a_l.name != a_r.name) return a_l.name < a_r.name;
				return a_l.handle.id < a_r.handle.id;
			});

		// 表の番号は一覧の上位ビットに入るので、上限を超えたぶんは描かない
		if (m_pending.size() > PARTICLE_DRAW_RECORD_MAX)
		{
			if (!m_isOverflowWarned)
			{
				m_isOverflowWarned = true;
				ENGINE_WARNING("[Particle] 1フレームに描くパーティクルアセットが上限(%u 種類)を超えました。超えたぶんは描きません",
					PARTICLE_DRAW_RECORD_MAX);
			}
			m_pending.resize(PARTICLE_DRAW_RECORD_MAX);
		}

		uint64_t _alphaCapacity = 0;
		for (const auto& _pending : m_pending)
		{
			m_recordIndices[_pending.handle] = static_cast<uint32_t>(m_records.size());
			m_records.push_back(_pending.record);
			m_isAlphaBlendRecords.push_back(_pending.isAlphaBlend);
			if (_pending.isAlphaBlend) _alphaCapacity += _pending.capacity;
		}
		m_pending.clear();

		if (m_records.empty()) return;

		//----------------------------------------------------------------------
		// 表を送る
		//----------------------------------------------------------------------
		const UINT _recordNum = static_cast<UINT>(m_records.size());
		if (!ReserveRecordBuffer(a_pDevice, a_pHeapManager, a_pCmdList, _recordNum)) return;

		m_upRecordBuffer->UploadFrame(
			a_pCmdList,
			m_records.data(),
			sizeof(ParticleDrawRecord) * m_records.size(),
			a_frameIndex);
		m_isRecordReady = true;

		//----------------------------------------------------------------------
		// 半透明の粒を集める先をそろえる。
		// 集まる粒の数は、半透明のプールの容量の合計を超えない(各プールの生きている粒は容量以下)
		//----------------------------------------------------------------------
		m_alphaCapacity = static_cast<UINT>((std::min<uint64_t>)(_alphaCapacity, UINT32_MAX / 2));
		if (m_alphaCapacity > 0)
		{
			ResizeAlphaBuffers(a_pDevice, a_pHeapManager, m_alphaCapacity);
		}

		// 間接描画の引数は大きさが変わらないので、最初に1回だけ作る。
		// 中身は使う前にシミュレーションがリセット用の CS で書く
		if (!m_drawArgs.GetResource())
		{
			m_drawArgs.Create(a_pDevice, a_pHeapManager, DRAW_ARGS_ELEMENT_NUM);
			if (m_drawArgs.GetResource())
			{
				m_drawArgs.GetResource()->SetName(L"ParticleAlphaDrawArgs");
				m_drawArgs.SetMemoryCategory(Graphics::D3D12::EVideoMemoryCategory::Particle);
			}
		}
	}

	UINT ParticleDrawTable::GetRecordSRVIndex() const
	{
		return m_upRecordBuffer ? static_cast<UINT>(m_upRecordBuffer->GetSRVHandle().GetIndex()) : 0xFFFFFFFFu;
	}

	std::optional<uint32_t> ParticleDrawTable::FindRecordIndex(const Handle<Resource::ParticlesAsset>& a_handle) const
	{
		auto _it = m_recordIndices.find(a_handle);
		if (_it == m_recordIndices.end()) return std::nullopt;
		return _it->second;
	}

	bool ParticleDrawTable::IsAlphaBlendRecord(uint32_t a_recordIndex) const
	{
		return a_recordIndex < m_isAlphaBlendRecords.size() && m_isAlphaBlendRecords[a_recordIndex];
	}

	//======================================================================================
	// GPU 側の大きさをそろえる
	//======================================================================================
	bool ParticleDrawTable::ReserveRecordBuffer(
		Graphics::D3D12::Device* a_pDevice,
		Graphics::D3D12::DescriptorHeapManager* a_pHeapManager,
		Graphics::D3D12::GraphicsCommandList* a_pCmdList,
		UINT a_need)
	{
		if (m_upRecordBuffer && m_recordCapacity >= a_need) return true;

		UINT _newCapacity = (std::max)(m_recordCapacity, RECORD_BUFFER_MIN_CAPACITY);
		while (_newCapacity < a_need)
		{
			_newCapacity *= 2;
		}

		// 古い表は前のフレームの描画がまだ読んでいるかもしれないので、GPU が使い終わってから捨てる
		if (m_upRecordBuffer)
		{
			std::shared_ptr<Graphics::D3D12::StaticStructuredBuffer<ParticleDrawRecord>> _spOld(std::move(m_upRecordBuffer));
			MainEngine::Instance().ReserveRelease([_spOld]() { _spOld->Release(); });
		}

		m_upRecordBuffer = std::make_unique<Graphics::D3D12::StaticStructuredBuffer<ParticleDrawRecord>>();
		m_upRecordBuffer->Create(a_pDevice, a_pHeapManager, a_pCmdList, _newCapacity, nullptr);
		if (!m_upRecordBuffer->GetResource())
		{
			m_upRecordBuffer.reset();
			m_recordCapacity = 0;
			return false;
		}
		m_upRecordBuffer->SetMemoryCategory(Graphics::D3D12::EVideoMemoryCategory::Particle);
		m_recordCapacity = _newCapacity;
		return true;
	}

	void ParticleDrawTable::ResizeAlphaBuffers(
		Graphics::D3D12::Device* a_pDevice,
		Graphics::D3D12::DescriptorHeapManager* a_pHeapManager,
		UINT a_need)
	{
		//----------------------------------------------------------------------
		// 2のべき乗で持つ(バイトニックソートの決まり)。
		// 足りなければ伸ばす。要る数の4倍以上あるときは縮めて VRAM を返す
		// (ちょうどの大きさで作り直すと、増減のたびに作り直してしまうので余裕を見る)
		//----------------------------------------------------------------------
		const UINT _size = ToParticleSortSize(a_need);
		const bool _isShort = (m_sortSize < _size);
		const bool _isTooLarge = (m_sortSize > _size * 4);
		if (m_sortBuffer.GetResource() && m_drawList.GetResource() && !_isShort && !_isTooLarge) return;

		// 中身は毎フレーム集め直すので写さなくてよい
		ReleaseAfterGPU(m_sortBuffer);
		ReleaseAfterGPU(m_drawList);
		m_sortSize = 0;

		m_sortBuffer.Create(a_pDevice, a_pHeapManager, _size);
		m_drawList.Create(a_pDevice, a_pHeapManager, _size);
		if (!m_sortBuffer.GetResource() || !m_drawList.GetResource())
		{
			// 作れたぶんも返す(ディスクリプタが漏れないように)。半透明は描かれないだけで壊れはしない
			m_sortBuffer.Release();
			m_drawList.Release();
			return;
		}

		m_sortBuffer.GetResource()->SetName(L"ParticleAlphaSortBuffer");
		m_drawList.GetResource()->SetName(L"ParticleAlphaDrawList");
		m_sortBuffer.SetMemoryCategory(Graphics::D3D12::EVideoMemoryCategory::Particle);
		m_drawList.SetMemoryCategory(Graphics::D3D12::EVideoMemoryCategory::Particle);
		m_sortSize = _size;
	}

	//======================================================================================
	// フレームの終わり・解放
	//======================================================================================
	void ParticleDrawTable::FinishFrame(Graphics::D3D12::GraphicsCommandList* a_pCmdList)
	{
		if (!a_pCmdList || !m_isArgsReady) return;

		// 次のフレームの頭は COMMON から始まる約束
		m_drawArgs.Barrier(a_pCmdList, D3D12_RESOURCE_STATE_COMMON);
		m_drawList.Barrier(a_pCmdList, D3D12_RESOURCE_STATE_COMMON);
		m_isArgsReady = false;
	}

	void ParticleDrawTable::Release()
	{
		// 終了時は最後のフレームがまだ読んでいるかもしれないので、完了待ちの後に流れる遅延解放へ回す
		ReleaseAfterGPU(m_sortBuffer);
		ReleaseAfterGPU(m_drawList);
		ReleaseAfterGPU(m_drawArgs);
		if (m_upRecordBuffer)
		{
			std::shared_ptr<Graphics::D3D12::StaticStructuredBuffer<ParticleDrawRecord>> _spOld(std::move(m_upRecordBuffer));
			MainEngine::Instance().ReserveRelease([_spOld]() { _spOld->Release(); });
		}

		m_recordCapacity = 0;
		m_sortSize = 0;
		m_isArgsReady = false;
		BeginBuild();
	}
}
