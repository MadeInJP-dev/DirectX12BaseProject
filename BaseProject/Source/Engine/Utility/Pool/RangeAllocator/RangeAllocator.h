#pragma once
namespace Engine
{
	// RangeAllocator の使われ方(計測用)。単位はすべて要素数
	struct RangeAllocatorStats
	{
		uint32_t capacity = 0;			// 全体
		uint32_t allocated = 0;			// 配っている数(返却待ちを含む)
		uint32_t pending = 0;			// 返されたが、GPUが使い終わるのを待っている数
		uint32_t peakAllocated = 0;		// Init してから一番多く配っていたときの数
		uint32_t freeBlockCount = 0;	// 空き領域の塊の数(多いほど断片化している)
		uint32_t largestFreeBlock = 0;	// 一番大きい空き領域 : 一度に確保できる上限
	};

	/// <summary>
	/// バッファと１対１で持つアロケータークラス
	/// </summary>
	template<typename T>
	class RangeAllocator
	{
	public:
		RangeAllocator() = default;
		~RangeAllocator() = default;

		/// <summary>
		/// 初期化 : 全体で一つのブロックを作る
		/// </summary>
		/// <param name="a_maxCount">最大領域数</param>
		void Init(uint32_t a_maxCount);

		/// <summary>
		/// 指定されたサイズの領域を確保する
		/// </summary>
		/// <param name="a_count">サイズ</param>
		/// <returns>割り当てられた領域</returns>
		RangeHandle<T> AllocateRange(uint32_t a_count);

		/// <summary>
		/// 指定した領域の解放 : 現在のフレームでは解放しずにフェンス値によって遅延開放される
		/// </summary>
		/// <param name="a_handle">指定した領域</param>
		/// <param name="a_releaseFenceValue">この値をGPUが完了させたら再利用可能になる。
		/// 今フレームがこの領域を参照している可能性があるため、最後にシグナル済みの値ではなく
		/// 今フレーム完了時にシグナルされる値を渡すこと</param>
		void ReserveFreeRange(const RangeHandle<T>& a_handle, uint64_t a_releaseFenceValue);

		// 毎フレームの頭で呼ぶ：GPUが使い終わった領域をフリーリストに戻す
		
		/// <summary>
		/// 毎フレーム最初に呼ぶ : GPUが使い終わった領域をフリーリストに戻す
		/// メッシュバッファアロケータ側のバッファと同期する必要あり
		/// </summary>
		/// <param name="a_completedFenceValue">GPUが実際に完了させたフェンス値</param>
		void ApplyReservedFrees(uint64_t a_completedFenceValue);


		uint32_t GetMaxCount() const { return m_maxCount; }

		// 使われ方の計測(ワーカーが確保中でも読めるようロックを取る)
		RangeAllocatorStats GetStats() const;
	private:

		/// <summary>
		/// 空いている領域を集めて塊にする
		/// </summary>
		void Merge();

	private:
		struct FreeBlock
		{
			uint32_t startIndex;
			uint32_t count;
		};

		// 遅延解放用の構造体
		struct PendingFree
		{
			FreeBlock block;
			uint64_t releaseFenceValue; // このフェンス値を越えたら再利用可能
		};

		std::vector<FreeBlock> m_freeBlocks;
		std::queue<PendingFree> m_pendingFrees;
		uint32_t m_currentAllocationId = 1;
		uint32_t m_maxCount = 0;

		// 計測用 : 配っている数(返却待ちを含む) / 返却待ちの数 / 一番多く配っていたときの数
		uint32_t m_allocatedCount = 0;
		uint32_t m_pendingCount = 0;
		uint32_t m_peakAllocatedCount = 0;

		mutable std::mutex m_mutex;	// 排他処理
	};
	template<typename T>
	inline void RangeAllocator<T>::Init(uint32_t a_maxCount)
	{
		m_maxCount = a_maxCount;
		m_freeBlocks.clear();
		while (!m_pendingFrees.empty()) m_pendingFrees.pop();

		m_allocatedCount = 0;
		m_pendingCount = 0;
		m_peakAllocatedCount = 0;

		m_freeBlocks.push_back({ 0, m_maxCount });
	}
	template<typename T>
	inline RangeHandle<T> RangeAllocator<T>::AllocateRange(uint32_t a_count)
	{
		// 要素数が0だったら割り当てられない
		if (a_count == 0)
		{
			ENGINE_ERRLOG(false, "サイズ0のメモリ領域が要求されました");
			return { std::numeric_limits<uint32_t>::max(), 0, 0 };
		}

		std::lock_guard<std::mutex> _lock(m_mutex);

		for (auto _it = m_freeBlocks.begin(); _it != m_freeBlocks.end(); ++_it)
		{
			if (_it->count >= a_count)
			{
				uint32_t _startIdx = _it->startIndex;

				if (_it->count == a_count)
				{
					m_freeBlocks.erase(_it);
				}
				else
				{
					_it->startIndex += a_count;
					_it->count -= a_count;
				}

				m_allocatedCount += a_count;
				m_peakAllocatedCount = (std::max)(m_peakAllocatedCount, m_allocatedCount);

				// 型安全なハンドルを返す
				return { _startIdx, a_count, m_currentAllocationId++ };
			}
		}
		ENGINE_ERRLOG(false, "メガバッファのメモリ領域がいっぱいです");
		return { std::numeric_limits<uint32_t>::max(), 0, 0 }; // 無効なハンドル
	}
	template<typename T>
	inline void RangeAllocator<T>::ReserveFreeRange(const RangeHandle<T>& a_handle, uint64_t a_releaseFenceValue)
	{
		if (!a_handle.IsValid()) return;

		// 確保(ロードのワーカースレッド)と同時に来ることがあるので、同じロックで守る
		std::lock_guard<std::mutex> _lock(m_mutex);

		m_pendingFrees.push({
			{ a_handle.startIndex, a_handle.count },
			a_releaseFenceValue
			});
		m_pendingCount += a_handle.count;
	}
	template<typename T>
	inline void RangeAllocator<T>::ApplyReservedFrees(uint64_t a_completedFenceValue)
	{
		// 空きリストは確保(ロードのワーカースレッド)も触るので、同じロックで守る
		std::lock_guard<std::mutex> _lock(m_mutex);

		bool _needsMerge = false;

		while (!m_pendingFrees.empty())
		{
			if (m_pendingFrees.front().releaseFenceValue <= a_completedFenceValue)
			{
				ENGINE_LOG(
					"FreeListの解放 : start %d, count %d",
					m_pendingFrees.front().block.startIndex, m_pendingFrees.front().block.count
				);
				const uint32_t _count = m_pendingFrees.front().block.count;
				m_allocatedCount -= _count;
				m_pendingCount -= _count;

				m_freeBlocks.push_back(m_pendingFrees.front().block);
				m_pendingFrees.pop();
				_needsMerge = true;
			}
			else
			{
				break;
			}
		}
		if (_needsMerge) Merge();
	}
	template<typename T>
	inline RangeAllocatorStats RangeAllocator<T>::GetStats() const
	{
		std::lock_guard<std::mutex> _lock(m_mutex);

		RangeAllocatorStats _stats = {};
		_stats.capacity = m_maxCount;
		_stats.allocated = m_allocatedCount;
		_stats.pending = m_pendingCount;
		_stats.peakAllocated = m_peakAllocatedCount;
		_stats.freeBlockCount = static_cast<uint32_t>(m_freeBlocks.size());
		for (const FreeBlock& _block : m_freeBlocks)
		{
			_stats.largestFreeBlock = (std::max)(_stats.largestFreeBlock, _block.count);
		}
		return _stats;
	}
	template<typename T>
	inline void RangeAllocator<T>::Merge()
	{
		if (m_freeBlocks.size() <= 1) return;

		std::sort(m_freeBlocks.begin(), m_freeBlocks.end(),
			[](const FreeBlock& a_a, const FreeBlock& a_b) {
				return a_a.startIndex < a_b.startIndex;
			}
		);

		size_t _writeIdx = 0;
		for (size_t _readIdx = 1; _readIdx < m_freeBlocks.size(); ++_readIdx)
		{
			if (m_freeBlocks[_writeIdx].startIndex + m_freeBlocks[_writeIdx].count == m_freeBlocks[_readIdx].startIndex)
			{
				m_freeBlocks[_writeIdx].count += m_freeBlocks[_readIdx].count;
			}
			else
			{
				_writeIdx++;
				m_freeBlocks[_writeIdx] = m_freeBlocks[_readIdx];
			}
		}
		m_freeBlocks.resize(_writeIdx + 1);
	}
}