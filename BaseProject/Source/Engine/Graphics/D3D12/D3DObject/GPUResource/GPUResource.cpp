#include "Engine/Graphics/D3D12/D3DObject/GPUResource/GPUResource.h"

#include "Engine/Graphics/D3D12/DescriptorHeapManager/DescriptorHeapManager.h"

// CD3DX12_* のヘルパーはここだけで使う。
// プリコンパイル済みヘッダーへ置くと全翻訳単位に広がるため
#pragma warning(push, 0)
#include "d3dx12.h"
#pragma warning(pop)

namespace Engine::Graphics::D3D12
{
	bool GPUResource::Create(Graphics::D3D12::Device* a_pDevice, const GPUResourceDesc& a_desc)
	{
		// リソースサイズ計算
		m_strideSize = a_desc.strideSize;
		m_elementNum = a_desc.elementNum;
		m_bufferSize = m_strideSize * m_elementNum;

		// ステート設定
		m_currentState = a_desc.initialState;
		
		// ヒーププロパティ生成
		auto _prop = CD3DX12_HEAP_PROPERTIES(a_desc.heapType);

		// リソース生成
		auto _hr = a_pDevice->CreateCommittedResource(
			&_prop,
			a_desc.heapFlags,
			&a_desc.resourceDesc,
			m_currentState,
			a_desc.pClearValue,						// バッファだとnullptrなので
			IID_PPV_ARGS(m_cpResource.ReleaseAndGetAddressOf())
		);
		if (FAILED(_hr))
		{
			ENGINE_ERRLOG(false,"リソース生成に失敗");
			return false;
		}

		// フォーマットセット
		m_format = a_desc.format;

		// ビデオメモリの集計に入れる。解放されたら札が勝手に引く
		m_isPlaced = false;
		VideoMemoryTracker::TrackResource(m_cpResource.Get(), a_desc.memoryCategory);

		// リーク調査用 : ライブオブジェクトレポートに種別と通し番号を出すため名前を付ける。
		// (要素サイズ/数も入れておくと、どのバッファか特定しやすい)
		{
			static std::atomic<uint32_t> s_counter = 0;
			wchar_t _name[128];
			swprintf_s(_name, L"GPUResource#%u (stride=%zu num=%zu)",
				s_counter.fetch_add(1), a_desc.strideSize, a_desc.elementNum);
			m_cpResource->SetName(_name);
		}

		// 成功
		return true;
	}
	bool GPUResource::CreateReservedResource(Graphics::D3D12::Device* a_pDevice, const GPUResourceDesc& a_desc)
	{
		// リソースサイズ計算
		m_strideSize = a_desc.strideSize;
		m_elementNum = a_desc.elementNum;
		m_bufferSize = m_strideSize * m_elementNum;

		// ステート設定
		m_currentState = a_desc.initialState;

		// リソース予約
		auto _hr = a_pDevice->CreateReservedResource(
			&a_desc.resourceDesc,
			a_desc.initialState,
			a_desc.pClearValue,
			IID_PPV_ARGS(m_cpResource.ReleaseAndGetAddressOf())
		);
		if (FAILED(_hr))
		{
			ENGINE_ERRLOG(false, "リソース生成に失敗");
			return false;
		}

		// フォーマットセット
		m_format = a_desc.format;

		// 予約リソースは実メモリを持たないので、ビデオメモリの集計には入れない。
		// 数えるのは、使う範囲に割り当てたヒープのほう(割り当てる側が TrackHeap する)
		m_isPlaced = false;

		// リーク調査用 : ライブオブジェクトレポートに種別と通し番号を出すため名前を付ける。
		// (要素サイズ/数も入れておくと、どのバッファか特定しやすい)
		{
			static std::atomic<uint32_t> s_counter = 0;
			wchar_t _name[128];
			swprintf_s(_name, L"GPUResource#%u reserved (stride=%zu num=%zu)",
				s_counter.fetch_add(1), a_desc.strideSize, a_desc.elementNum);
			m_cpResource->SetName(_name);
		}

		// 成功
		return true;
	}
	bool GPUResource::Create(Graphics::D3D12::Device* a_pDevice, ID3D12Heap* a_pHeap, uint64_t a_heapOffset, const D3D12_RESOURCE_DESC& a_resDesc, D3D12_RESOURCE_STATES a_initialState, size_t a_strideSize, size_t a_elementNum, const D3D12_CLEAR_VALUE* a_pClearValue)
	{
		if (!a_pHeap)
		{
			ENGINE_ERRLOG(false, "配置先のヒープが空です");
			return false;
		}

		// リソースサイズ計算
		m_strideSize = a_strideSize;
		m_elementNum = a_elementNum;
		m_bufferSize = m_strideSize * m_elementNum;

		// ステート設定
		m_currentState = a_initialState;

		// フォーマットセット
		m_format = a_resDesc.Format;

		// 指定されたヒープ上のオフセットに作成する
		auto _hr = a_pDevice->CreatePlacedResource(
			a_pHeap,
			a_heapOffset,
			&a_resDesc,
			a_initialState,
			a_pClearValue,
			IID_PPV_ARGS(m_cpResource.ReleaseAndGetAddressOf())
		);
		if (FAILED(_hr))
		{
			// 席の大きさ不足・オフセットのアライメント違反・
			// ヒープのフラグ違い(Tier1で種別が混在)あたりが原因になりやすい
			ENGINE_ERRLOG(false, "CreatePlacedResource に失敗 : offset=%llu", a_heapOffset);
			return false;
		}

		// ビデオメモリは置き先のヒープのぶんとして数えているので、ここでは数えない
		m_isPlaced = true;

		// 成功
		return true;
	}
	void GPUResource::Release()
	{
		// ビデオメモリの集計は、実体が最後に消えたときに札が引く
		m_cpResource.Reset();
		m_isPlaced = false;

		// ビューを取ったときに預かったヒープへ返す。
		// 一度も取っていない(= nullptr)なら返すものが無い
		if (m_pHeapManager)
		{
			m_pHeapManager->ReserveFree(m_srvHandle);
			m_pHeapManager->ReserveFree(m_uavHandle);
			m_pHeapManager->ReserveFree(m_rtvHandle);
			m_pHeapManager->ReserveFree(m_dsvHandle);
			m_pHeapManager->ReserveFree(m_readOnlyDsvHandle);
			m_pHeapManager->FreeImGuiSRV(m_imguiSRVHandle);
		}

		// ハンドルを空にする。
		// 同じリソースへ Release が二度来ても、返し済みの席を二重に返さないため
		m_srvHandle = {};
		m_uavHandle = {};
		m_rtvHandle = {};
		m_dsvHandle = {};
		m_readOnlyDsvHandle = {};
		m_imguiSRVHandle = {};

		m_pHeapManager = nullptr;
	}
	void GPUResource::Barrier(Graphics::D3D12::GraphicsCommandList* a_pCmdList, D3D12_RESOURCE_STATES a_nextState)
	{
		if (m_currentState == a_nextState) return;

		D3D12_RESOURCE_BARRIER _barrier = {};
		_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		_barrier.Transition.pResource = m_cpResource.Get();
		_barrier.Transition.StateAfter = a_nextState;
		_barrier.Transition.StateBefore = m_currentState;
		_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		a_pCmdList->ResourceBarrier(1, &_barrier);

		// ステートの更新
		m_currentState = a_nextState;
	}
	void GPUResource::AliasingBarrier(Graphics::D3D12::GraphicsCommandList* a_pCmdList, GPUResource* a_pBeforeResource)
	{
		D3D12_RESOURCE_BARRIER _barrier = {};
		_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
		_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

		_barrier.Aliasing.pResourceBefore = a_pBeforeResource ? a_pBeforeResource->GetResource() : nullptr;
		_barrier.Aliasing.pResourceAfter = GetResource();

		a_pCmdList->ResourceBarrier(1,&_barrier);
	}
	void GPUResource::SetMemoryCategory(EVideoMemoryCategory a_category)
	{
		if (m_isPlaced) return;

		VideoMemoryTracker::TrackResource(m_cpResource.Get(), a_category);
	}
	ID3D12Resource* GPUResource::GetResource() const
	{
		return m_cpResource.Get();
	}
	D3D12_GPU_VIRTUAL_ADDRESS GPUResource::GetGPUVirtualAddress() const
	{
		return m_cpResource->GetGPUVirtualAddress();
	}
	size_t GPUResource::GetBufferSize() const
	{
		return m_bufferSize;
	}
	size_t GPUResource::GetStrideSize() const
	{
		return m_strideSize;
	}
	size_t GPUResource::GetElementNum() const
	{
		return m_elementNum;
	}
}