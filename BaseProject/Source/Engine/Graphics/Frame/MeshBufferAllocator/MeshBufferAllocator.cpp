#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshBufferAllocator.h"

#include "Engine/Graphics/Device/FrameManager/FrameManager.h"
#include "Engine/Graphics/Frame/DrawList/MeshDrawData.h"

namespace Engine::Graphics
{

	void MeshBufferAllocator::Init(
		D3D12::Device* a_pDevice,
		D3D12::DescriptorHeapManager* a_pHeapManager,
		const FrameManager* a_pFrameManager,
		D3D12::GraphicsCommandList* a_pCmdList,
		const BufferSizeDesc& a_bufferSizes
	)
	{
		// 領域を返すときのフェンス値はここから引く
		m_pFrameManager = a_pFrameManager;

		// メッシュ用バッファ作成 : サイズはもらい受ける
		m_staticVerticesBuffer.Create(a_pDevice,a_pHeapManager,a_pCmdList,a_bufferSizes.staticVertexBufferSize);
		m_skinVertexBuffer.Create(a_pDevice,a_pHeapManager,a_pCmdList,a_bufferSizes.skinVertexBufferSize);
		m_indexBuffer.Create(a_pDevice,a_pHeapManager,a_pCmdList,a_bufferSizes.indexBufferSize);
		m_animatedVertexBuffer.Create(a_pDevice,a_pHeapManager,a_bufferSizes.animatedVertexBufferSize);
		// 前フレームの位置も同じ要素数で作成(同じオフセットで参照するため)。
		// アニメ後の2本は描画キューで書くので、実メモリの割り当ても描画キューで流す(Create の既定)。
		// ほかはロードのコピーで書くので、コピーキューで流す
		m_prevAnimatedPositionBuffer.Create(a_pDevice,a_pHeapManager,a_bufferSizes.animatedVertexBufferSize);

		// メッシュレット数の上限。
		// CullData は DirectX::ComputeCullData がメッシュレット1個につき1個生成するため、
		// カリングバッファはメッシュレットバッファと必ず同じ要素数で確保する。
		// (別々の値にしていると CullData 側だけ 100 倍確保するような取り違えが起きる。
		//  sizeof(DirectX::CullData) は 24 バイトなので 1000万要素だと 229MiB を無駄に常駐させることになる)
		constexpr size_t MAX_MESHLET_NUM = 100000;

		m_meshletBuffer.Create(a_pDevice,a_pHeapManager,a_pCmdList,MAX_MESHLET_NUM);
		m_uniqueVertexIndicesBuffer.Create(a_pDevice, a_pHeapManager, a_pCmdList, 10000000);
		m_meshTriangleBuffer.Create(a_pDevice, a_pHeapManager, a_pCmdList, 10000000);
		m_meshletCullDataBuffer.Create(a_pDevice,a_pHeapManager,a_pCmdList,MAX_MESHLET_NUM);
	}

	void MeshBufferAllocator::Release()
	{
		m_staticVerticesBuffer.Release();
		m_skinVertexBuffer.Release();
		m_indexBuffer.Release();
		m_animatedVertexBuffer.Release();
		m_prevAnimatedPositionBuffer.Release();

		m_meshletBuffer.Release();
		m_uniqueVertexIndicesBuffer.Release();
		m_meshTriangleBuffer.Release();
		m_meshletCullDataBuffer.Release();
	}

	bool MeshBufferAllocator::FlushTileMappings(D3D12_COMMAND_LIST_TYPE a_queueType, D3D12::CommandQueue* a_pQueue, D3D12::Fence* a_pFence, UINT64 a_signalValue)
	{
		D3D12::MegaBuffer* _pBuffers[] = {
			&m_staticVerticesBuffer,
			&m_skinVertexBuffer,
			&m_indexBuffer,
			&m_animatedVertexBuffer,
			&m_prevAnimatedPositionBuffer,
			&m_meshletBuffer,
			&m_uniqueVertexIndicesBuffer,
			&m_meshTriangleBuffer,
			&m_meshletCullDataBuffer,
		};

		bool _isAnyFlushed = false;
		for (D3D12::MegaBuffer* _pBuffer : _pBuffers)
		{
			if (_pBuffer->FlushTileMappings(a_queueType, a_pQueue, a_pFence, a_signalValue)) _isAnyFlushed = true;
		}
		return _isAnyFlushed;
	}

	void MeshBufferAllocator::UpdateFrame(D3D12::GraphicsCommandList* /*a_pCmdList*/,uint64_t a_completedFenceValue)
	{
		// メッシュ用のバッファ更新
		m_staticVerticesBuffer.Update(a_completedFenceValue);
		m_skinVertexBuffer.Update(a_completedFenceValue);
		m_indexBuffer.Update(a_completedFenceValue);
		m_animatedVertexBuffer.Update(a_completedFenceValue);

		// 前フレームの位置は自分では配らないので、アニメ後の頂点が使っている範囲に合わせて返す
		m_prevAnimatedPositionBuffer.TrimMapped([this]() { return m_animatedVertexBuffer.GetUsedEnd(); });

		m_meshletBuffer.Update(a_completedFenceValue);
		m_uniqueVertexIndicesBuffer.Update(a_completedFenceValue);
		m_meshTriangleBuffer.Update(a_completedFenceValue);
		m_meshletCullDataBuffer.Update(a_completedFenceValue);
	}
	RangeHandle<Resource::MeshVertexPacked> MeshBufferAllocator::AllocateVertex(const Resource::ResourceBuildContext& a_ctx, const std::vector<Resource::MeshVertexFloat>& a_vertex)
	{
		// GPU 用の形に詰め直してから積む : 84 バイト → 32 バイト
		std::vector<Resource::MeshVertexPacked> _packed = {};
		_packed.reserve(a_vertex.size());
		for (const auto& _vertex : a_vertex)
		{
			_packed.push_back(Resource::MeshVertexPacked::From(_vertex));
		}

		return m_staticVerticesBuffer.AllocateAndRecordUpload(
			a_ctx.pCopyCmdList, *a_ctx.pKeepAliveUploads,
			_packed.data(), static_cast<UINT>(_packed.size())
		);
	}
	RangeHandle<Resource::MeshSkinVertex> MeshBufferAllocator::AllocateSkinVertex(const Resource::ResourceBuildContext& a_ctx, const std::vector<Resource::MeshVertexFloat>& a_vertex)
	{
		// ボーン割り当てだけを抜き出して積む : 頂点と同じ並び・同じ数
		std::vector<Resource::MeshSkinVertex> _skin = {};
		_skin.reserve(a_vertex.size());
		for (const auto& _vertex : a_vertex)
		{
			_skin.push_back(Resource::MeshSkinVertex::From(_vertex));
		}

		return m_skinVertexBuffer.AllocateAndRecordUpload(
			a_ctx.pCopyCmdList, *a_ctx.pKeepAliveUploads,
			_skin.data(), static_cast<UINT>(_skin.size())
		);
	}
	RangeHandle<uint32_t> MeshBufferAllocator::AllocateIndex(const Resource::ResourceBuildContext& a_ctx, const std::vector<uint32_t>& a_indices)
	{
		return m_indexBuffer.AllocateAndRecordUpload(
			a_ctx.pCopyCmdList, *a_ctx.pKeepAliveUploads,
			a_indices.data(), static_cast<UINT>(a_indices.size())
		);
	}
	RangeHandle<Resource::Meshlet> MeshBufferAllocator::AllocateMeshlet(const Resource::ResourceBuildContext& a_ctx, const std::vector<Resource::Meshlet>& a_meshlets)
	{
		return m_meshletBuffer.AllocateAndRecordUpload(
			a_ctx.pCopyCmdList, *a_ctx.pKeepAliveUploads,
			a_meshlets.data(), static_cast<UINT>(a_meshlets.size())
		);
	}
	RangeHandle<uint32_t> MeshBufferAllocator::AllocateUniqueVertIndices(const Resource::ResourceBuildContext& a_ctx, const std::vector<uint32_t>& a_uniqueVertIndices)
	{
		return m_uniqueVertexIndicesBuffer.AllocateAndRecordUpload(
			a_ctx.pCopyCmdList, *a_ctx.pKeepAliveUploads,
			a_uniqueVertIndices.data(), static_cast<UINT>(a_uniqueVertIndices.size())
		);
	}
	RangeHandle<DirectX::MeshletTriangle> MeshBufferAllocator::AllocateTriangles(const Resource::ResourceBuildContext& a_ctx, const std::vector<DirectX::MeshletTriangle>& a_triangles)
	{
		return m_meshTriangleBuffer.AllocateAndRecordUpload(
			a_ctx.pCopyCmdList, *a_ctx.pKeepAliveUploads,
			a_triangles.data(), static_cast<UINT>(a_triangles.size())
		);
	}
	RangeHandle<DirectX::CullData> MeshBufferAllocator::AllocateCullData(const Resource::ResourceBuildContext& a_ctx, const std::vector<DirectX::CullData>& a_cullData)
	{
		return m_meshletCullDataBuffer.AllocateAndRecordUpload(
			a_ctx.pCopyCmdList, *a_ctx.pKeepAliveUploads,
			a_cullData.data(), static_cast<UINT>(a_cullData.size())
		);
	}
	RangeHandle<Resource::AnimatedVertex> MeshBufferAllocator::AllocateAnimatedVertex(UINT a_size)
	{
		auto _handle = m_animatedVertexBuffer.Allocate(a_size);
		if (!_handle.IsValid()) return _handle;

		// 前フレームの位置は自分では配らず、アニメ後の頂点と同じオフセットで使う。
		// 実メモリだけは同じ範囲まで用意しておく(無いとスキニングの書き込みが落ちる)
		if (!m_prevAnimatedPositionBuffer.EnsureMapped(_handle.startIndex + _handle.count))
		{
			m_animatedVertexBuffer.ReserveFree(_handle, 0);
			return {};
		}
		return _handle;
	}
	void MeshBufferAllocator::ReserveFreeStaticVertex(const RangeHandle<Resource::MeshVertexPacked>& a_handle)
	{
		ENGINE_LOG("静的頂点データバッファ : Free");
		m_staticVerticesBuffer.ReserveFree(a_handle, GetReleaseFenceValue());
	}
	void MeshBufferAllocator::ReserveFreeSkinVertex(const RangeHandle<Resource::MeshSkinVertex>& a_handle)
	{
		// スキンメッシュでなければ持っていない
		if (!a_handle.IsValid()) return;

		ENGINE_LOG("ボーン割り当てバッファ : Free");
		m_skinVertexBuffer.ReserveFree(a_handle, GetReleaseFenceValue());
	}
	void MeshBufferAllocator::ReserveFreeIndex(const RangeHandle<uint32_t>&a_handle)
	{
		ENGINE_LOG("インデックスデータバッファ : Free");
		m_indexBuffer.ReserveFree(a_handle, GetReleaseFenceValue());
	}
	void MeshBufferAllocator::ReserveFreeAnimatedVertex(const RangeHandle<Resource::AnimatedVertex>&a_handle)
	{
		ENGINE_LOG("アニメーション後頂点データバッファ : Free");
		m_animatedVertexBuffer.ReserveFree(a_handle, GetReleaseFenceValue());
	}
	void MeshBufferAllocator::ReserveFreeMeshlet(const RangeHandle<Resource::Meshlet>& a_handle)
	{
		ENGINE_LOG("メッシュレット : Free");
		m_meshletBuffer.ReserveFree(a_handle, GetReleaseFenceValue());
	}
	void MeshBufferAllocator::ReserveFreeUniqueVertIndices(const RangeHandle<uint32_t>&a_handle)
	{
		ENGINE_LOG("ユニーク頂点インデックス : Free");
		m_uniqueVertexIndicesBuffer.ReserveFree(a_handle, GetReleaseFenceValue());
	}
	void MeshBufferAllocator::ReserveFreeTriangles(const RangeHandle<DirectX::MeshletTriangle>&a_handle)
	{
		ENGINE_LOG("メッシュトライアングル : Free");
		m_meshTriangleBuffer.ReserveFree(a_handle, GetReleaseFenceValue());
	}
	void MeshBufferAllocator::ReserveFreeMeshletCullData(const RangeHandle<DirectX::CullData>& a_handle)
	{
		ENGINE_LOG("メッシュレット当たり判定データ : Free");
		m_meshletCullDataBuffer.ReserveFree(a_handle, GetReleaseFenceValue());
	}

	uint64_t MeshBufferAllocator::GetReleaseFenceValue() const
	{
		// 今フレームが読み終わる(= 終わりにシグナルされる)まで空けない
		return m_pFrameManager ? m_pFrameManager->GetNextFenceValue() : 0;
	}
}
