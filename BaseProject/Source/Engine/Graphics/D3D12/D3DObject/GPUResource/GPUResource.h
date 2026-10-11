#pragma once
#include "Engine/Graphics/D3D12/VideoMemoryTracker/VideoMemoryTracker.h"

namespace Engine::Graphics::D3D12
{
	// 前方宣言
	class DescriptorHeapManager;

	// リソース作成用構造体
	struct GPUResourceDesc
	{
		D3D12_HEAP_TYPE		heapType	= D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC resourceDesc = {};	// リソース作成情報
		size_t				strideSize = 0;		// 一つのサイズ
		size_t				elementNum = 0;		// 要素数

		D3D12_HEAP_FLAGS		heapFlags	= D3D12_HEAP_FLAG_NONE;
		D3D12_RESOURCE_STATES	initialState	= D3D12_RESOURCE_STATE_COMMON;
		DXGI_FORMAT				format		= DXGI_FORMAT_UNKNOWN;
		D3D12_CLEAR_VALUE*		pClearValue	= nullptr;						// バッファだとnullptrなので

		// 用途フラグ
		D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;

		// ビデオメモリの集計で、どの用途として数えるか
		EVideoMemoryCategory memoryCategory = EVideoMemoryCategory::Other;
	};

	// リソース規定クラス
	class GPUResource
	{
	public:

		GPUResource() = default;
		virtual ~GPUResource() = default;
		NON_COPYABLE_MOVABLE(GPUResource);

		// 作成
		bool Create(Graphics::D3D12::Device* a_pDevice ,const GPUResourceDesc& a_desc);

		// 仮想アドレスだけを取る(予約リソース)。
		// 実メモリは持たないので、使う範囲はヒープを用意して UpdateTileMappings で割り当てること。
		// heapType は使わない(割り当てるヒープ側が持つ)
		bool CreateReservedResource(Graphics::D3D12::Device* a_pDevice, const GPUResourceDesc& a_desc);

		// 指定のヒープ上に作成する(placed)
		//
		// a_resDesc は、その席を確保したときに GetResourceAllocationInfo へ渡したものと
		// 同じでなければならない。食い違うと確保した席に収まらない。
		//
		// a_pClearValue は RTV / DSV のときだけ渡す(バッファは必ず nullptr)。
		// 渡さないとクリアのたびにドライバ側で最適化が効かず警告も出る
		bool Create(Graphics::D3D12::Device* a_pDevice,ID3D12Heap* a_pHeap, uint64_t a_heapOffset,const D3D12_RESOURCE_DESC& a_resDesc,D3D12_RESOURCE_STATES a_initialState,size_t a_strideSize,size_t a_elementNum,const D3D12_CLEAR_VALUE* a_pClearValue = nullptr);

		// 解放
		virtual void Release();

		// ステート遷移
		virtual void Barrier(Graphics::D3D12::GraphicsCommandList* a_pCmdList,D3D12_RESOURCE_STATES a_nextState);

		// エイリアシングバリア
		void AliasingBarrier(Graphics::D3D12::GraphicsCommandList* a_pCmdList, GPUResource* a_pBeforeResource);

		// ビデオメモリの集計で数える用途を付け替える。
		// 型の決まった派生クラス(構造体バッファなど)を作ったあと、持ち主が呼ぶ。
		// placed で作ったものは置き先のヒープで数えているので何もしない
		void SetMemoryCategory(EVideoMemoryCategory a_category);

		// アクセサ
		virtual ID3D12Resource* GetResource() const;
		virtual D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;
		size_t GetBufferSize() const;
		size_t GetStrideSize() const;
		size_t GetElementNum() const;
		D3D12_RESOURCE_STATES GetState() const { return m_currentState; }

		// =========================================================
		// ハンドルのアクセサ
		// =========================================================
		const Handle<Graphics::D3D12::SRV>& GetSRV() const { return m_srvHandle; }
		const Handle<Graphics::D3D12::UAV>& GetUAV() const { return m_uavHandle; }
		const Handle<Graphics::D3D12::RTV>& GetRTV() const { return m_rtvHandle; }
		const Handle<Graphics::D3D12::DSV>& GetDSV() const { return m_dsvHandle; }
		const Handle<Graphics::D3D12::DSV>& GetReadOnlyDSV() const { return m_readOnlyDsvHandle; }
		const Handle<Graphics::D3D12::ImGuiSRV>& GetImGuiSRV() const { return m_imguiSRVHandle; }

		const DXGI_FORMAT& GetFormat() const { return m_format; }

	protected:

		//--------------------------------------------------------------------------------------------
		// ビューを預けているディスクリプタヒープ(借り物)
		//
		// 実体は GraphicsEngine が持っている。ここは「どこへ返すのか」の控えで、
		// ビューを取ったときに受け取ったものをそのまま持ち続ける。
		// Release() の引数を増やさずに返却先へ辿れるようにするため
		// (Release() は virtual で呼び出し側が多く、引数を増やすと全部に波及する)。
		// ビューを1つも持たないリソースでは nullptr のまま
		//--------------------------------------------------------------------------------------------
		DescriptorHeapManager* m_pHeapManager = nullptr;

		// データ
		ComPtr<ID3D12Resource> m_cpResource = nullptr;
		D3D12_RESOURCE_STATES m_currentState = D3D12_RESOURCE_STATE_COMMON;
		DXGI_FORMAT m_format;

		// ヒープ上に置いたか(placed)。集計の付け替えで二重に数えないために見る
		bool m_isPlaced = false;

		size_t m_bufferSize = 0;
		size_t m_strideSize = 0;
		size_t m_elementNum = 0;

		// ビューハンドル
		Handle<Graphics::D3D12::SRV> m_srvHandle = {};
		Handle<Graphics::D3D12::UAV> m_uavHandle = {};
		Handle<Graphics::D3D12::RTV> m_rtvHandle = {};
		Handle<Graphics::D3D12::DSV> m_dsvHandle = {};
		Handle<Graphics::D3D12::DSV> m_readOnlyDsvHandle = {};
		// ImGui用SRV : SRVと別型にしておかないと ReserveFree() が本体のSRVアロケーターへ飛ぶ
		Handle<Graphics::D3D12::ImGuiSRV> m_imguiSRVHandle = {};
	};
}