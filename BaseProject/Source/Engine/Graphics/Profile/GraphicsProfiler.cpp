#include "Engine/Graphics/Profile/GraphicsProfiler.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Device/GraphicsDevice/GraphicsDevice.h"
#include "Engine/Graphics/D3D12/DescriptorHeapManager/DescriptorHeapManager.h"
#include "Engine/Graphics/D3D12/CBAllocator/CBAllocator.h"
#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshBufferAllocator.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/Graphics/Raytracing/BLASCompactor/BLASCompactor.h"

namespace Engine::Graphics
{
	GraphicsProfiler::GraphicsProfiler(const GraphicsEngine* a_pOwner)
		: m_pOwner(a_pOwner)
	{}

	GraphicsProfiler::~GraphicsProfiler()
	{}

	//======================================================================================
	// 取り直し
	//
	// 頼まれたフレームだけ取って、頼みは毎回下ろす。
	// 見る側が表示をやめれば次のフレームから何もしなくなる
	//======================================================================================
	void GraphicsProfiler::CaptureIfRequested()
	{
		if (!m_isCaptureRequested) return;
		m_isCaptureRequested = false;

		Capture();
	}

	void GraphicsProfiler::Capture()
	{
		if (!m_pOwner) return;

		m_snapshot.captureCount++;
		m_snapshot.renderWidth = m_pOwner->GetRenderWidth();
		m_snapshot.renderHeight = m_pOwner->GetRenderHeight();

		CaptureVideoMemory();
		CaptureVideoMemoryBreakdown();
		CaptureDescriptorHeap();
		CaptureMegaBuffers();
		CaptureFrameBuffers();
		CaptureConstantBuffer();
		CaptureDrawCount();
		CapturePipelineState();
	}

	void GraphicsProfiler::ResetPeaks()
	{
		m_cbGraphicsPeakBytes = 0;
		m_cbComputePeakBytes = 0;
		m_frameBufferPeakVec.clear();
	}

	//======================================================================================
	// ビデオメモリ
	//
	// DXGI の見積もり。使っている量が Budget を超えると、OS がリソースを
	// システムメモリへ追い出し始めて急に遅くなる
	//======================================================================================
	void GraphicsProfiler::CaptureVideoMemory()
	{
		VideoMemoryProfile& _out = m_snapshot.videoMemory;
		_out = {};

		const RenderDevice* _pRenderDevice = m_pOwner->GetRenderDevice();
		const GraphicsDevice* _pDevice = _pRenderDevice ? _pRenderDevice->GetGraphicsDevice() : nullptr;
		D3D12::Adapter* _pAdapter = _pDevice ? _pDevice->GetAdapter() : nullptr;
		if (!_pAdapter) return;

		// 使用量を引けるのは IDXGIAdapter3 から
		ComPtr<IDXGIAdapter3> _cpAdapter3 = nullptr;
		if (FAILED(_pAdapter->QueryInterface(IID_PPV_ARGS(&_cpAdapter3)))) return;

		DXGI_QUERY_VIDEO_MEMORY_INFO _local = {};
		DXGI_QUERY_VIDEO_MEMORY_INFO _nonLocal = {};
		if (FAILED(_cpAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &_local))) return;
		if (FAILED(_cpAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &_nonLocal))) return;

		_out.isValid = true;
		_out.localUsage = _local.CurrentUsage;
		_out.localBudget = _local.Budget;
		_out.nonLocalUsage = _nonLocal.CurrentUsage;
		_out.nonLocalBudget = _nonLocal.Budget;
	}

	//======================================================================================
	// ビデオメモリの用途別の内訳
	//
	// リソースを作ったときに付けた用途ごとに、今生きているぶんを合計したもの。
	// 容量の大きいものを固定で確保していると、使っていなくてもここに出る
	//======================================================================================
	void GraphicsProfiler::CaptureVideoMemoryBreakdown()
	{
		VideoMemoryBreakdownProfile& _out = m_snapshot.videoMemoryBreakdown;
		_out = {};

		const RenderDevice* _pRenderDevice = m_pOwner->GetRenderDevice();
		const GraphicsDevice* _pDevice = _pRenderDevice ? _pRenderDevice->GetGraphicsDevice() : nullptr;
		if (!_pDevice) return;

		const D3D12::VideoMemoryBreakdown _breakdown = _pDevice->GetVideoMemoryBreakdown();
		if (!_breakdown.isValid) return;

		_out.isValid = true;
		_out.categories.reserve(_breakdown.categories.size());
		for (size_t _i = 0; _i < _breakdown.categories.size(); ++_i)
		{
			const D3D12::VideoMemoryCategoryUsage& _usage = _breakdown.categories[_i];

			VideoMemoryCategoryProfile _row = {};
			_row.name = D3D12::ToString(static_cast<D3D12::EVideoMemoryCategory>(_i));
			_row.localBytes = _usage.localBytes;
			_row.nonLocalBytes = _usage.nonLocalBytes;
			_row.objectCount = _usage.objectCount;
			_out.categories.push_back(std::move(_row));

			_out.trackedLocalBytes += _usage.localBytes;
			_out.trackedNonLocalBytes += _usage.nonLocalBytes;
		}

		if (const Raytracing::BLASCompactor* _pCompactor = m_pOwner->GetBLASCompactor())
		{
			_out.blasCompactedCount = _pCompactor->GetCompactedCount();
			_out.blasCompactionSavedBytes = _pCompactor->GetSavedBytes();
		}
	}

	//======================================================================================
	// ディスクリプタヒープ
	//
	// CBV / SRV / UAV は1枚のヒープを区画に分けて使っている。
	// 区画ごとに上限があり、尽きると無効なハンドルが返ってビューが作れなくなる
	//======================================================================================
	void GraphicsProfiler::CaptureDescriptorHeap()
	{
		DescriptorHeapProfile& _out = m_snapshot.descriptorHeap;
		_out = {};

		const D3D12::DescriptorHeapManager* _pHeap = m_pOwner->GetDescriptorHeapManager();
		if (!_pHeap) return;

		auto _push = [&_out](const char* a_name, const D3D12::DescriptorUsage& a_usage)
			{
				GraphicsUsageProfile _row = {};
				_row.name = a_name;
				_row.used = a_usage.used;
				_row.capacity = a_usage.capacity;
				_row.peak = a_usage.peak;
				_out.views.push_back(std::move(_row));
			};

		_push("CBV", _pHeap->GetUsage<D3D12::CBV>());
		_push("SRV", _pHeap->GetUsage<D3D12::SRV>());
		_push("UAV", _pHeap->GetUsage<D3D12::UAV>());
		_push("RTV", _pHeap->GetUsage<D3D12::RTV>());
		_push("DSV", _pHeap->GetUsage<D3D12::DSV>());
		_push("ImGui SRV", _pHeap->GetUsage<D3D12::ImGuiSRV>());
		_push("ImGui Backend", _pHeap->GetImGuiBackendUsage());

		_out.pendingFreeCount = _pHeap->GetPendingFreeCount();
	}

	//======================================================================================
	// メガバッファ
	//
	// メッシュはロード時にここから領域を切り出して詰める。
	// 空きの合計が足りていても、一番大きい空き領域より大きいメッシュは入らない(断片化)
	//======================================================================================
	void GraphicsProfiler::CaptureMegaBuffers()
	{
		auto& _out = m_snapshot.megaBuffers;
		_out.clear();

		const MeshBufferAllocator* _pMesh = m_pOwner->GetMeshBufferAllocator();
		if (!_pMesh) return;

		auto _push = [&_out](const char* a_name, size_t a_strideBytes, const RangeAllocatorStats& a_stats, const D3D12::MegaBuffer& a_buffer)
			{
				MegaBufferProfile _row = {};
				_row.name = a_name;
				_row.strideBytes = a_strideBytes;
				_row.capacity = a_stats.capacity;
				_row.allocated = a_stats.allocated;
				_row.pending = a_stats.pending;
				_row.peakAllocated = a_stats.peakAllocated;
				_row.freeBlockCount = a_stats.freeBlockCount;
				_row.largestFreeBlock = a_stats.largestFreeBlock;
				_row.usedEnd = a_stats.usedEnd;
				_row.isReserved = a_buffer.IsReserved();
				_row.reservedBytes = a_buffer.GetReservedBytes();
				_row.mappedBytes = a_buffer.GetMappedBytes();
				_out.push_back(std::move(_row));
			};

		_push("Static Vertex", sizeof(Resource::MeshVertexFloat), _pMesh->GetStaticVertexBuffer().GetRangeStats(), _pMesh->GetStaticVertexBuffer());
		_push("Index", sizeof(uint32_t), _pMesh->GetIndexBuffer().GetRangeStats(), _pMesh->GetIndexBuffer());
		_push("Animated Vertex", sizeof(Resource::MeshVertexFloat), _pMesh->GetAnimatedVertexBuffer().GetRangeStats(), _pMesh->GetAnimatedVertexBuffer());

		// 前フレームの位置は自分では配らず、アニメ後の頂点と同じオフセットで使う。
		// 使われ方はアニメ後の頂点のものを出し、実メモリだけ自分のものを出す
		_push("Prev Animated Position", sizeof(Math::Vector3), _pMesh->GetAnimatedVertexBuffer().GetRangeStats(), _pMesh->GetPrevAnimatedPositionBuffer());
		_out.back().isSharedUsage = true;

		_push("Meshlet", sizeof(Resource::Meshlet), _pMesh->GetMeshletBuffer().GetRangeStats(), _pMesh->GetMeshletBuffer());
		_push("Unique Vertex Index", sizeof(uint32_t), _pMesh->GetUniqueVertexIndicesBuffer().GetRangeStats(), _pMesh->GetUniqueVertexIndicesBuffer());
		_push("Meshlet Triangle", sizeof(DirectX::MeshletTriangle), _pMesh->GetMeshletTriangleBuffer().GetRangeStats(), _pMesh->GetMeshletTriangleBuffer());
		_push("Meshlet Cull Data", sizeof(DirectX::CullData), _pMesh->GetMeshletCullDataBuffer().GetRangeStats(), _pMesh->GetMeshletCullDataBuffer());
	}

	//======================================================================================
	// 毎フレーム詰め直す構造体バッファ
	//
	// 使っている数はこのフレームに積まれた描画要求の数。
	// 容量を超えたぶんは上がらず、そのぶんは描かれない
	//======================================================================================
	void GraphicsProfiler::CaptureFrameBuffers()
	{
		auto& _out = m_snapshot.frameBuffers;
		_out.clear();

		const RenderContext* _pContext = m_pOwner->GetRenderContext();
		const DrawLists* _pDrawLists = m_pOwner->GetDrawLists();
		if (!_pContext || !_pDrawLists) return;

		const FrameBufferCapacity _capacity = _pContext->GetFrameBufferCapacity();
		const DebugDraw* _pDebugDraw = m_pOwner->GetDebugDraw();

		auto _push = [&_out](const char* a_name, size_t a_used, size_t a_capacity, size_t a_strideBytes)
			{
				GraphicsUsageProfile _row = {};
				_row.name = a_name;
				_row.used = a_used;
				_row.capacity = a_capacity;
				_row.strideBytes = a_strideBytes;
				_out.push_back(std::move(_row));
			};

		_push("Mesh Instance", _pDrawLists->GetInstanceDataVec().size(), _capacity.meshInstance, sizeof(MeshInstanceData));
		_push("Mesh Material", _pDrawLists->GetMeshMaterialVec().size(), _capacity.meshMaterial, sizeof(MeshMaterial));
		_push("Draw Instance Index", _pDrawLists->GetDrawInstanceIndexVec().size(), _capacity.drawInstanceIndex, sizeof(uint32_t));
		_push("Bone Matrix", _pDrawLists->GetBoneMatrixVec().size(), _capacity.bone, sizeof(Resource::BoneMatrix));
		_push("UI", _pDrawLists->GetUIDataVec().size(), _capacity.ui, sizeof(UIData));
		_push("Debug Line", _pDebugDraw ? _pDebugDraw->GetLineDataVec().size() : 0, _capacity.debugLine, sizeof(DebugLineData));
		_push("Ground Impulse", m_pOwner->GetGroundImpulseCount(), m_pOwner->GetGroundImpulseBuffer().GetElementNum(), sizeof(GroundImpulse));

		// 計測を始めてからの最大値 : 並びが変わったら(初回・リセット後)数え直す
		if (m_frameBufferPeakVec.size() != _out.size())
		{
			m_frameBufferPeakVec.assign(_out.size(), 0);
		}
		for (size_t _i = 0; _i < _out.size(); ++_i)
		{
			m_frameBufferPeakVec[_i] = (std::max)(m_frameBufferPeakVec[_i], _out[_i].used);
			_out[_i].peak = m_frameBufferPeakVec[_i];
		}
	}

	//======================================================================================
	// 定数バッファ
	//
	// ルートCBVで渡す値は、フレームごとのアップロード領域へ256バイト単位で積んでいく。
	// 溢れるとその定数は送られず、パスが前の値や空の値で描いてしまう
	//======================================================================================
	void GraphicsProfiler::CaptureConstantBuffer()
	{
		ConstantBufferProfile& _out = m_snapshot.constantBuffer;
		_out = {};

		const RenderContext* _pContext = m_pOwner->GetRenderContext();
		const D3D12::CBAllocator* _pCB = _pContext ? _pContext->GetCB() : nullptr;
		if (!_pCB) return;

		_out.graphicsUsedBytes = _pCB->GetUsedBytes();
		_out.graphicsCapacityBytes = _pCB->GetCapacityBytes();
		_out.computeUsedBytes = _pCB->GetComputeUsedBytes();
		_out.computeCapacityBytes = _pCB->GetComputeCapacityBytes();

		m_cbGraphicsPeakBytes = (std::max)(m_cbGraphicsPeakBytes, _out.graphicsUsedBytes);
		m_cbComputePeakBytes = (std::max)(m_cbComputePeakBytes, _out.computeUsedBytes);
		_out.graphicsPeakBytes = m_cbGraphicsPeakBytes;
		_out.computePeakBytes = m_cbComputePeakBytes;
	}

	//======================================================================================
	// 描画要求の数
	//======================================================================================
	void GraphicsProfiler::CaptureDrawCount()
	{
		DrawCountProfile& _out = m_snapshot.drawCount;
		_out = {};

		const DrawLists* _pDrawLists = m_pOwner->GetDrawLists();
		if (!_pDrawLists) return;

		_out.drawItemCount = _pDrawLists->GetItemCount();
		_out.instanceCount = _pDrawLists->GetInstanceDataVec().size();
		_out.materialCount = _pDrawLists->GetMeshMaterialVec().size();
		_out.skinningCount = _pDrawLists->GetSkinningItems().size();
		_out.dynamicRayRequestCount = _pDrawLists->GetDynamicRayRequests().size();
		_out.uiCount = _pDrawLists->GetUIDataVec().size();
		_out.boneMatrixCount = _pDrawLists->GetBoneMatrixVec().size();
		_out.groundImpulseCount = m_pOwner->GetGroundImpulseCount();

		const DebugDraw* _pDebugDraw = m_pOwner->GetDebugDraw();
		_out.debugLineCount = _pDebugDraw ? _pDebugDraw->GetLineDataVec().size() : 0;
	}

	//======================================================================================
	// パイプラインステート
	//======================================================================================
	void GraphicsProfiler::CapturePipelineState()
	{
		PipelineStateProfile& _out = m_snapshot.pipelineState;
		_out = {};

		const PipelineStateManager* _pPSO = m_pOwner->GetPipelineStateManager();
		if (!_pPSO) return;

		_out.psoCount = _pPSO->GetPSOCount();
		_out.rootSignatureCount = _pPSO->GetRootSignatureCount();
	}
}
