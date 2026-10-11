#pragma once
#include "Engine/Graphics/D3D12/GPUBuffer/MegaBuffer/MegaBuffer.h"

namespace Engine::Graphics::D3D12
{
	/// <summary>
	/// シーンに一つ持つような巨大なバッファ
	/// 静的なデータで毎フレーム変わるようなデータには向かない
	/// </summary>
	template<typename T>
	class MegaStructuredBuffer : public MegaBuffer
	{
	public:
		MegaStructuredBuffer() = default;
		~MegaStructuredBuffer() override = default;
		NON_COPYABLE_MOVABLE(MegaStructuredBuffer);

		/// <summary>
		/// バッファの作成
		/// </summary>
		/// <param name="a_pDevice">デバイス</param>
		/// <param name="a_pCmdList">コマンドリスト</param>
		/// <param name="a_elemetNum">要素数</param>
		/// <returns></returns>
		bool Create(
			Graphics::D3D12::Device* a_pDevice,
			DescriptorHeapManager* a_pHeapManager,
			Graphics::D3D12::GraphicsCommandList* a_pCmdList,
			size_t a_elemetNum
		);

		/// <summary>
		/// 領域を確保し、渡されたコマンドリストへ転送コマンドを積む
		/// 実行はバッチを開いた側が行うため、まとめて読むときはこちらを使う
		/// </summary>
		/// <param name="a_pCmdList">積み先のコピーコマンドリスト</param>
		/// <param name="a_keepAlive">転送完了まで生かしておく中間バッファの預け先</param>
		RangeHandle<T> AllocateAndRecordUpload(
			GraphicsCommandList* a_pCmdList,
			std::vector<ComPtr<ID3D12Resource>>& a_keepAlive,
			const T* a_pData,
			UINT a_count
		);

		/// <summary>
		/// 領域の解放 : GPU が a_releaseFenceValue まで進んだら再利用される
		/// </summary>
		/// <param name="a_handle">返す領域</param>
		/// <param name="a_releaseFenceValue">
		/// 今フレームの終わりにシグナルされるフェンス値。
		/// 今フレームがまだこの領域を読んでいるかもしれないので、それが終わるまで空けない
		/// </param>
		void ReserveFree(const RangeHandle<T>& a_handle, uint64_t a_releaseFenceValue);

		/// <summary>
		/// バッファの更新
		/// </summary>
		/// <param name="a_currentFrameFence"></param>
		void Update(uint64_t a_currentFrameFence);

		// 領域の使われ方(計測用)
		RangeAllocatorStats GetRangeStats() const { return m_rangeAllocator.GetStats(); }

		// 使っている範囲の終わり(要素数)。同じオフセットで並走するバッファの実メモリを合わせるのに使う
		uint32_t GetUsedEnd() const { return m_rangeAllocator.GetUsedEnd(); }

	private:
		bool m_isDirty = false;
		RangeAllocator<T> m_rangeAllocator;
	};
	template<typename T>
	inline bool MegaStructuredBuffer<T>::Create(Graphics::D3D12::Device* a_pDevice, DescriptorHeapManager* a_pHeapManager, Graphics::D3D12::GraphicsCommandList* a_pCmdList, size_t a_elemetNum)
	{
		// アロケーターの作成
		m_rangeAllocator.Init(static_cast<uint32_t>(a_elemetNum));

		return MegaBuffer::Create(a_pDevice,a_pHeapManager,a_pCmdList,a_elemetNum,sizeof(T));
	}
	template<typename T>
	inline RangeHandle<T> MegaStructuredBuffer<T>::AllocateAndRecordUpload(
		GraphicsCommandList* a_pCmdList,
		std::vector<ComPtr<ID3D12Resource>>& a_keepAlive,
		const T* a_pData,
		UINT a_count
	)
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

		// バイトオフセットとサイズを計算
		UINT _destOffsetBytes = _handle.startIndex * sizeof(T);
		UINT _sizeBytes = a_count * sizeof(T);

		// 基底クラスの隠蔽された関数を呼んで転送コマンドを積む
		RecordUploadData(a_pCmdList, a_keepAlive, _destOffsetBytes, a_pData, _sizeBytes);

		return _handle;
	}

	template<typename T>
	inline void MegaStructuredBuffer<T>::ReserveFree(const RangeHandle<T>& a_handle, uint64_t a_releaseFenceValue)
	{
		if (!a_handle.IsValid()) return;

		// 今フレームがこの領域を参照している可能性があるため、
		// 今フレーム完了時のフェンス値でタグ付けして遅延解放を予約する
		m_rangeAllocator.ReserveFreeRange(a_handle, a_releaseFenceValue);
	}
	template<typename T>
	inline void MegaStructuredBuffer<T>::Update(uint64_t a_currentFrameFence)
	{
		m_rangeAllocator.ApplyReservedFrees(a_currentFrameFence);

		// 空いた後ろの実メモリを返す(予約リソースのとき)
		TrimMapped([this]() { return m_rangeAllocator.GetUsedEnd(); });
	}
}