#include "Engine/Graphics/D3D12/GPUBuffer/MegaBuffer/MegaBuffer.h"


namespace Engine::Graphics::D3D12
{
	bool Engine::Graphics::D3D12::MegaBuffer::Create(
		Graphics::D3D12::Device* a_pDevice,
		DescriptorHeapManager* a_pHeapManager,
		Graphics::D3D12::GraphicsCommandList* /*a_pCmdList*/,
		size_t a_elemetNum,
		size_t a_strideSize,
		D3D12_COMMAND_LIST_TYPE a_mappingQueueType
	)
	{
		// リソースの作成 : 予約リソースにできればそうする
		if (!CreateBufferResource(a_pDevice, a_elemetNum, a_strideSize, D3D12_RESOURCE_FLAG_NONE, a_mappingQueueType))
		{
			ENGINE_ERRLOG(false, "メガバッファの作成に失敗");
			return false;
		}

		// 仕様書作成
		D3D12_SHADER_RESOURCE_VIEW_DESC _desc = {};
		_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		_desc.Format = DXGI_FORMAT_UNKNOWN;
		_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		_desc.Buffer.FirstElement = 0;
		_desc.Buffer.NumElements = static_cast<UINT>(m_elementNum);
		_desc.Buffer.StructureByteStride = static_cast<UINT>(m_strideSize);
		_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		// ハンドルをもらう
		m_srvHandle = AllocateSRV(a_pDevice, a_pHeapManager, GetResource(), _desc);

		return true;
	}


	ComPtr<ID3D12Resource> MegaBuffer::CreateUploadBuffer(const void* a_pData, UINT a_sizeBytes)
	{
		// 一時的なUploadバッファを作ってCPUデータを書き込む
		Microsoft::WRL::ComPtr<ID3D12Resource> _cpLoadBuffer;
		D3D12_HEAP_PROPERTIES _heapProps = {};
		_heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC _resDesc = {};
		_resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		_resDesc.Width = a_sizeBytes;
		_resDesc.Height = 1;
		_resDesc.DepthOrArraySize = 1;
		_resDesc.MipLevels = 1;
		_resDesc.Format = DXGI_FORMAT_UNKNOWN;
		_resDesc.SampleDesc.Count = 1;
		_resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		_resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		if (!m_pDevice) return nullptr;
		m_pDevice->CreateCommittedResource(
			&_heapProps,
			D3D12_HEAP_FLAG_NONE,
			&_resDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&_cpLoadBuffer)
		);
		if (_cpLoadBuffer) _cpLoadBuffer->SetName(L"Mega_UploadBuffer");	// リーク調査用
		VideoMemoryTracker::TrackResource(_cpLoadBuffer.Get(), EVideoMemoryCategory::Upload);

		// データの書き込み
		void* _pMapped = nullptr;
		_cpLoadBuffer->Map(0, nullptr, &_pMapped);
		std::memcpy(_pMapped, a_pData, a_sizeBytes);
		_cpLoadBuffer->Unmap(0, nullptr);

		return _cpLoadBuffer;
	}

	void MegaBuffer::RecordUploadData(
		GraphicsCommandList* a_pCmdList,
		std::vector<ComPtr<ID3D12Resource>>& a_keepAlive,
		UINT a_destOffsetBytes,
		const void* a_pData,
		UINT a_sizeBytes
	)
	{
		if (!a_pCmdList) return;

		// 中間バッファの用意
		auto _cpLoadBuffer = CreateUploadBuffer(a_pData, a_sizeBytes);
		if (!_cpLoadBuffer) return;

		// 転送コマンドを積むだけ : 実行はバッチを開いた側が行う
		a_pCmdList->CopyBufferRegion(
			m_cpResource.Get(), a_destOffsetBytes,
			_cpLoadBuffer.Get(), 0, a_sizeBytes
		);

		// GPUの転送が終わるまで中間バッファを生かしておく
		a_keepAlive.push_back(std::move(_cpLoadBuffer));
	}

	bool MegaBuffer::CreateBufferResource(
		Graphics::D3D12::Device* a_pDevice,
		size_t a_elementNum,
		size_t a_strideSize,
		D3D12_RESOURCE_FLAGS a_flags,
		D3D12_COMMAND_LIST_TYPE a_mappingQueueType
	)
	{
		// 中間バッファ・実メモリのヒープを作るときに使う
		m_pDevice = a_pDevice;
		m_mappingQueueType = a_mappingQueueType;

		{
			std::lock_guard<std::mutex> _lock(m_mappingMutex);
			m_isReserved = false;
			m_reservedTileCount = 0;
			m_mappedTileCount = 0;
			m_heapVec.clear();
			m_pendingMappingVec.clear();
		}

		//------------------------------------------------------------------------------------------
		// 予約リソース : 仮想アドレスだけを取る
		//
		// 大きさはタイル(64KB)の倍数に切り上げる。最後のタイルも丸ごと割り当てられるように
		//------------------------------------------------------------------------------------------
		if (IsReservedResourceSupported(a_pDevice))
		{
			const UINT64 _tileSize = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
			const UINT64 _tileCount = (static_cast<UINT64>(a_elementNum) * a_strideSize + _tileSize - 1) / _tileSize;

			GPUResourceDesc _desc = {};
			_desc.strideSize = a_strideSize;
			_desc.elementNum = a_elementNum;
			_desc.initialState = D3D12_RESOURCE_STATE_COMMON;
			_desc.memoryCategory = EVideoMemoryCategory::MeshBuffer;
			_desc.resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			_desc.resourceDesc.Width = _tileCount * _tileSize;
			_desc.resourceDesc.Height = 1;
			_desc.resourceDesc.DepthOrArraySize = 1;
			_desc.resourceDesc.MipLevels = 1;
			_desc.resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
			_desc.resourceDesc.SampleDesc.Count = 1;
			_desc.resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			_desc.resourceDesc.Flags = a_flags;

			if (CreateReservedResource(a_pDevice, _desc))
			{
				std::lock_guard<std::mutex> _lock(m_mappingMutex);
				m_isReserved = true;
				m_reservedTileCount = static_cast<uint32_t>(_tileCount);
				return true;
			}

			ENGINE_WARNING("[MegaBuffer] 予約リソースを作れなかったので、最大の大きさで committed に作ります (stride=%zu num=%zu)",
				a_strideSize, a_elementNum);
		}

		//------------------------------------------------------------------------------------------
		// committed : 最大の大きさを最初から確保する(以前と同じ)
		//------------------------------------------------------------------------------------------
		GPUBufferDesc _gpuDesc = {};
		_gpuDesc.elementNum = a_elementNum;
		_gpuDesc.strideSize = a_strideSize;
		_gpuDesc.flags = a_flags;
		_gpuDesc.heapType = D3D12_HEAP_TYPE_DEFAULT;
		_gpuDesc.memoryCategory = EVideoMemoryCategory::MeshBuffer;
		return GPUBuffer::Create(a_pDevice, _gpuDesc);
	}

	void MegaBuffer::Release()
	{
		// 先にリソースを手放す : ヒープはタイルの割り当て先なので、リソースより後に消す
		GPUBuffer::Release();

		std::lock_guard<std::mutex> _lock(m_mappingMutex);
		m_isReserved = false;
		m_reservedTileCount = 0;
		m_mappedTileCount = 0;
		m_pendingMappingVec.clear();
		m_heapVec.clear();
	}

	//======================================================================================
	// 実メモリを足す
	//
	// 足したヒープはタイルへの割り当てを積むだけで、キューへはまだ流さない。
	// 呼んだ側はこの後、その範囲を使うコマンドリストを同じキューへ流す。
	// 流す直前に FlushTileMappings が呼ばれるので、割り当てがリストより先にキューへ乗る
	//======================================================================================
	bool MegaBuffer::EnsureMapped(uint32_t a_endElement)
	{
		if (!m_isReserved) return true;

		const UINT64 _tileSize = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
		const UINT64 _endBytes = static_cast<UINT64>(a_endElement) * m_strideSize;
		const uint32_t _needTileCount = static_cast<uint32_t>((_endBytes + _tileSize - 1) / _tileSize);

		std::lock_guard<std::mutex> _lock(m_mappingMutex);

		if (_needTileCount > m_reservedTileCount)
		{
			ENGINE_ERRLOG(false, "[MegaBuffer] 予約した大きさを超える範囲が求められました (%u / %u タイル)", _needTileCount, m_reservedTileCount);
			return false;
		}

		// 足りないぶんを単位ずつ足す。最後は予約の残りまでで止める
		while (m_mappedTileCount < _needTileCount)
		{
			const uint32_t _tileCount = (std::min)(CHUNK_TILE_COUNT, m_reservedTileCount - m_mappedTileCount);

			D3D12_HEAP_DESC _heapDesc = {};
			_heapDesc.SizeInBytes = static_cast<UINT64>(_tileCount) * _tileSize;
			_heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			_heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
			_heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

			ComPtr<ID3D12Heap> _cpHeap = nullptr;
			if (FAILED(m_pDevice->CreateHeap(&_heapDesc, IID_PPV_ARGS(_cpHeap.ReleaseAndGetAddressOf()))))
			{
				ENGINE_ERRLOG(false, "[MegaBuffer] 実メモリを足せませんでした (%u タイル)", _tileCount);
				return false;
			}
			_cpHeap->SetName(L"MegaBuffer_TileHeap");	// リーク調査用
			VideoMemoryTracker::TrackHeap(_cpHeap.Get(), EVideoMemoryCategory::MeshBuffer);

			PendingMapping _mapping = {};
			_mapping.startTile = m_mappedTileCount;
			_mapping.tileCount = _tileCount;
			_mapping.pHeap = _cpHeap.Get();
			m_pendingMappingVec.push_back(_mapping);

			m_heapVec.push_back(std::move(_cpHeap));
			m_mappedTileCount += _tileCount;
		}
		return true;
	}

	//======================================================================================
	// 積んである割り当てをキューへ流す
	//
	// ロックを握ったまま流す。別のスレッドが「積まれていないので何もしない」で抜けて
	// 自分のリストを先に流してしまうことがないよう、流し終えるまで次の人を待たせる
	//======================================================================================
	void MegaBuffer::FlushTileMappings(D3D12_COMMAND_LIST_TYPE a_queueType, D3D12::CommandQueue* a_pQueue)
	{
		if (!m_isReserved || !a_pQueue) return;
		if (a_queueType != m_mappingQueueType) return;

		std::lock_guard<std::mutex> _lock(m_mappingMutex);

		for (const PendingMapping& _mapping : m_pendingMappingVec)
		{
			// リソース側 : 先頭タイルから並びで tileCount 枚
			D3D12_TILED_RESOURCE_COORDINATE _coordinate = {};
			_coordinate.X = _mapping.startTile;

			D3D12_TILE_REGION_SIZE _regionSize = {};
			_regionSize.NumTiles = _mapping.tileCount;
			_regionSize.UseBox = FALSE;

			// ヒープ側 : 先頭から同じ枚数
			const D3D12_TILE_RANGE_FLAGS _rangeFlags = D3D12_TILE_RANGE_FLAG_NONE;
			const UINT _heapStartTile = 0;
			const UINT _rangeTileCount = _mapping.tileCount;

			a_pQueue->UpdateTileMappings(
				m_cpResource.Get(),
				1, &_coordinate, &_regionSize,
				_mapping.pHeap,
				1, &_rangeFlags, &_heapStartTile, &_rangeTileCount,
				D3D12_TILE_MAPPING_FLAG_NONE
			);
		}
		m_pendingMappingVec.clear();
	}

	uint64_t MegaBuffer::GetMappedBytes() const
	{
		std::lock_guard<std::mutex> _lock(m_mappingMutex);
		if (!m_isReserved) return m_cpResource ? static_cast<uint64_t>(m_bufferSize) : 0;
		return static_cast<uint64_t>(m_mappedTileCount) * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
	}

	bool MegaBuffer::IsReservedResourceSupported(Graphics::D3D12::Device* a_pDevice)
	{
		if (!a_pDevice) return false;

		D3D12_FEATURE_DATA_D3D12_OPTIONS _options = {};
		if (FAILED(a_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &_options, sizeof(_options)))) return false;

		// バッファのタイルは Tier 1 から使える
		return _options.TiledResourcesTier != D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
	}
}
