#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"

#include "Engine/Graphics/D3D12/DescriptorHeapManager/DescriptorHeapManager.h"

#include "Engine/Graphics/D3D12/D3DObject/RootSignature/RootSignature.h"
#include "Engine/Graphics/D3D12/D3DObject/PipelineState/PipelineState.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/BackBuffer/BackBuffer.h"
#include "Engine/Graphics/Frame/DrawList/DrawList.h"
#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshBufferAllocator.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"

#include "Engine/ECS/World/World.h"

//============================================================================================
//
// 初期化
//
//============================================================================================
namespace Engine::Graphics
{
	namespace
	{
		// 描く順のインスタンス番号の表の容量(要素数)。
		// アイテムはインスタンスデータ(最大 100000)をパスの数だけ指すので、その数倍を見ておく
		constexpr UINT DRAW_INSTANCE_INDEX_CAPACITY = 400000;

		//------------------------------------------------------------------------------------------
		// メッシュシェーダーのルートパラメーター番号(MeshCommon.hlsli の MESHGLOBAL_ROOT_SIG と合わせる)
		//------------------------------------------------------------------------------------------
		constexpr UINT MESH_ROOT_BASE_INSTANCE = 9;			// RootConstants(b1) : 表を引く土台
		constexpr UINT MESH_ROOT_DRAW_INSTANCE_INDEX = 11;		// SRV(t9) : 描く順のインスタンス番号の表
		constexpr UINT MESH_ROOT_MESH_LOD = 12;					// SRV(t10) : LOD の表

		//------------------------------------------------------------------------------------------
		// DispatchMesh の上限
		// 1次元あたり 65535 グループ、3次元の積で 2^22 グループまで
		//------------------------------------------------------------------------------------------
		constexpr UINT MAX_DISPATCH_MESH_DIM = 65535;
		constexpr UINT MAX_DISPATCH_MESH_TOTAL = 1u << 22;
	}

	void RenderContext::Init(
		GraphicsEngine* a_pOwner,
		D3D12::GraphicsCommandList* a_pCmdList,
		const RenderContextDesc& a_desc
	)
	{
		m_pGraphicsEngine = a_pOwner;

		// デバイスのキャッシュ
		m_pDevice = a_desc.pDevice;

		// ビューの置き場をキャッシュ : 以降はここから引く
		m_pHeapManager = a_desc.pHeapManager;

		// テクスチャの実体を引く先
		m_pResourceManager = a_desc.pResourceManager;

		// 借り物の参照。どれも GraphicsEngine の持ち物で、このコンテキストより長生きする
		m_pPipelineStateManager = a_desc.pPipelineStateManager;
		m_pDrawLists = a_desc.pDrawLists;
		m_pBackBuffer = a_desc.pBackBuffer;

		// ルート定数バッファアロケーター
		m_upCBAllocator = std::make_unique<D3D12::CBAllocator>();
		m_upCBAllocator->RootCBVCreate(
			m_pDevice, a_desc.cbAllocatorMemSize
		);

		// バッファ作成
		m_boneBuffer.Create(a_desc.pDevice, m_pHeapManager, a_desc.boneElementNum);								// ボーン行列用
		m_debugLineBuffer.Create(a_desc.pDevice, m_pHeapManager, a_pCmdList, 10000, nullptr);					// 形状描画用バッファ

		// メッシュ用データの作成
		m_meshInstanceBuffer.Create(a_desc.pDevice, m_pHeapManager, a_pCmdList, 100000, nullptr);
		m_meshMaterialBuffer.Create(a_desc.pDevice, m_pHeapManager, a_pCmdList, 100000, nullptr);
		m_meshLodBuffer.Create(a_desc.pDevice, m_pHeapManager, a_pCmdList, MESH_LOD_DATA_CAPACITY, nullptr);

		// 描く順のインスタンス番号の表。
		// 1つのインスタンスデータを複数のパス(ZPre・GBuffer・影など)のアイテムが指すので、
		// インスタンスデータより多めに取っておく
		m_drawInstanceIndexBuffer.Create(a_desc.pDevice, m_pHeapManager, DRAW_INSTANCE_INDEX_CAPACITY);

		// UIインスタンス
		m_uiInstanceBuffer.Create(a_desc.pDevice, m_pHeapManager, 10000);

		// 描画用の板ポリはフレームごとに変わらないので、
		// コンテキストの数だけ作らずグラフィックスエンジンが1つずつ持っている
		// (GraphicsEngine::RefQuadPolygon / RefCurvedQuadPolygon)
	}

	void RenderContext::Release()
	{
		// リンク解除
		m_pDevice = nullptr;				// デバイス
		m_pCmdList = nullptr;				// コマンドリスト
		m_pHeapManager = nullptr;			// ビューの置き場(借り物)
		m_pPipelineStateManager = nullptr;	// PSO・ルートシグネチャ(借り物)
		m_pDrawLists = nullptr;				// 描画要求の配列(借り物)
		m_pBackBuffer = nullptr;			// バックバッファ(借り物)

		// ルート定数バッファ用アロケーター解放
		m_upCBAllocator->Release();
		
		// 各構造体バッファ解放
		m_boneBuffer.Release();
		m_debugLineBuffer.Release();
		m_meshInstanceBuffer.Release();
		m_meshMaterialBuffer.Release();
		m_meshLodBuffer.Release();
		m_drawInstanceIndexBuffer.Release();
		m_uiInstanceBuffer.Release();
	}

	void RenderContext::Clear()
	{
		m_pCmdList = nullptr;
		m_upCBAllocator->ResetUse();
	}

	D3D12::GraphicsCommandList* RenderContext::RefCurrentCmdList()
	{
		return m_pCmdList;
	}
	void RenderContext::SetDirectCommandList(D3D12::GraphicsCommandList* a_pCmdList)
	{
		m_pCmdList = a_pCmdList;
	}
	//============================================================================================
	//
	// カメラ
	//
	//============================================================================================


	D3D12::CBAllocator* RenderContext::BindCB()
	{
		return m_upCBAllocator.get();
	}

	FrameBufferCapacity RenderContext::GetFrameBufferCapacity() const
	{
		FrameBufferCapacity _capacity = {};
		_capacity.meshInstance = m_meshInstanceBuffer.GetElementNum();
		_capacity.meshMaterial = m_meshMaterialBuffer.GetElementNum();
		_capacity.meshLod = m_meshLodBuffer.GetElementNum();
		_capacity.drawInstanceIndex = m_drawInstanceIndexBuffer.GetElementNum();
		_capacity.bone = m_boneBuffer.GetElementNum();
		_capacity.ui = m_uiInstanceBuffer.GetElementNum();
		_capacity.debugLine = m_debugLineBuffer.GetElementNum();
		return _capacity;
	}

	void RenderContext::SetRenderTargets(const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& a_rtvHandleVec, const D3D12_CPU_DESCRIPTOR_HANDLE* a_pDsvHandle)
	{
		m_pCmdList->OMSetRenderTargets(
			static_cast<UINT>(a_rtvHandleVec.size()),
			a_rtvHandleVec.data(),
			false,
			a_pDsvHandle
		);

		// ビューポートとシザー矩形を設定
		// ※バックバッファの大きさで張っている。画面と違う大きさのカメラ(ビューポート指定)や
		//   縮小解像度でラスタライズするパスでは合わないので、そのときはグラフ側の大きさを渡す形にすること
		m_pCmdList->RSSetViewports(1, &m_pBackBuffer->GetViewport());
		m_pCmdList->RSSetScissorRects(1, &m_pBackBuffer->GetScissorRect());
	}

	void RenderContext::ComputeBindSRVBindLess(UINT a_rootIdx, Handle<D3D12::SRV> a_srvHandle)
	{
		// シェーダー可視ヒープ上の、そのビュー自身の席を指す
		m_pCmdList->SetComputeRootDescriptorTable(a_rootIdx, m_pHeapManager->GetGPU(a_srvHandle));
	}

	void RenderContext::BindUAVBindLess(UINT a_rootIdx, Handle<D3D12::UAV> a_handle)
	{
		// シェーダー可視ヒープ上の、そのビュー自身の席を指す
		m_pCmdList->SetComputeRootDescriptorTable(a_rootIdx, m_pHeapManager->GetGPU(a_handle));
	}

	D3D12_GPU_DESCRIPTOR_HANDLE RenderContext::GetGPUHandleBindLess(Handle<D3D12::SRV> a_handle) const
	{
		return m_pHeapManager->GetGPU(a_handle);
	}

	void RenderContext::ClearRenderTarget(const Handle<Resource::Texture>& a_texHandle)
	{
		auto* _tex = (*m_pResourceManager).Ref(a_texHandle);

		// もしテクスチャのステートがレンダーターゲットでなければリターン
		if (
			_tex->GetState() != D3D12_RESOURCE_STATE_RENDER_TARGET && 
			!Resource::HasFlag(_tex->GetUsage(),Resource::ETextureUsage::RTV)
		)
		{
			return;
		}
		auto _cpu = m_pHeapManager->GetCPU(_tex->GetRTV());

		// CPUハンドルと、テクスチャ作成時のクリアバリューをセット
		D3D12::ClearRenderTargetView(m_pCmdList, _cpu, _tex->GetClearColor());
	}

	void RenderContext::ClearRenderTarget(const D3D12_CPU_DESCRIPTOR_HANDLE& a_rtvHandle, const Math::Color& a_clearColor)
	{
		D3D12::ClearRenderTargetView(m_pCmdList, a_rtvHandle, a_clearColor);
	}


	void RenderContext::ClearDSV(const Handle<D3D12::DSV>& a_DSVHandle)
	{
		auto _cpu = m_pHeapManager->GetCPU(a_DSVHandle);
		D3D12::ClearDepthStencilView(m_pCmdList,_cpu);
	}

	void RenderContext::ClearDSV(const D3D12_CPU_DESCRIPTOR_HANDLE& a_DSVHandle)
	{
		D3D12::ClearDepthStencilView(m_pCmdList,a_DSVHandle);
	}

	void RenderContext::BindBindlessHeaps()
	{
		// ビューはシェーダー可視ヒープへ作った時点で写してあるので、張るだけでよい
		ID3D12DescriptorHeap* _heaps[] = {
			m_pHeapManager->RefShaderVisibleCBVSRVUAVHeap(),
			m_pHeapManager->RefSamplerHeap()
		};
		m_pCmdList->SetDescriptorHeaps(static_cast<UINT>(std::size(_heaps)), _heaps);
	}

	void RenderContext::GraphicsBindDescriptorIndices(UINT a_rootIdx, std::span<const UINT> a_indices)
	{
		if (a_indices.empty()) return;
		m_pCmdList->SetGraphicsRoot32BitConstants(a_rootIdx, static_cast<UINT>(a_indices.size()), a_indices.data(), 0);
	}

	void RenderContext::ComputeBindDescriptorIndices(UINT a_rootIdx, std::span<const UINT> a_indices)
	{
		if (a_indices.empty()) return;
		m_pCmdList->SetComputeRoot32BitConstants(a_rootIdx, static_cast<UINT>(a_indices.size()), a_indices.data(), 0);
	}

	void RenderContext::Dispatch(UINT a_x, UINT a_y, UINT a_z)
	{
		m_pCmdList->Dispatch(a_x,a_y,a_z);
	}

	void RenderContext::DispatchMesh(UINT a_x, UINT a_y, UINT a_z)
	{
		m_pCmdList->DispatchMesh(a_x,a_y,a_z);
	}

	void RenderContext::UpdateBuffer(
		const std::vector<MeshInstanceData>& a_mesInstance,
		const std::vector<MeshMaterial>& a_mesMaterial,
		const std::vector<MeshLodData>& a_meshLod,
		const std::vector<Resource::BoneMatrix>& a_boneMatVec)
	{
		// インスタンスデータバッファ
		if (!a_mesInstance.empty())
		{
			m_meshInstanceBuffer.UpdateData(a_mesInstance.data(),a_mesInstance.size() * sizeof(MeshInstanceData));
			m_meshInstanceBuffer.Update(m_pCmdList);
		}
		// マテリアルデータバッファ
		if (!a_mesMaterial.empty())
		{
			m_meshMaterialBuffer.UpdateData(a_mesMaterial.data(),a_mesMaterial.size() * sizeof(MeshMaterial));
			m_meshMaterialBuffer.Update(m_pCmdList);
		}
		// LOD の表 : 積む側(DrawLists::AddMeshLodData)が容量に収めている
		if (!a_meshLod.empty())
		{
			m_meshLodBuffer.UpdateData(a_meshLod.data(), a_meshLod.size() * sizeof(MeshLodData));
			m_meshLodBuffer.Update(m_pCmdList);
		}

		// ボーン行列の更新
		//
		// 中身は呼び出し側(GraphicsEngine)が用意する。
		// 以前はここで SceneManager::RefWorld() =「一番上のシーン」から直接引いていたが、
		// ポーズ画面のようにシーンを重ねているとポーズ側のワールドしか載らず、
		// 後ろのゲームのキャラがボーン行列を失って一点に潰れ、消えたように見えていた。
		m_boneBuffer.ResetForNewFrame();
		if (!a_boneMatVec.empty())
		{
			m_boneBuffer.AllocateAndWrite(a_boneMatVec.data(), static_cast<UINT>(a_boneMatVec.size()));
		}

		// デバッグライン用バッファ更新
		const auto& _debugVec = m_pGraphicsEngine->GetDebugDraw()->GetLineDataVec();
		if (!_debugVec.empty())
		{
			m_debugLineBuffer.UpdateData(_debugVec.data(), _debugVec.size() * sizeof(DebugLineData));
			m_debugLineBuffer.Update(m_pCmdList);
		}
	}

	void RenderContext::UpdateUIBuffer(const std::vector<UIData>& a_uiInstanceVec)
	{
		if (a_uiInstanceVec.empty()) return;

		// 書き込みオフセットを毎フレーム先頭へ戻す。
		// BindUIBuffer() はバッファ先頭(element0)のGPUアドレスを固定でバインドするため、
		// リセットしないと AllocateAndWrite が毎フレーム後方へ書き進み、
		// シェーダーは初回フレームのデータ(element0)を読み続けてUIが動かなくなる。
		// (ボーン用 m_boneBuffer と同じ運用に揃える)
		m_uiInstanceBuffer.ResetForNewFrame();
		m_uiInstanceBuffer.AllocateAndWrite(a_uiInstanceVec);
	}

	void RenderContext::UpdateDrawInstanceIndexBuffer(const std::vector<uint32_t>& a_indexVec)
	{
		// 張るのは先頭アドレス固定なので、UI・ボーンと同じく毎フレーム先頭から書き直す
		m_drawInstanceIndexBuffer.ResetForNewFrame();
		m_drawInstanceIndexCount = 0;
		if (a_indexVec.empty()) return;

		// 容量を超えたぶんは上げない。
		// 丸ごと書き込もうとすると AllocateAndWrite が何も書かずに失敗して全部が描けなくなるので、
		// 入るところまでは描き、溢れたアイテムだけ描画ループで弾く
		const UINT _count = (std::min)(static_cast<UINT>(a_indexVec.size()), DRAW_INSTANCE_INDEX_CAPACITY);

		// 溢れる状態は毎フレーム続くので、知らせるのは最初の1回だけ
		static bool s_isOverflowReported = false;
		if (_count < a_indexVec.size() && !s_isOverflowReported)
		{
			ENGINE_ERROR("描く順のインスタンス番号の表が容量(%u)を超えました(%zu)。溢れたアイテムは描かれません",
				DRAW_INSTANCE_INDEX_CAPACITY, a_indexVec.size());
			s_isOverflowReported = true;
		}

		m_drawInstanceIndexBuffer.AllocateAndWrite(a_indexVec.data(), _count);
		m_drawInstanceIndexCount = _count;
	}

	// どちらもバインドレス : バッファの番号をルート定数で渡す
	void RenderContext::ComputeBindBonePaletteBuffer(UINT a_rootIndex)
	{
		const UINT _index = m_boneBuffer.GetSRV().GetIndex();
		ComputeBindDescriptorIndices(a_rootIndex, std::span<const UINT>(&_index, 1));
	}

	void RenderContext::BindGraphicsDebugLineBuffer(UINT a_rootIndex)
	{
		const UINT _index = m_debugLineBuffer.GetSRVHandle().GetIndex();
		GraphicsBindDescriptorIndices(a_rootIndex, std::span<const UINT>(&_index, 1));
	}

	void RenderContext::BindCamera()
	{
		if (!m_pGraphicsEngine) return;
		const auto& _cam = m_pGraphicsEngine->GetSceneView()->GetCameraData();
		GraphicsBindRootCBV(0, _cam);
	}

	void RenderContext::BindMeshInstance()
	{
		m_pCmdList->SetGraphicsRootShaderResourceView(1, m_meshInstanceBuffer.GetGPUVirtualAddress());
		m_pCmdList->SetGraphicsRootShaderResourceView(2,m_meshMaterialBuffer.GetGPUVirtualAddress());
		m_pCmdList->SetGraphicsRootShaderResourceView(MESH_ROOT_DRAW_INSTANCE_INDEX, m_drawInstanceIndexBuffer.GetGPUVirtualAddress());
		m_pCmdList->SetGraphicsRootShaderResourceView(MESH_ROOT_MESH_LOD, m_meshLodBuffer.GetGPUVirtualAddress());
	}

	void RenderContext::BindMeshlet()
	{
		auto* _pBufferManager = m_pGraphicsEngine->RefMeshBufferAllocator();
		if (!_pBufferManager)return;
		m_pCmdList->SetGraphicsRootShaderResourceView(3, _pBufferManager->RefMeshletBuffer().GetResource()->GetGPUVirtualAddress());
		m_pCmdList->SetGraphicsRootShaderResourceView(4, _pBufferManager->RefUniqueVertexIndicesBuffer().GetResource()->GetGPUVirtualAddress());
		m_pCmdList->SetGraphicsRootShaderResourceView(5, _pBufferManager->RefMeshletTriangleBuffer().GetResource()->GetGPUVirtualAddress());
		m_pCmdList->SetGraphicsRootShaderResourceView(6, _pBufferManager->GetStaticVertexBuffer().GetResource()->GetGPUVirtualAddress());
		m_pCmdList->SetGraphicsRootShaderResourceView(7, _pBufferManager->GetAnimatedVertexBuffer().GetResource()->GetGPUVirtualAddress());
		m_pCmdList->SetGraphicsRootShaderResourceView(8, _pBufferManager->RefMeshletCullDataBuffer().GetResource()->GetGPUVirtualAddress());
		// 前フレームのスキニング済みの位置(t8 = ルートパラメータ10) : モーションベクター用
		m_pCmdList->SetGraphicsRootShaderResourceView(10, _pBufferManager->GetPrevAnimatedPositionBuffer().GetResource()->GetGPUVirtualAddress());
	}

	void RenderContext::BindUIBuffer(UINT a_rootIndex, UINT a_startInstance)
	{
		// ルートSRVは「先頭要素のアドレス」を渡すだけなので、
		// 途中から張りたいときは要素ぶんバイト数を進める
		const D3D12_GPU_VIRTUAL_ADDRESS _address =
			m_uiInstanceBuffer.GetGPUVirtualAddress() +
			static_cast<UINT64>(a_startInstance) * sizeof(UIData);

		m_pCmdList->SetGraphicsRootShaderResourceView(a_rootIndex, _address);
	}

	// UAVのテクスチャを塗りつぶす
	void RenderContext::ClearUAV(
		Handle<D3D12::UAV> a_uavHandle,
		ID3D12Resource* a_pResource,
		const float a_color[4])
	{
		if (!m_pCmdList || !a_pResource) return;
		if (!a_uavHandle.IsValid()) return;

		// GPUハンドルはシェーダー可視ヒープ上の席なので、そのヒープを張っておく
		BindBindlessHeaps();

		const D3D12_GPU_DESCRIPTOR_HANDLE _gpu = m_pHeapManager->GetGPU(a_uavHandle);
		const D3D12_CPU_DESCRIPTOR_HANDLE _cpu = m_pHeapManager->GetCPU(a_uavHandle);
		if (_gpu.ptr == 0 || _cpu.ptr == 0) return;

		m_pCmdList->ClearUnorderedAccessViewFloat(_gpu, _cpu, a_pResource, a_color, 0, nullptr);
	}

	void RenderContext::DrawUI(UINT a_rootIndex)
	{
		if (!m_pDrawLists) return;
		const auto& _uiDataVec = m_pDrawLists->GetUIDataVec();
		if (_uiDataVec.empty()) return;

		auto* _pFlatPolygon   = m_pGraphicsEngine->RefQuadPolygon();
		auto* _pCurvedPolygon = m_pGraphicsEngine->RefCurvedQuadPolygon();
		if (!_pFlatPolygon || !_pCurvedPolygon) return;

		//------------------------------------------------------------------
		// 湾曲するUIだけ、横に分割した板ポリで描く
		//
		// 使う板ポリが変わる = 別のドローになるので、湾曲の有無で2回に分ける。
		// ただし配列はレイヤー順に並んでいて、UIパスは深度を持たない(積んだ順が
		// そのまま重なり)ので、「湾曲するものを全部まとめて後から描く」ことはできない。
		// 前後関係が入れ替わって、下にあるはずのUIが手前に出てしまう。
		//
		// そこで同じ種類が続く区間ごとに1回ずつ描く。並び順はそのままなので重なりは崩れず、
		// 湾曲UIが混ざっていないフレームは今までどおり1回のドローで終わる
		//------------------------------------------------------------------
		size_t _runStart = 0;
		while (_runStart < _uiDataVec.size())
		{
			const bool _isCurved = _uiDataVec[_runStart].IsCurved();

			// 同じ種類が続くところまで伸ばす
			size_t _runEnd = _runStart + 1;
			while (_runEnd < _uiDataVec.size() && _uiDataVec[_runEnd].IsCurved() == _isCurved)
			{
				++_runEnd;
			}

			// SV_InstanceID は毎回0から数え直されるので、区間の頭を先頭にして張り直す
			BindUIBuffer(a_rootIndex, static_cast<UINT>(_runStart));

			DrawPolygonInstancing(
				_isCurved ? _pCurvedPolygon : _pFlatPolygon,
				static_cast<UINT>(_runEnd - _runStart)
			);

			_runStart = _runEnd;
		}
	}

	//------------------------------------------------------------------
	// 境界面を1枚ずつ描く
	//
	// 面の情報は定数バッファで渡し、板ポリは VS が SV_VertexID から組む(6頂点)。
	// 頂点バッファを張らないので、PSO は空の入力レイアウトで作っておくこと
	//------------------------------------------------------------------
	void RenderContext::DrawBoundary(UINT a_rootIndex)
	{
		if (!m_pDrawLists) return;
		const auto& _boundaryVec = m_pDrawLists->GetBoundaryDataVec();
		if (_boundaryVec.empty()) return;

		constexpr UINT QUAD_VERTEX_COUNT = 6;	// 三角形2枚
		for (const auto& _data : _boundaryVec)
		{
			GraphicsBindRootCBV(static_cast<int>(a_rootIndex), _data);
			m_pCmdList->DrawInstanced(QUAD_VERTEX_COUNT, 1, 0, 0);
		}
	}

	void RenderContext::DrawQueueDispatchMesh(uint8_t a_passIndex)
	{
		if (!m_pDrawLists) return;

		// _firstIndex : この範囲の先頭が、描く順のインスタンス番号の表の何番目か
		UINT _firstIndex = 0;
		auto _itemVec = m_pDrawLists->GetPassItems(a_passIndex, &_firstIndex);
		DispatchMeshItems(_itemVec, _firstIndex);
	}

	void RenderContext::DrawTransparentQueueDispatchMesh(uint8_t a_passIndex)
	{
		if (!m_pDrawLists) return;

		// 並びは GraphicsEngine::Execute の中(DrawLists::SortTransparentItems)で奥から手前に決まっている
		UINT _firstIndex = 0;
		auto _itemVec = m_pDrawLists->GetTransparentPassItems(a_passIndex, &_firstIndex);
		DispatchMeshItems(_itemVec, _firstIndex);
	}

	void RenderContext::DispatchMeshItems(std::span<const LightWeightDrawItem> a_itemVec, UINT a_firstIndex)
	{
		// 直前に張ったPSOの番号。
		// 番号は16bitのどの値も実在しうるので、それより広い型の値で始める。
		// 番号と同じ幅の「無効値」で始めると、先頭のアイテムがたまたま
		// その番号だったときに「張り替え済み」と見なされ、
		// PSOを張らないまま描いてしまう
		uint32_t _lastPSO = 0xFFFFFFFFu;

		if (!m_pPipelineStateManager) return;

		const auto& _itemVec = a_itemVec;
		const UINT _firstIndex = a_firstIndex;
		if (_itemVec.empty()) return;

		// 表に上げ切れなかったアイテムは、増幅シェーダーが引く先が無いので描かない
		if (_firstIndex >= m_drawInstanceIndexCount) return;
		const size_t _drawableCount = (std::min)(
			_itemVec.size(), static_cast<size_t>(m_drawInstanceIndexCount - _firstIndex));

		//------------------------------------------------------------------------------------------
		// インスタンシング
		//
		// 同じメッシュの同じサブセットを同じPSOで描くアイテムが続く区間を、1回の DispatchMesh にまとめる。
		// X はメッシュレット数から決まるグループ数、Y は区間のアイテム数。
		// 増幅シェーダーは「土台(ルート定数) + SV_GroupID.y」で描く順のインスタンス番号の表を引き、
		// そこからアイテムごとのインスタンスデータ(行列・マテリアル・スキニング済み頂点の位置)へ辿る。
		//
		// まとめる条件にマテリアルは入れない : マテリアルの値はインスタンスデータごとに持っていて
		// (バインドレス)、区間の中で違っていても描ける。
		// メッシュとサブセットを揃えるのは、X(メッシュレット数)を区間の全員で同じにするため。
		// ソートキーの meshID は切り詰めた値なので、区切りはハンドルそのもので判定する。
		//
		// 半透明はまとめない : 奥から手前の順に描く必要があり、1回のディスパッチの中の
		// インスタンス同士の重なり順は当てにしない
		//------------------------------------------------------------------------------------------
		auto _canBatch = [](const LightWeightDrawItem& a_head, const LightWeightDrawItem& a_item)
			{
				return !a_head.isTransparent && !a_item.isTransparent
					&& a_head.GetPSOID() == a_item.GetPSOID()
					&& a_head.meshHandle == a_item.meshHandle
					&& a_head.subIndex == a_item.subIndex
					&& a_head.subsetMeshletCount == a_item.subsetMeshletCount;
			};

		size_t _runStart = 0;
		while (_runStart < _drawableCount)
		{
			const auto& _head = _itemVec[_runStart];

			// 同じものが続くところまで伸ばす
			size_t _runEnd = _runStart + 1;
			while (_runEnd < _drawableCount && _canBatch(_head, _itemVec[_runEnd]))
			{
				++_runEnd;
			}

			// メッシュシェーダー経路はインスタンスデータ側にリソースを寄せてあるため、
			// ここではメッシュ・マテリアルをバインドしない
			const uint16_t _psoID = _head.GetPSOID();
			// ----------------------------------------------------
			// PSOの切り替え
			// ----------------------------------------------------
			if (_psoID != _lastPSO)
			{
				auto* _pPSO = m_pPipelineStateManager->GetPSO(_psoID);
				if (!_pPSO)
				{
					_runStart = _runEnd;
					continue;
				}
				SetGraphicPSO(_pPSO);

				_lastPSO = _psoID;
			}

			// ----------------------------------------------------
			// ディスパッチ
			// Y は1次元あたりの上限と、X*Y の総数の上限に収まるよう分けて投げる
			// ----------------------------------------------------
			const UINT _groupX = (_head.subsetMeshletCount + 31) / 32;
			if (_groupX > 0)
			{
				const UINT _maxY = (std::min)(MAX_DISPATCH_MESH_DIM, MAX_DISPATCH_MESH_TOTAL / _groupX);
				size_t _chunkStart = _runStart;
				while (_chunkStart < _runEnd)
				{
					const UINT _groupY = static_cast<UINT>((std::min)(static_cast<size_t>(_maxY), _runEnd - _chunkStart));
					const UINT _baseIndex = _firstIndex + static_cast<UINT>(_chunkStart);

					m_pCmdList->SetGraphicsRoot32BitConstant(MESH_ROOT_BASE_INSTANCE, _baseIndex, 0);
					m_pCmdList->DispatchMesh(_groupX, _groupY, 1);

					_chunkStart += _groupY;
				}
			}

			_runStart = _runEnd;
		}
	}

	void RenderContext::ResourceCopy(ID3D12Resource* a_pSrc, ID3D12Resource* a_pDst)
	{
		m_pCmdList->CopyResource(a_pDst, a_pSrc);
	}

	void RenderContext::SetGraphicsRootSignature(ID3D12RootSignature* a_pRootSig)
	{
		m_pCmdList->SetGraphicsRootSignature(a_pRootSig);
	}

	void RenderContext::SetComputeRootSignature(ID3D12RootSignature* a_pRootSig)
	{
		m_pCmdList->SetComputeRootSignature(a_pRootSig);
	}

	void RenderContext::SetGraphicsRootSignature(const Handle<ID3D12RootSignature>& a_handle)
	{
		auto* _pPsoManager = m_pPipelineStateManager;
		if (!_pPsoManager) return;
		auto* _pRootSig = _pPsoManager->GetRootSignature(a_handle);
		if (!_pRootSig) return;
		SetGraphicsRootSignature(_pRootSig);
	}

	void RenderContext::SetComputeRootSignature(const Handle<ID3D12RootSignature>& a_handle)
	{
		auto* _pPsoManager = m_pPipelineStateManager;
		if (!_pPsoManager) return;
		auto* _pRootSig = _pPsoManager->GetRootSignature(a_handle);
		if (!_pRootSig) return;
		SetComputeRootSignature(_pRootSig);
	}



	void RenderContext::SetGraphicPSO(ID3D12PipelineState* a_pPSO)
	{
		m_pCmdList->SetPipelineState(a_pPSO);
		// プリミティブトポロジーセット
		m_pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	void RenderContext::SetComputePSO(ID3D12PipelineState* a_pPSO)
	{
		m_pCmdList->SetPipelineState(a_pPSO);
	}

	// ハンドルで張る版。
	// 8bitの添字と違って世代まで見るので、無効なものは黙って弾ける
	void RenderContext::SetGraphicPSO(const Handle<ID3D12PipelineState>& a_handle)
	{
		auto* _pPsoManager = m_pPipelineStateManager;
		if (!_pPsoManager) return;
		auto* _pPSO = _pPsoManager->GetPSO(a_handle);
		if (!_pPSO) return;
		SetGraphicPSO(_pPSO);
	}

	void RenderContext::SetComputePSO(const Handle<ID3D12PipelineState>& a_handle)
	{
		auto* _pPsoManager = m_pPipelineStateManager;
		if (!_pPsoManager) return;
		auto* _pPSO = _pPsoManager->GetPSO(a_handle);
		if (!_pPSO) return;
		SetComputePSO(_pPSO);
	}

	void RenderContext::SetPrimitive(D3D12_PRIMITIVE_TOPOLOGY a_pri)
	{
		m_pCmdList->IASetPrimitiveTopology(a_pri);
	}

	void RenderContext::DrawPolygonInstancing(UINT a_count)
	{
		DrawPolygonInstancing(m_pGraphicsEngine->RefQuadPolygon(), a_count);
	}

	void RenderContext::DrawPolygonInstancing(Resource::QuadPolygon* a_pPolygon, UINT a_count)
	{
		if (!a_pPolygon) return;

		// ポリゴンの頂点、インデックスバッファをバインド
		BindPolygonBuffers(a_pPolygon);

		// GPUインスタンシング
		m_pCmdList->DrawIndexedInstanced(
			a_pPolygon->GetIndexCount(),	// インデックス数(4頂点の1枚板なら6、分割板ならその分だけ増える)
			a_count,						// 描画するオブジェクト数(インスタンス数)
			0,
			0,
			0
		);
	}

	void RenderContext::DrawPolygonIndirect(ID3D12CommandSignature* a_pSignature, ID3D12Resource* a_pArgs, UINT64 a_argsOffset)
	{
		DrawPolygonIndirect(m_pGraphicsEngine->RefQuadPolygon(), a_pSignature, a_pArgs, a_argsOffset);
	}

	void RenderContext::DrawPolygonIndirect(
		Resource::QuadPolygon* a_pPolygon,
		ID3D12CommandSignature* a_pSignature,
		ID3D12Resource* a_pArgs,
		UINT64 a_argsOffset)
	{
		if (!a_pPolygon || !a_pSignature || !a_pArgs) return;

		// ポリゴンの頂点、インデックスバッファをバインド(直接描画と同じ)
		BindPolygonBuffers(a_pPolygon);

		// 描画引数(インデックス数・インスタンス数など)は GPU 上のバッファから読む。
		// 1回の描画につき引数1件。数のバッファは使わない(件数は CPU 側で 1 と決めてある)
		m_pCmdList->ExecuteIndirect(
			a_pSignature,
			1,				// 描画の回数(引数の件数)
			a_pArgs,
			a_argsOffset,
			nullptr,		// 件数を GPU から読むバッファ(使わない)
			0
		);
	}

	void RenderContext::BindPolygonBuffers(Resource::QuadPolygon* a_pPolygon)
	{
		const D3D12_VERTEX_BUFFER_VIEW& _vbView = a_pPolygon->GetVBView();
		const D3D12_INDEX_BUFFER_VIEW& _ibView = a_pPolygon->GetIBView();
		m_pCmdList->IASetVertexBuffers(0, 1, &_vbView);
		m_pCmdList->IASetIndexBuffer(&_ibView);
	}

	void RenderContext::Transition(
		ID3D12Resource* a_pResource,
		D3D12_RESOURCE_STATES a_before,
		D3D12_RESOURCE_STATES a_after
	)
	{
		D3D12::ResourceBarrier(
			m_pCmdList,
			a_pResource,
			a_before,
			a_after
		);
	}


	void RenderContext::DrawShape()
	{
		const auto& _debugVec = m_pGraphicsEngine->GetDebugDraw()->GetLineDataVec();
		if (_debugVec.empty()) return;
		m_pCmdList->DrawInstanced(
			136,
			static_cast<UINT>(_debugVec.size()),
			0,
			0
		);
	}

	RenderContext::RenderContext()
	{}

	RenderContext::~RenderContext()
	{}
}