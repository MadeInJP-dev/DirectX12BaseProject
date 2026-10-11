#pragma once
#include "Engine/Graphics/D3D12/GPUBuffer/MegaBuffer/MegaBuffer.h"
#include "Core/Debug/DebugLog.h"

namespace Engine::Graphics::D3D12
{
	/// <summary>
	/// シーンに一つ持つような巨大なバッファ : GPU専用
	/// </summary>
	template<typename T>
	class MegaRWStructuredBuffer : public MegaBuffer
	{
	public:
		MegaRWStructuredBuffer() = default;
		~MegaRWStructuredBuffer() override = default;
		NON_COPYABLE_MOVABLE(MegaRWStructuredBuffer);

		/// <summary>
		/// バッファの作成
		/// </summary>
		/// <param name="a_pDevice">デバイス</param>
		/// <param name="a_elemetNum">要素数</param>
		/// <param name="a_mappingQueueType">実メモリの割り当てを流すキュー(最初に中身を書くキュー)</param>
		/// <returns></returns>
		bool Create(
			Graphics::D3D12::Device* a_pDevice,
			DescriptorHeapManager* a_pHeapManager,
			UINT a_elemetNum,
			D3D12_COMMAND_LIST_TYPE a_mappingQueueType = D3D12_COMMAND_LIST_TYPE_DIRECT
		);

		/// <summary>
		/// // 中身のどこを使うかを割り当てて返す
		/// </summary>
		/// <param name="a_count">要素数</param>
		RangeHandle<T> Allocate(UINT a_count);

		/// <summary>
		/// 領域の解放 : GPU が a_releaseFenceValue まで進んだら再利用される
		/// </summary>
		/// <param name="a_handle">返す領域</param>
		/// <param name="a_releaseFenceValue">今フレームの終わりにシグナルされるフェンス値</param>
		void ReserveFree(const RangeHandle<T>& a_handle, uint64_t a_releaseFenceValue);

		/// <summary>
		/// バッファの更新
		/// </summary>
		/// <param name="a_currentFrameFence"></param>
		void Update(uint64_t a_currentFrameFence);

		// 領域の使われ方(計測用)
		RangeAllocatorStats GetRangeStats() const { return m_rangeAllocator.GetStats(); }

	private:
		bool m_isDirty = false;
		RangeAllocator<T> m_rangeAllocator;
	};

	template<typename T>
	inline bool MegaRWStructuredBuffer<T>::Create(Graphics::D3D12::Device* a_pDevice, DescriptorHeapManager* a_pHeapManager, UINT a_elemetNum, D3D12_COMMAND_LIST_TYPE a_mappingQueueType)
	{
		// アロケーターの作成
		m_rangeAllocator.Init(static_cast<uint32_t>(a_elemetNum));

		// バッファ作成 : 予約リソースにできればそうする
		if (!CreateBufferResource(a_pDevice, static_cast<size_t>(a_elemetNum), sizeof(T), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, a_mappingQueueType))
		{
			ENGINE_ERRLOG(false, "RWStructredBufferの作成に失敗");
			return false;
		}

		// 仕様書作成
		// SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC _srvDesc = {};
		_srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		_srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		_srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		_srvDesc.Buffer.FirstElement = 0;
		_srvDesc.Buffer.NumElements = a_elemetNum;
		_srvDesc.Buffer.StructureByteStride = sizeof(T);
		_srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		// UAV
		D3D12_UNORDERED_ACCESS_VIEW_DESC _uavDesc = {};
		_uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		_uavDesc.Format = DXGI_FORMAT_UNKNOWN;					// ストラクチャードバッファは必ずUNKNOWN
		_uavDesc.Buffer.FirstElement = 0;
		_uavDesc.Buffer.NumElements = a_elemetNum;
		_uavDesc.Buffer.StructureByteStride = sizeof(T);
		_uavDesc.Buffer.CounterOffsetInBytes = 0;				// Append/Consumeは使わないので0
		_uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		// ハンドルをもらう
		m_srvHandle = AllocateSRV(a_pDevice, a_pHeapManager, GetResource(), _srvDesc);
		m_uavHandle = AllocateUAV(a_pDevice, a_pHeapManager, GetResource(), _uavDesc);

		return true;
	}

	template<typename T>
	inline RangeHandle<T> MegaRWStructuredBuffer<T>::Allocate(UINT a_count)
	{
		// アロケーターから領域を確保
		auto _handle = m_rangeAllocator.AllocateRange(a_count);
		if (!_handle.IsValid()) return _handle;		// 容量不足

		// 配った範囲に実メモリを用意する(予約リソースのとき)。用意できなければ配ったぶんを返す
		if (!EnsureMapped(_handle.startIndex + _handle.count))
		{
			m_rangeAllocator.ReserveFreeRange(_handle, 0);
			return {};
		}

		// 中身のどこを使うかを割り当てて返す
		return _handle;
	}

	template<typename T>
	inline void MegaRWStructuredBuffer<T>::ReserveFree(const RangeHandle<T>& a_handle, uint64_t a_releaseFenceValue)
	{
		if (!a_handle.IsValid()) return;

		// 今フレームがこの領域を参照している可能性があるため、
		// 今フレーム完了時のフェンス値でタグ付けして遅延解放を予約する
		m_rangeAllocator.ReserveFreeRange(a_handle, a_releaseFenceValue);
	}

	template<typename T>
	inline void MegaRWStructuredBuffer<T>::Update(uint64_t a_currentFrameFence)
	{
		m_rangeAllocator.ApplyReservedFrees(a_currentFrameFence);
	}

}