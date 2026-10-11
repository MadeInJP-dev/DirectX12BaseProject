#include "Engine/Graphics/GraphicsEngine.h"

#include "Engine/MainEngine.h"

// D3D関係
#include "Engine/Graphics/D3D12/DescriptorHeapManager/DescriptorHeapManager.h"

// グラフィックスエンジンの持ち物(土台)
#include "Engine/Graphics/Device/RenderDevice.h"
#include "Engine/Graphics/Device/GraphicsDevice/GraphicsDevice.h"
#include "Engine/Graphics/BackBuffer/BackBuffer.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"

// グラフィックス関係
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Graphics/Particle/ParticleBufferManager.h"
#include "Engine/Graphics/Raytracing/RayEngine.h"
#include "Engine/Graphics/Raytracing/BLASCompactor/BLASCompactor.h"
#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshBufferAllocator.h"
#include "Engine/Resource/Data/QuadPolygon/QuadPolygon.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/Graphics/Profile/GraphicsProfiler.h"

// スレッドの稼働時間の計測(Present の待ちを外す)
#include "Engine/JobSystem/Profile/ThreadProfiler.h"

// レンダリングパイプライン(パスの型情報)
#include "Engine/Graphics/RenderingPipeline/PassMetaRegistry/PassMetaRegistry.h"

// カメラに依存しない、フレームに1回のGPU処理
#include "Engine/Graphics/FrameCompute/SkinningCompute/SkinningCompute.h"
#include "Engine/Graphics/FrameCompute/UpdateBLASPass/UpdateBLASPass.h"
#include "Engine/Graphics/FrameCompute/ParticleSimulation/ParticleSimulation.h"


namespace Engine::Graphics
{
	// unique_ptr の中身が完全型として見えるここで生成・破棄を定義する
	GraphicsEngine::GraphicsEngine() = default;
	GraphicsEngine::~GraphicsEngine() = default;

	//==========================================================================================
	//
	// 土台(デバイス・ディスクリプタヒープ・バックバッファ)
	//
	//==========================================================================================
	bool GraphicsEngine::InitDevice(bool a_isDebug)
	{
		m_upRenderDevice = std::make_unique<RenderDevice>();
		return m_upRenderDevice->Init(a_isDebug);
	}

	void GraphicsEngine::ReleaseDevice()
	{
		// 間接描画のコマンドシグネチャ。
		// 実行中のコマンドリストから参照されるので Release() では捨てず、
		// GPUの完了を待ち終えたここで、デバイスより先に手放す
		m_cpDrawIndexedSignature.Reset();

		if (!m_upRenderDevice) return;

		m_upRenderDevice->Release();
		m_upRenderDevice.reset();
	}

	void GraphicsEngine::CreateBackBuffer(HWND a_hWnd, UINT a_width, UINT a_height)
	{
		// スワップチェインはファクトリから描画キューに紐づけて作り、RTVはヒープへ預ける。
		// どれも先に用意できていないと作れない
		if (!m_upRenderDevice || !m_upRenderDevice->RefDevice() || !m_upDescriptorHeapManager)
		{
			ENGINE_ERRLOG(false, "バックバッファより先にデバイスとディスクリプタヒープを用意してください");
			return;
		}

		m_upBackBuffer = std::make_unique<BackBuffer>();
		m_upBackBuffer->Create(
			m_upDescriptorHeapManager.get(),
			m_upRenderDevice->RefGraphicsDevice()->RefFactory(),
			m_upRenderDevice->RefDirectCommandQueue(),
			a_hWnd, a_width, a_height
		);
	}

	void GraphicsEngine::ReleaseBackBuffer()
	{
		if (!m_upBackBuffer) return;

		// RTVをヒープへ返すので、ヒープを捨てるより前に通すこと
		m_upBackBuffer->Release();
		m_upBackBuffer.reset();
	}

	bool GraphicsEngine::InitDescriptorHeap()
	{
		auto* _pDevice = m_upRenderDevice ? m_upRenderDevice->RefDevice() : nullptr;
		if (!_pDevice)
		{
			ENGINE_ERRLOG(false, "ディスクリプタヒープより先にデバイスを作ってください");
			return false;
		}

		// 各ビューの席数。SRVはテクスチャ1枚につき1つ取るので、ここだけ桁が違う
		constexpr UINT CBV_COUNT = 1000;
		constexpr UINT SRV_COUNT = 4000;
		constexpr UINT UAV_COUNT = 1000;
		constexpr UINT RTV_COUNT = 100;
		constexpr UINT DSV_COUNT = 10;

		m_upDescriptorHeapManager = std::make_unique<D3D12::DescriptorHeapManager>();

		if (!m_upDescriptorHeapManager->Init(
			_pDevice,
			CBV_COUNT, SRV_COUNT, UAV_COUNT, RTV_COUNT, DSV_COUNT))
		{
			return false;
		}

		// ビューの席は、GPUが使い終わるまで空きへ戻さない。
		// 「今記録しているフレームが終わるときの値」を付けて預け、BeginFrame で戻す
		RenderDevice* _pRenderDevice = m_upRenderDevice.get();
		m_upDescriptorHeapManager->SetNextFenceValueProvider(
			[_pRenderDevice]() { return _pRenderDevice->GetNextFenceValue(); });
		return true;
	}

	void GraphicsEngine::ReleaseDescriptorHeap()
	{
		if (!m_upDescriptorHeapManager) return;

		m_upDescriptorHeapManager->Release();
		m_upDescriptorHeapManager.reset();
	}

	void GraphicsEngine::Init(
		D3D12::GraphicsCommandList* a_pCmdList,
		const GraphicsEngineDesc& a_desc
	)
	{
		// リソースの持ち主 : 以降の初期化(スキニング・パーティクルのシェーダー)でも使う
		m_pResourceManager = a_desc.pResourceManager;

		// 描画解像度。バックバッファもこの大きさで作られているので、
		// 画面の大きさが要る所(カメラの既定サイズ・ジッター・UIの座標変換)はすべてこれを使う
		m_renderWidth = a_desc.width;
		m_renderHeight = a_desc.height;
		ENGINE_ERRLOG(m_pResourceManager, "GraphicsEngineDesc.pResourceManager が渡されていません");


		auto* _pDevice = m_upRenderDevice ? m_upRenderDevice->RefDevice() : nullptr;

		// パイプラインステート・ルートシグネチャ管理。
		// スキニングやパーティクルの用意、パスの組み立てが引くので最初に作る
		m_upPipelineStateManager = std::make_unique<PipelineStateManager>();
		m_upPipelineStateManager->Init(_pDevice);

		// デバッグ用ワイヤーの置き場。
		// レンダーコンテキストが毎フレーム中身を読むので、先に用意しておく
		m_upDebugDraw = std::make_unique<DebugDraw>();

		// 描画まわりのプロファイラ。持ち物を読むだけなので、どこで作っても構わない
		m_upProfiler = std::make_unique<GraphicsProfiler>(this);

		// 静的 BLAS の圧縮。メッシュを読むと頼まれるので、リソースを読み始めるより前に作る
		m_upBLASCompactor = std::make_unique<Raytracing::BLASCompactor>();
		m_upBLASCompactor->Init(_pDevice, m_upRenderDevice ? m_upRenderDevice->GetFrameManager() : nullptr);

		// レンダーコンテキストの作成
		for (int _i = 0; _i < CPU_FRAME_COUNT; ++_i)
		{
			auto _upCtx = std::make_unique<RenderContext>();

			RenderContextDesc _desc = {};
			_desc.pDevice = _pDevice;
			_desc.pHeapManager = m_upDescriptorHeapManager.get();
			_desc.pPipelineStateManager = m_upPipelineStateManager.get();
			_desc.pDrawLists = &m_drawLists;
			_desc.pBackBuffer = m_upBackBuffer.get();
			_desc.pResourceManager = m_pResourceManager;

			_desc.cbAllocatorMemSize = 32 * 1024 * 1024;
			// シーンを重ねて描くとき(ポーズ画面など)は全ワールドのボーン行列を
			// 1本のパレットへ連結するので、1ワールド分(プールの確保数)では足りない
			_desc.boneElementNum = 40000;

			_upCtx->Init(this, a_pCmdList, _desc);
			m_upRenderContextVec.push_back(std::move(_upCtx));
		}

		//------------------------------------------------------------------
		// 描画用の板ポリ
		//
		// UIもパーティクルも同じものを使い回すだけで、フレームごとに書き換えたりしない。
		// レンダーコンテキストに持たせるとフレーム数ぶん同じ頂点バッファができるので、
		// エンジンが1つずつ持って配る。
		//
		// 湾曲用は横に kCurveDivision 分割したもの。
		// 4頂点の板をいくら曲げようとしても、間に頂点が無いのでまっすぐな
		// 台形にしかならない(頂点シェーダーは頂点の位置しか動かせない)。
		// 縦は分割しない : 曲げる量はUVのx(横位置)だけで決まるので、
		// 横に割りさえすれば弧になり、縦を割っても頂点が増えるだけで形は変わらない
		//------------------------------------------------------------------
		m_upQuadPolygon = std::make_unique<Resource::QuadPolygon>();
		m_upQuadPolygon->Init(m_upDescriptorHeapManager.get());

		m_upCurvedQuadPolygon = std::make_unique<Resource::QuadPolygon>();
		m_upCurvedQuadPolygon->Init(m_upDescriptorHeapManager.get(), CURVE_DIVISION + 1, 2);

		//------------------------------------------------------------------
		// 間接描画のコマンドシグネチャ
		//
		// 引数バッファの1件 = D3D12_DRAW_INDEXED_ARGUMENTS(DrawIndexedInstanced の5引数)。
		// 描く引数だけでルート引数(定数・ビュー)は変えないので、ルートシグネチャは渡さない
		// (D3D12 の決まりで、引数が描画/Dispatch だけのときは nullptr でよい)
		//------------------------------------------------------------------
		if (_pDevice)
		{
			D3D12_INDIRECT_ARGUMENT_DESC _argDesc = {};
			_argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

			D3D12_COMMAND_SIGNATURE_DESC _sigDesc = {};
			_sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);	// 20バイト
			_sigDesc.NumArgumentDescs = 1;
			_sigDesc.pArgumentDescs = &_argDesc;
			_sigDesc.NodeMask = 0;

			if (FAILED(_pDevice->CreateCommandSignature(&_sigDesc, nullptr, IID_PPV_ARGS(m_cpDrawIndexedSignature.ReleaseAndGetAddressOf()))))
			{
				ENGINE_WARNING("[GraphicsEngine] 間接描画のコマンドシグネチャを作れませんでした");
			}
			else
			{
				m_cpDrawIndexedSignature->SetName(L"DrawIndexedSignature");
			}
		}

		// ライト
		// バッファは上限ぶんを固定確保する(FrameLightData::Create の中)。
		// ライトが増えるたびに作り直すと、GPUが読んでいる最中のリソースを解放することになる
		m_lightManager.Init();
		for (auto& _frameLight : m_frameLightDataArr)
		{
			_frameLight.Create(_pDevice, m_upDescriptorHeapManager.get());
		}

		// グラウンドフィールドの衝撃 : ライトと同じく上限ぶんを固定確保する
		for (auto& _impulseBuffer : m_groundImpulseBufferArr)
		{
			_impulseBuffer.Create(_pDevice, m_upDescriptorHeapManager.get(), MAX_GROUND_IMPULSES);
		}

		//------------------------------------------------------------------------------------
		// カメラに依存しない、フレームに1回で足りるGPU処理
		//
		// スキニング・BLAS更新・パーティクルの発生と更新は、どのカメラの描画でも同じ結果を読む
		//------------------------------------------------------------------------------------
		m_upSkinningCompute = std::make_unique<SkinningCompute>();
		m_upSkinningCompute->Setup(m_upPipelineStateManager.get(), *m_pResourceManager);
		m_upParticleSimulation = std::make_unique<ParticleSimulation>();
		m_upParticleSimulation->Setup(m_upPipelineStateManager.get(), *m_pResourceManager);

		// シーンの見え方(カメラ・画面効果・空・環境光)
		m_upSceneView = std::make_unique<SceneView>();
		m_upSceneView->Init(m_renderWidth, m_renderHeight);

		// バッファ管理クラス
		BufferSizeDesc _bufferSizeDesc = {};
		_bufferSizeDesc.staticVertexBufferSize = 20000000;
		_bufferSizeDesc.indexBufferSize = 20000000;
		_bufferSizeDesc.animatedVertexBufferSize = 5000000;
		m_upMeshBufferAllocator = std::make_unique<MeshBufferAllocator>();
		m_upMeshBufferAllocator->Init(
			_pDevice,
			m_upDescriptorHeapManager.get(),
			m_upRenderDevice->GetFrameManager(),
			a_pCmdList,
			_bufferSizeDesc
		);

		// メガバッファは使う範囲にだけ実メモリを足していく(予約リソース)。
		// 足した割り当ては、その範囲を使うリストより先にキューへ乗せる必要があるので、
		// どのキューでもリストを流す直前に流してもらう
		{
			MeshBufferAllocator* _pMeshBufferAllocator = m_upMeshBufferAllocator.get();
			m_upRenderDevice->SetBeforeExecuteHook(
				[_pMeshBufferAllocator](D3D12_COMMAND_LIST_TYPE a_type, D3D12::CommandQueue* a_pQueue)
				{
					_pMeshBufferAllocator->FlushTileMappings(a_type, a_pQueue);
				});
		}

		//------------------------------------------------------------------------------------
		// 生成できるパスの一覧を作る。
		// パイプラインアセットのロードで型IDからパスを作り直すのに使うので、
		// リソースを読み始めるより前に用意しておく必要がある
		//------------------------------------------------------------------------------------
		m_upPassMetaRegistry = std::make_unique<Pipeline::PassMetaRegistry>();
		Pipeline::RegisterBuiltinPasses(*m_upPassMetaRegistry);

		// カメラごとの描画構成
		m_upCameraPipelines = std::make_unique<CameraPipelineManager>();
		m_upCameraPipelines->Init(this);

		// 描画要求の受け口 : 上で作ったもの(DrawLists・PSO管理・メッシュバッファ・カメラ管理)を引くので最後
		m_upDrawSubmitter = std::make_unique<DrawSubmitter>();
		m_upDrawSubmitter->Init(this);

		// パーティクルバッファの生成
		m_upParticleManager = std::make_unique<Particle::ParticleBufferManager>();
		m_upParticleManager->Init(this, m_upDescriptorHeapManager.get(), a_pCmdList);

		// レイトレワールド構築
		m_upRayEngine = std::make_unique<Raytracing::RayEngine>();
		m_upRayEngine->CommitWorld(_pDevice, m_upDescriptorHeapManager.get(), a_pCmdList, m_pResourceManager);
	}

	Pipeline::PassMetaRegistry* GraphicsEngine::RefPassMetaRegistry()
	{
		return m_upPassMetaRegistry.get();
	}


	void GraphicsEngine::Release()
	{
		// 描画要求の受け口は引く先を控えているだけなので、引く先より先に捨てる
		m_upDrawSubmitter.reset();

		// カメラごとのパイプラインが抱えているGPUリソースを先に手放す。
		// DescriptorHeapManager の解放より前でないとビューが残る
		if (m_upCameraPipelines)
		{
			m_upCameraPipelines->Release();
			m_upCameraPipelines.reset();
		}

		m_upSceneView.reset();


		// レンダーコンテキスト解放
		for (auto& _ctx : m_upRenderContextVec)
		{
			_ctx->Release();
			_ctx.reset();
		}

		// ライト解放
		// プールを空にした時点で配り済みのライトハンドルはすべて無効になる
		for (auto& _frameLight : m_frameLightDataArr)
		{
			_frameLight.Release();
		}
		m_lightManager.Release();

		// グラウンドフィールドの衝撃解放
		for (auto& _impulseBuffer : m_groundImpulseBufferArr)
		{
			_impulseBuffer.Release();
		}
		m_groundImpulseCount = 0;

		// 板ポリ解放
		m_upQuadPolygon.reset();
		m_upCurvedQuadPolygon.reset();

		// デバッグ用ワイヤー解放
		m_upDebugDraw.reset();

		// プロファイラ解放
		m_upProfiler.reset();

		// ディスクリプタヒープはここでは捨てない。
		// この後に解放されるもの(パーティクル/レイトレ/バックバッファ/遅延解放キュー)が
		// まだビューを返してくるので、ReleaseDescriptorHeap() を最後に呼ぶこと


		// キューを流すたびにメガバッファを触る口を先に外す
		if (m_upRenderDevice) m_upRenderDevice->SetBeforeExecuteHook(nullptr);

		m_upMeshBufferAllocator->Release();

		// 描画要求の配列を空にする(積みかけのものが残っていても捨ててよい)
		m_drawLists.Clear();

		// パイプラインステート・ルートシグネチャ解放。
		// パスもスキニングもパーティクルも、握っているのはハンドルだけなので最後でよい
		if (m_upPipelineStateManager)
		{
			m_upPipelineStateManager->Release();
			m_upPipelineStateManager.reset();
		}

		// パーティクルのGPUバッファ解放。
		// これらはディスクリプタヒープにハンドルを持つため、
		// 必ず ReleaseDescriptorHeap より前に破棄する。
		if (m_upParticleManager)
		{
			m_upParticleManager->Release();
			m_upParticleManager.reset();
		}

		// 静的 BLAS の圧縮 : 途中のものは諦める(BLAS 自体は持ち主のメッシュが手放す)
		if (m_upBLASCompactor)
		{
			m_upBLASCompactor->Release();
			m_upBLASCompactor.reset();
		}

		// レイトレワールド(TLAS/BLAS・各種バッファ)の解放
		if (m_upRayEngine)
		{
			m_upRayEngine->Release();
			m_upRayEngine.reset();
		}
	}

	void GraphicsEngine::BeginFrame()
	{
		// フレーム番号を進め、その番号を前回使ったフレームのGPU作業が終わるまで待つ。
		// ここを抜ければ、このフレームのアロケーターとフレームごとのバッファは書き換えてよい
		{
			ENGINE_PROFILE_SCOPE("GPUFrameWait");
			m_upRenderDevice->BeginFrame();
		}

		// GPUが使い終わったビューの席を空きへ戻す(ここまでで前のフレームの完了は待ってある)
		m_upDescriptorHeapManager->ApplyReservedFrees(m_upRenderDevice->GetCompletedFenceValue());

		// 今フレームに描くバックバッファの番号を引き直す
		m_upBackBuffer->BeginFrame();

		// 今から使うレンダーコンテキスをクリア
		m_currentFrameIndex = m_upRenderDevice->GetCurrentFrameIndex();
		m_upRenderContextVec[m_currentFrameIndex]->Clear();

		// 設計図が変わったカメラの実行インスタンスを組み直し、モデルを受け取るパスの一覧を作り直す。
		// 描画アイテムを1つも積んでいない今のうちに済ませることで、
		// 配り直したパス番号とアイテムのパス番号が食い違わないようにする
		m_upCameraPipelines->BeginFrame();
	}
	void GraphicsEngine::Execute()
	{
		// ここへ来る時点で、アプリ側の描画要求(カメラ・モデル・UI・ライト)は積み終わっている。
		// 積むのは呼び出し側(Application::MainLoop の GameManager::Draw)の仕事で、
		// エンジンはゲームを知らない
		auto* _pCmdList = m_upRenderDevice->AcquireDirectCommandList();
		// GPUが実際に完了させた値 : これ以下でタグ付けされた領域だけをフリーリストに戻す
		auto _completedFence = m_upRenderDevice->GetCompletedFenceValue();

		// メッシュバッファの更新
		m_upMeshBufferAllocator->UpdateFrame(_pCmdList, _completedFence);

		// パーティクルのバッファ更新
		m_upParticleManager->UploadEmitData(_pCmdList, m_currentFrameIndex);

		// バックバッファを描き込める状態にしてクリアする
		m_upBackBuffer->TransitionToRenderTarget(_pCmdList);
		auto _cpuHandle = m_upDescriptorHeapManager->GetCPU(
			m_upBackBuffer->GetBackBuffer().GetRTV()
		);
		float _clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f }; // 背景色
		_pCmdList->ClearRenderTargetView(_cpuHandle, _clearColor, 0, nullptr);

		// レンダーコンテキストにコマンドリストをセット
		m_upRenderContextVec[m_currentFrameIndex]->SetDirectCommandList(_pCmdList);

		// GPUへ送るカメラを確定する。
		// ECS側のカメラ設定は GameManager::Draw() の中(PreDraw)で済んでいるので、
		// エディターカメラなどの割り込みはここ(GPUデータ作成の直前)で当たる
		m_upSceneView->UpdateGPUCameraData();

		// カリングを別のカメラで行っているなら、その視錐台をワイヤーで出す。
		// デバッグラインは下の UpdateBuffer で上がるので、それより前に積む
		if (m_upSceneView->IsCullCameraSeparated() && m_upDebugDraw)
		{
			m_upDebugDraw->DrawFrustum(m_upSceneView->GetCullViewProjMat(), Math::Color(1.0f, 0.8f, 0.0f, 1.0f));
		}

		// バッファの更新
		// ボーン行列は上の GameManager::Draw() で描くワールドぶんだけ積まれている。
		// 「今の一番上のシーン」から引いてはいけない(ポーズ中は後ろのゲームのボーンが載らない)
		m_upRenderContextVec[m_currentFrameIndex]->UpdateBuffer(
			m_drawLists.GetInstanceDataVec(), m_drawLists.GetMeshMaterialVec(),
			m_drawLists.GetBoneMatrixVec()
		);
		//------------------------------------------------------------------
		// UIをレイヤー順に並べ替える
		//
		// UIパスは深度を持たない(DepthEnable(false))ので、重なりを決めるのは
		// 描く順そのもの。並べ替えないとレイヤーの値がどこにも効かない。
		//------------------------------------------------------------------
		m_drawLists.SortUIByLayer();

		m_upRenderContextVec[m_currentFrameIndex]->UpdateUIBuffer(m_drawLists.GetUIDataVec());

		// ライトをGPUバッファへ詰め直す。
		// レンダーパスが引くのはこの結果なので、必ずレンダーグラフの実行より前に済ませる
		m_lightManager.BuildFrameData(m_frameLightDataArr[m_currentFrameIndex]);

		// グラウンドフィールドの衝撃をGPUバッファへ詰め直す。ライトと同じくグラフの実行より前に済ませる
		{
			auto& _impulseBuffer = m_groundImpulseBufferArr[m_currentFrameIndex];
			const auto& _impulseVec = m_upSceneView->GetGroundImpulses();
			_impulseBuffer.ResetForNewFrame();
			m_groundImpulseCount = 0;
			if (!_impulseVec.empty())
			{
				_impulseBuffer.AllocateAndWrite(_impulseVec);
				m_groundImpulseCount = static_cast<uint32_t>(_impulseVec.size());
			}
		}

		// 主光源のシャドウマップのカスケードを組む。
		// 平行光(上で詰めた先頭)とカメラ(UpdateGPUCameraData で確定)の両方が要るので、この位置。
		// シャドウマップを使わないフレームはカスケード数 0 になり、描くパスも読むパスも何もしない
		m_lightManager.BuildShadowCascades(m_upSceneView->GetCPUCameraData());

		// 半透明の並びを決める : カメラ(上書き込み)が確定したここで、カメラからの距離を入れる。
		// パスが読むのは共有のカメラなので、どのカメラのパスもこの位置を基準に並ぶ
		const auto& _cameraPos = m_upSceneView->GetCPUCameraData().pos;
		m_drawLists.ResolveTransparentSortKeys(Math::Vector3(_cameraPos.x, _cameraPos.y, _cameraPos.z));

		// 描画アイテムをソート : パスはこの並びからパス番号で自分のぶんを引く
		m_drawLists.SortItems();

		// 描く順のインスタンス番号の表を上げる : 並びはソートで決まるので、必ずこの後。
		// 各パスはこの表を「土台 + SV_GroupID.y」で引いて、同じメッシュをまとめて描く
		m_upRenderContextVec[m_currentFrameIndex]->UpdateDrawInstanceIndexBuffer(m_drawLists.GetDrawInstanceIndexVec());

		//------------------------------------------------------------------
		// カメラに依存しない毎フレームの計算
		//
		// スキニングの結果とBLASは、どのカメラの描画でも同じものを読む。
		// カメラごとに回すと同じ計算を何度も走らせることになるので、
		// パイプラインより前でまとめて1回だけ通す
		//------------------------------------------------------------------
		{
			ENGINE_PROFILE_SCOPE("GPUSkinning");
			m_upSkinningCompute->Execute(this, m_upRenderContextVec[m_currentFrameIndex].get());
		}
		{
			ENGINE_PROFILE_SCOPE("BLASUpdate");
			ExecuteUpdateBLAS(this, m_upRenderContextVec[m_currentFrameIndex].get());
		}

		// 静的 BLAS の圧縮を進める。差し替えた実体を、この後の TLAS のビルドから指す
		{
			ENGINE_PROFILE_SCOPE("BLASCompaction");
			if (m_upBLASCompactor) m_upBLASCompactor->Execute(_pCmdList);
		}

		// 発生と更新は間のUAVバリアごと1つの関数にまとめてある。
		// 分けるとバリアを挟み忘れて、空きスロットが減り続ける不具合が戻る
		{
			ENGINE_PROFILE_SCOPE("ParticleSimulation");
			m_upParticleSimulation->Execute(this, m_upRenderContextVec[m_currentFrameIndex].get());
		}

		// カメラごとの描画構成を回す。
		// 各カメラは自分の最終出力テクスチャへ描くだけで、画面へ出すのは下の PresentTo
		m_upCameraPipelines->Execute(m_upRenderContextVec[m_currentFrameIndex].get());

		// メインカメラが描いた絵をバックバッファへ載せる
		m_upCameraPipelines->PresentTo(_pCmdList);

		// パーティクルの間接描画の引数を COMMON へ戻す。
		// カメラごとの描画(上の Execute)がすべて読み終わった後、コマンドリストを流す前にやる
		// (流したあとは COMMON に戻るが、CPU 側の覚えている状態は戻らないため)
		if (m_upParticleManager)
		{
			m_upParticleManager->FinishFrame(_pCmdList);
		}

		m_upRenderDevice->SubmitDirectCommandList(_pCmdList);
		m_upRenderContextVec[m_currentFrameIndex]->SetDirectCommandList(nullptr);

		// 描画まわりの使われ方を取る(頼まれたフレームだけ)。
		// 描画要求と定数バッファの使用量がそろっていて、EndFrame で消える前のここで取る
		if (m_upProfiler) m_upProfiler->CaptureIfRequested();
	}
	void GraphicsEngine::EndFrame()
	{
		// 今フレーム積まれなかったカメラを捨てる
		m_upCameraPipelines->EndFrame();

		// 描画要求の配列をクリアしてメモリ領域を確保しておく
		m_drawLists.Clear();

		// 今フレームだけの画面効果(カメラから送られた DoF など)を落とす
		m_upSceneView->EndFrame();

		// デバッグ用配列のクリア。
		// 積む側(システム・GameObject・エンジン内部)はこのフレームの更新で入れ直す
		if (m_upDebugDraw) m_upDebugDraw->Clear();
	}

	void GraphicsEngine::Present(bool a_isVsync)
	{
		// バックバッファを表示できる状態へ落とす
		auto* _pCmdList = m_upRenderDevice->AcquireDirectCommandList();
		m_upBackBuffer->TransitionToPresent(_pCmdList);
		m_upRenderDevice->SubmitDirectCommandList(_pCmdList);

		// 積んだリストを流して、フレーム終了のシグナルを打つ
		m_upRenderDevice->EndFrame();

		// スワップチェイン切替
		// 垂直同期やキューの詰まりでここは止まることがあるので、止まっている時間として数える
		{
			Thread::ThreadStateScope _idleScope(Thread::EThreadState::Idle);
			m_upBackBuffer->Present(a_isVsync);
		}
	}


	const Graphics::RenderContext* GraphicsEngine::GetRenderContext() const
	{
		return m_upRenderContextVec[m_currentFrameIndex].get();
	}
	Graphics::RenderContext* GraphicsEngine::RefRenderContext()
	{
		return m_upRenderContextVec[m_currentFrameIndex].get();

	}
	PipelineStateManager* GraphicsEngine::RefPipelineStateManager()
	{
		return m_upPipelineStateManager.get();
	}
	D3D12::DescriptorHeapManager* GraphicsEngine::RefDescriptorHeapManager()
	{
		return m_upDescriptorHeapManager.get();
	}
	const D3D12::DescriptorHeapManager* GraphicsEngine::GetDescriptorHeapManager() const
	{
		return m_upDescriptorHeapManager.get();
	}
	LightManager* GraphicsEngine::RefLightManager()
	{
		return &m_lightManager;
	}

	const FrameLightData& GraphicsEngine::GetFrameLightData() const
	{
		return m_frameLightDataArr[m_currentFrameIndex];
	}

	const D3D12::DynamicStructuredBuffer<GroundImpulse>& GraphicsEngine::GetGroundImpulseBuffer() const
	{
		return m_groundImpulseBufferArr[m_currentFrameIndex];
	}

	void GraphicsEngine::BindPSO(Graphics::RenderContext* a_pCtx, const Handle<ID3D12PipelineState>& a_handle)
	{
		if (!a_pCtx) return;
		a_pCtx->SetGraphicPSO(a_handle);
	}

	void GraphicsEngine::BindPSO(Graphics::RenderContext* a_pCtx, uint8_t a_psoIndex)
	{
		auto* _pPSO = m_upPipelineStateManager->GetPSO(a_psoIndex);
		if (!_pPSO) return;
		a_pCtx->SetGraphicPSO(_pPSO);
	}

}