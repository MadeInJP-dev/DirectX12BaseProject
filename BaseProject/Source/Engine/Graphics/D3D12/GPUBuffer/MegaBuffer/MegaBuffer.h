#pragma once
#include "Engine/Graphics/D3D12/GPUBuffer/GPUBuffer.h"

namespace Engine::Graphics::D3D12
{
	//==========================================================================================
	// シーンに一つ持つような巨大なバッファ
	//
	// 対応しているGPUでは予約リソース(タイルリソース)で作る。
	// 仮想アドレスだけを最大の大きさで取り、実メモリ(ヒープ)は配った範囲のぶんだけ
	// 64KB のタイル単位で足していく。アドレスは最後まで変わらないので、
	// ルートSRV・SRV の番号・BLAS が持つ頂点アドレスはそのまま使える。
	// 対応していなければ、以前と同じく最大の大きさで committed に作る。
	//
	// 足した実メモリの割り当て(UpdateTileMappings)はキューの操作なので、
	// その範囲を使うコマンドリストより先に、同じキューへ流す必要がある。
	// 確保した側はここへ積んでおくだけで、流すのは FlushTileMappings
	// (コマンドプールがリストを流す直前に呼ぶ)。
	// どのキューで流すかはバッファごとに決まっていて、最初に中身を書くキューに合わせる
	//==========================================================================================
	class MegaBuffer : public GPUBuffer
	{
	public:
		MegaBuffer() = default;
		~MegaBuffer() override = default;
		NON_COPYABLE_MOVABLE(MegaBuffer);

		// 実メモリのヒープも手放す
		void Release() override;

		/// <summary>
		/// 要素 [0, a_endElement) を GPU から触れるようにする。
		/// 予約リソースなら、足りないぶんの実メモリを足して割り当てを積む(流すのは FlushTileMappings)。
		/// committed なら何もしない
		/// </summary>
		/// <returns>実メモリを用意できなければ false</returns>
		bool EnsureMapped(uint32_t a_endElement);

		/// <summary>
		/// 積んである割り当てをキューへ流す。
		/// このバッファの割り当てを流すキュー(a_queueType が一致するもの)のときだけ流す
		/// </summary>
		void FlushTileMappings(D3D12_COMMAND_LIST_TYPE a_queueType, D3D12::CommandQueue* a_pQueue);

		// 計測用
		bool IsReserved() const { return m_isReserved; }
		uint64_t GetMappedBytes() const;		// 実メモリを割り当てた大きさ(committed なら全体)

		/// <summary>
		/// バッファの作成
		/// </summary>
		/// <param name="a_pDevice">デバイス</param>
		/// <param name="a_pCmdList">コマンドリスト</param>
		/// <param name="a_elemetNum">要素数</param>
		/// <param name="a_strideSize">要素サイズ</param>
		/// <param name="a_mappingQueueType">実メモリの割り当てを流すキュー(最初に中身を書くキュー)</param>
		/// <returns></returns>
		bool Create(
			Graphics::D3D12::Device* a_pDevice,
			DescriptorHeapManager* a_pHeapManager,
			Graphics::D3D12::GraphicsCommandList* a_pCmdList,
			size_t a_elemetNum,
			size_t a_strideSize,
			D3D12_COMMAND_LIST_TYPE a_mappingQueueType = D3D12_COMMAND_LIST_TYPE_COPY
		);

	protected:

		/// <summary>
		/// バッファの実体を作る : 予約リソースにできればそうし、できなければ committed にする。
		/// ビュー(SRV/UAV)は作らない(派生クラスが作る)
		/// </summary>
		/// <param name="a_mappingQueueType">実メモリの割り当てを流すキュー(最初に中身を書くキュー)</param>
		bool CreateBufferResource(
			Graphics::D3D12::Device* a_pDevice,
			size_t a_elementNum,
			size_t a_strideSize,
			D3D12_RESOURCE_FLAGS a_flags,
			D3D12_COMMAND_LIST_TYPE a_mappingQueueType
		);

		// テンプレート派生クラスから呼ぶための生データ操作関数

		/// <summary>
		/// 渡されたコマンドリストへ転送コマンドを積むだけ
		/// 実行は呼び出し元(バッチを開いた側)の責任
		/// </summary>
		/// <param name="a_pCmdList">積み先のコピーコマンドリスト</param>
		/// <param name="a_keepAlive">転送完了まで生かしておく中間バッファの預け先</param>
		void RecordUploadData(
			GraphicsCommandList* a_pCmdList,
			std::vector<ComPtr<ID3D12Resource>>& a_keepAlive,
			UINT a_destOffsetBytes,
			const void* a_pData,
			UINT a_sizeBytes
		);

	private:
		// CPUデータを書き込んだUploadヒープを作る
		ComPtr<ID3D12Resource> CreateUploadBuffer(const void* a_pData, UINT a_sizeBytes);

		// GPU が予約リソースを使えるか
		static bool IsReservedResourceSupported(Graphics::D3D12::Device* a_pDevice);

	protected:
		bool m_isDirty = false;

		// 中間バッファ・実メモリのヒープを作るデバイス(借り物)。Create で受け取ったものを持ち続ける
		Graphics::D3D12::Device* m_pDevice = nullptr;

	private:

		// 実メモリを足すときの単位(タイル数) : 64 タイル = 4MB
		static constexpr uint32_t CHUNK_TILE_COUNT = 64;

		// キューへ流す前の割り当て 1 件ぶん : タイル [startTile, startTile + tileCount) をヒープの先頭から
		struct PendingMapping
		{
			uint32_t startTile = 0;
			uint32_t tileCount = 0;
			ID3D12Heap* pHeap = nullptr;	// m_heapVec が持っている
		};

		// 予約リソースで作ったか(false なら committed で、割り当ては要らない)
		bool m_isReserved = false;

		// 割り当てを流すキュー
		D3D12_COMMAND_LIST_TYPE m_mappingQueueType = D3D12_COMMAND_LIST_TYPE_COPY;

		// 確保はロードのワーカースレッドから、流すのはコマンドプールを流すスレッドから来るので、
		// 以下はこの mutex を取ってから触る
		mutable std::mutex m_mappingMutex;
		uint32_t m_reservedTileCount = 0;							// 仮想アドレスの大きさ(タイル数)
		uint32_t m_mappedTileCount = 0;								// 実メモリを足し終えたタイル数(流す前のぶんも含む)
		std::vector<ComPtr<ID3D12Heap>> m_heapVec = {};				// 足した実メモリ。先頭から順にタイルへ割り当ててある
		std::vector<PendingMapping> m_pendingMappingVec = {};		// まだキューへ流していない割り当て
	};
}