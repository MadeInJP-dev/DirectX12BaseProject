#include "MainEngine.h"

#include "Engine/DevTool/IDevTool.h"

#include "Engine/Window/NativeWindow.h"
#include "Engine/Time/TimeManager.h"
#include "Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Resource/Manager/ResourceManager/ResourceManager.h"

#include "Engine/Graphics/D3D12/DescriptorHeapManager/DescriptorHeapManager.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/BackBuffer/BackBuffer.h"

#include "Engine/Graphics/Raytracing/RayEngine.h"

#include "Engine/Graphics/Particle/ParticleBufferManager.h"


#include "Option/OptionManager.h"


#include "Audio/AudioManager.h"

#include "Input/InputManager/InputManager.h"

#include "JobSystem/JobSystem.h"
#include "JobSystem/Profile/ThreadProfiler.h"

#include "Physics/PhysicsEngine.h"

#include "ECS/Component/ComponentMetaRegistry.h"

#include "GameObject/ObjectMetaRegistry/ObjectMetaRegistry.h"

#include "Scene/SceneManager/SceneManager.h"


// DXGIのデバッグ機能(ライブオブジェクト報告)はここだけで使う。
// プリコンパイル済みヘッダーへ置くと全翻訳単位に広がるため
#pragma warning(push, 0)
#include <dxgidebug.h>
#pragma warning(pop)

namespace Engine
{
	MainEngine::MainEngine()
	{}

	MainEngine::~MainEngine()
	{}

	void MainEngine::Init()
	{
		// リソースマネージャー(とアセットデータベース)。
		// 誰よりも先に作る : ResourceRef は作られた時点からこれを見に来る
		m_upResourceManager = std::make_unique<Resource::ResourceManager>();

		// オプションマネージャーの初期化と読込
		m_upOptionManager = std::make_unique<Option::OptionManager>();
		auto& _optionManager = *m_upOptionManager;
		_optionManager.Init();
		_optionManager.Deserialize();
		const auto& _winOp = _optionManager.GetWindowOption();

		// 設定を保存
		m_appMode = EAppMode::Game;
		m_buildMode = _optionManager.GetBuildConfig().buildMode;

		// インプット初期化
		m_upInputManager = std::make_unique<Input::InputManager>();
		m_upInputManager->Init(&_optionManager.GetInputOption());
		bool _isD3DDebug = false;
		// ビルドモードによって、仕様を変更
		switch (m_buildMode)
		{
		case EBuildConfiguration::Debug:
		{
			ComPtr<ID3D12Debug> _debug;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&_debug))))
			{
				_debug->EnableDebugLayer();
			}
			// D3DデバッグON
			_isD3DDebug = true;
			ENGINE_LOG("[Option] : Debug モードでビルドされます");
			break;
		}
		case EBuildConfiguration::Development:
		{
			ENGINE_LOG("[Option] : Development モードでビルドされます");
			break;
		}
		case EBuildConfiguration::Shipping:
		{
			ENGINE_LOG("[Option] : Shipping モードでビルドされます");
			break;
		}
		default:
			break;
		}

		// ウィンドウクラスの生成
		m_upWindow = std::make_unique<Window::NativeWindow>();

		// フォーカスの出入りで入力を止める・戻す。
		// ウィンドウは入力を知らないので、作る前にここでつないでおく(作った瞬間にも届くため)
		m_upWindow->SetFocusCallback(
			[pInputManager = m_upInputManager.get()](bool a_isActive)
			{
				pInputManager->SetActive(a_isActive);
			});

		Window::WindowDesc _desc = {};
		_desc.width = static_cast<UINT>(_winOp.windowWidth);
		_desc.height = static_cast<UINT>(_winOp.windowHeight);
		_desc.titleName = Core::String::ToWideString(_winOp.windowTitle);
		_desc.className = L"AppWindow";
		_desc.windowMode = _winOp.windowMode;
		if (!m_upWindow->Create(_desc))
		{
			ENGINE_ERRLOG(false, "ウィンドウ作成失敗");
			return;
		}

		// タイムマネージャークラスの生成
		m_upTimeManager = std::make_unique<Time::TimeManager>();
		m_upTimeManager->Init();

		// 描画周りの器を先に作る。
		// デバイス・コマンドキュー・ディスクリプタヒープ・バックバッファはどれもグラフィックスエンジンの持ち物で、
		// 他のD3Dオブジェクトはすべてデバイスから作るので、何よりも先に用意する
		m_upGraphicsEngine = std::make_unique<Graphics::GraphicsEngine>();
		if (!m_upGraphicsEngine->InitDevice(_isD3DDebug))
		{
			ENGINE_ERRLOG(false, "デバイスの作成に失敗");
			return;
		}

		// 初期化中のGPU操作を積むコマンドリスト。最後に ExecuteImmediate で流す
		auto* _pCmdList = m_upGraphicsEngine->RefRenderDevice()->AcquireDirectCommandList();

		// ジョブシステム起動
		m_upJobSystem = std::make_unique<Thread::JobSystem>();
		{
			uint32_t _threadCount = std::thread::hardware_concurrency();
			_threadCount -= 5;
			m_upJobSystem->Init(_threadCount);
		}

		// 非同期ロードの実行先として登録する。
		// ResourceManager 側からエンジンのシングルトンを引かせないよう、ここで渡す
		m_upResourceManager->SetJobSystem(m_upJobSystem.get());

		// コンポーネントの型情報。中身の登録は最初のワールドを作ったとき(RegisterGameTypes)に行われる
		m_upComponentRegistry = std::make_unique<ECS::ComponentMetaRegistry>();

		// ECS外オブジェクトのクラス情報。中身の登録はゲーム側(GameManager::Init)が行う
		m_upObjectRegistry = std::make_unique<GameObject::ObjectMetaRegistry>();

		// シーンの積み替え役。ワールドの作り手はゲーム側(GameManager::Init)が差し込む。
		// プレハブの読み込みは今のワールドからコンポーネントの型を引くので、ローダーの行き先として渡す
		m_upSceneManager = std::make_unique<Scene::SceneManager>(this);
		m_upResourceManager->SetSceneManager(m_upSceneManager.get());

		// Jolt 全体(アロケータ・型の登録・JobSystem)。シーンごとの空間は CreateSceneWorld が作る。
		// Jolt のワーカーが動くのは Physics フェーズの PhysicsWorld::Update の中だけで、
		// 自前のジョブはシステムの中で待ち終わっている時間帯なので、同じ本数にしておく
		m_upPhysicsEngine = std::make_unique<Physics::PhysicsEngine>();
		{
			const int _hardwareThreads = static_cast<int>(std::thread::hardware_concurrency());
			m_upPhysicsEngine->Init((std::max)(1, _hardwareThreads - 5));
		}

		// オーディオエンジンの初期化
		m_upAudioManager = std::make_unique<Audio::AudioManager>();
		m_upAudioManager->Init(m_upResourceManager.get());

		// サウンドの読み込み(SoundIO)はオーディオエンジンから作るので、ローダーの行き先として渡す
		m_upResourceManager->SetAudioEngine(m_upAudioManager->RefAudioEngine());

		// 保存されている音量を流し込む。
		// オプションの読み込みはこれより前に済んでいるが、
		// AudioManager がまだ無い状態では入れられないのでここで反映する
		_optionManager.GetAudioOption().Apply(*m_upAudioManager);

		// アセットマネージャー作成
		InitializeAssetDatabase();

		// ディスクリプタヒープの初期化。
		// バックバッファのRTVを取るより前に用意しておく必要がある
		if (!m_upGraphicsEngine->InitDescriptorHeap())
		{
			ENGINE_ERRLOG(false, "ディスクリプタヒープマネージャーの初期化に失敗");
			return;
		}
		auto* _pHeapManager = m_upGraphicsEngine->RefDescriptorHeapManager();

		// バックバッファの生成
		// バックバッファは「描画解像度」で作る。
		// レンダーグラフのテクスチャも windowWidth/Height 基準で、
		// 最終段がバックバッファへ CopyResource するためサイズを一致させておく必要がある。
		// クライアント領域へはスワップチェインの STRETCH で伸ばされる。
		// スワップチェインは描画キューに紐づくので、InitDevice の後でないと作れない
		m_upGraphicsEngine->CreateBackBuffer(
			m_upWindow->GetWindowHandle(),
			static_cast<UINT>(_winOp.windowWidth),
			static_cast<UINT>(_winOp.windowHeight)
		);

		// 描画周り初期化(パイプラインステート管理・パーティクル・レイトレワールドもここで作られる)
		Graphics::GraphicsEngineDesc _geDesc = {};
		_geDesc.width = static_cast<UINT>(_winOp.windowWidth);
		_geDesc.height = static_cast<UINT>(_winOp.windowHeight);
		_geDesc.pResourceManager = m_upResourceManager.get();
		m_upGraphicsEngine->Init(_pCmdList,_geDesc);

		// アプリ寿命のサービス一式 : エディターもワールドもここを見る
		BuildEngineServices();

		// 開発ツール(エディター)初期化 : 差し込まれていなければツール無しで動く
		if (m_pDevTool && !m_pDevTool->Init(m_upWindow->GetWindowHandle(), _pHeapManager, m_upEngineServices.get()))
		{
			ENGINE_ERRLOG(false, "エディターの初期化に失敗");
			return;
		}

		// ダイレクトキューの実行
		m_upGraphicsEngine->RefRenderDevice()->ExecuteImmediate(_pCmdList);
	}

	void MainEngine::Release()
	{
		// 設定を保存
		m_upOptionManager->Serialize();

		// シーンはアプリ側が先に解放している(SceneManager::Release)。ここからはプレハブを読まない
		m_upResourceManager->SetSceneManager(nullptr);

		// ジョブシステムの解放は最初に行う。
		// 走っているジョブはリソースやGPUリソースを触っているため、
		// それらを解放する前に必ずワーカーを止めきること
		m_upResourceManager->SetJobSystem(nullptr);
		m_upJobSystem->Release();

		// アプリケーション・上位層の解放

		// 再生中のサウンドインスタンスを破棄。
		// SoundEffectInstance は生成元の SoundEffect(= Resource::Sound) を
		// 参照しているため、リソース解放より先に片付ける。
		m_upAudioManager->ReleaseInstances();

		// リソースの解放（Sound = DirectX::SoundEffect もここで解放される）。
		// 実体(m_upResourceManager)はここでは捨てない : MainEngine が壊れるまで残し、
		// それより後に壊れるものが持つ ResourceRef の返却先にする
		m_upResourceManager->Release();

		m_upResourceManager->RefAssetDatabase().Release();

		// オーディオエンジンの解放。
		// SoundEffect が AudioEngine を参照しているため、必ずリソース解放の後に行う。
		m_upResourceManager->SetAudioEngine(nullptr);
		m_upAudioManager->Release();

		// 開発ツール(エディター・ImGui)解放
		if (m_pDevTool) m_pDevTool->Release();

		// Jolt 全体の解放。
		// すべての PhysicsWorld が消えた後でないといけない : シーンのワールドは
		// SceneManager::Release(このRelease より前)、エフェクトエディターのプレビューは
		// 直前の開発ツールの Release で消えている
		if (m_upPhysicsEngine)
		{
			m_upPhysicsEngine->Release();
			m_upPhysicsEngine.reset();
		}

		// グラフィックスエンジンの解放（RenderContextやPSO管理・パーティクル・レイトレワールドなどが持つリソースを解放）。
		// デバイス・ディスクリプタヒープ・バックバッファはまだ捨てない :
		// この後に解放されるものがビューを返してくる
		m_upGraphicsEngine->Release();

		// 遅延解放キューを空にする
		// 全GPU作業の完了を待ってから実行し、デバイスより先にリソースを解放しきる。
		// 待つのは最後の Present まで含めて : この後バックバッファ(スワップチェイン)を捨てるので、
		// フレームのフェンス(Presentより前に打たれる)を待つだけでは足りない
		m_upGraphicsEngine->RefRenderDevice()->WaitForGPUIdle();
		for (auto& _releaseQueue : m_releaseQueues)
		{
			// 取り出してから実行する : 実行中に積み直されてもロックが二重にならない
			std::vector<std::function<void()>> _funcs = {};
			{
				std::lock_guard<std::mutex> _lock(m_releaseQueueMutex);
				_funcs.swap(_releaseQueue);
			}
			for (auto& _func : _funcs)
			{
				_func();
			}
		}

		// バックバッファのRTVを返す。
		// ディスクリプタヒープを捨てるより前でないとビューが残る
		m_upGraphicsEngine->ReleaseBackBuffer();

		// ディスクリプタヒープ解放。
		// ビューを預けていたものが全部片付いたこの位置が最後になる
		m_upGraphicsEngine->ReleaseDescriptorHeap();

		// コマンドキュー・フレーム同期・デバイスの解放。
		// 他のD3Dオブジェクトはすべてデバイスの子なので一番最後。残っているものはここで報告される
		m_upGraphicsEngine->ReleaseDevice();
		m_upGraphicsEngine.reset();

		// その他の解放
		m_upTimeManager->Release();
		m_upWindow->Release();

		// ビルドモードによって、仕様を変更
		switch (m_buildMode)
		{
		case EBuildConfiguration::Debug:
		{
			// 解放時にエラー検出（一番最後に呼ぶ）
			ComPtr<IDXGIDebug1> debug;
			if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
			{
				debug->ReportLiveObjects(
					DXGI_DEBUG_ALL,
					DXGI_DEBUG_RLO_DETAIL
				);
			}
			break;
		}
		case EBuildConfiguration::Development:
		{
			break;
		}
		case EBuildConfiguration::Shipping:
		{
			break;
		}
		default:
			break;
		}
	}

	bool MainEngine::BeginFrame()
	{
		auto& _optionManager = *m_upOptionManager;

		// フレーム開始
		m_upTimeManager->BeginFrame();

		// メッセージ処理
		if (!m_upWindow->ProcessMessage())
		{
			return false;
		}

		// タイトルにFPSを表示するかどうか
		if (_optionManager.GetWindowOption().isTitleFPS)
		{
			std::string _titleName = _optionManager.GetWindowOption().windowTitle;
			_titleName += std::string(": FPS = ") + std::to_string(m_upTimeManager->GetNowFPS());
			_titleName += std::string(": DELTATIME = ") + std::to_string(m_upTimeManager->GetDeltaTime());
			m_upWindow->ChangeTitle(_titleName);
		}

		// 入力更新
		m_upInputManager->Update();

		// オーディオ更新
		// 鳴り終わったワンショットの回収とデバイスロスト復帰を行うため、
		// 音を鳴らしていなくても毎フレーム呼ぶ必要がある
		m_upAudioManager->Update();

		// デバッグ線のオンオフ。グラフィックスエンジンはオプションを直接引かないので、ここから流し込む
		m_upGraphicsEngine->RefDebugDraw()->SetWireEnabled(_optionManager.GetDebugDrawOption().drawWire);

		m_upGraphicsEngine->RefParticleManager()->BeginFrame(m_upTimeManager->GetDeltaTime());	// パーティクルデータの更新

		m_upResourceManager->RefAssetDatabase().Update();

		return true;
	}

	EFPSRate MainEngine::GetFPSRate() const
	{
		if (m_opUserFPSRate) return m_opUserFPSRate.value();
		return m_upOptionManager->GetWindowOption().fpsRate;
	}

	void MainEngine::EndFrame()
	{
		// フレーム終了
		m_upTimeManager->EndFrame(GetFPSRate());

		// スレッドごとの稼働時間を1フレームぶん締める。
		// FPS制限の待ちまで含めた位置で区切るので、フレーム時間とずれない
		if (m_upJobSystem)
		{
			if (auto* _pThreadProfiler = m_upJobSystem->RefThreadProfiler())
			{
				_pThreadProfiler->EndFrame();
			}
		}
	}

	void MainEngine::BeginDraw()
	{
		// エディターの更新を入れる
		if (m_pDevTool) m_pDevTool->Update(GetDeltaTime());

		// 描画開始 : ここでフレームインデックスが更新され、そのフレームのGPU完了を待機する
		{
			ENGINE_PROFILE_SCOPE("GraphicsBeginFrame");
			m_upGraphicsEngine->BeginFrame();
		}

		// 今から使うフレームに登録されているファンクションを実行して空にする
		// BeginFrameの待機を終えた後に実行することで、このインデックスを前回使ったフレームの
		// GPU作業が完了していることが保証される
		UINT _currentFrameIdx = m_upGraphicsEngine->RefRenderDevice()->GetCurrentFrameIndex();
		{
			// 取り出してから実行する : 実行中に積み直されてもロックが二重にならない
			std::vector<std::function<void()>> _funcs = {};
			{
				std::lock_guard<std::mutex> _lock(m_releaseQueueMutex);
				_funcs.swap(m_releaseQueues[_currentFrameIdx]);
			}
			for (auto& _func : _funcs)
			{
				_func();
			}
		}

		// レイワールドインスタンスのクリア
		m_upGraphicsEngine->RefRayEngine()->EndFrame();
	}

	void MainEngine::EndDraw()
	{
		{
			ENGINE_PROFILE_SCOPE("EditorPhase");

			// ゲームモード以外の処理
			if (m_pDevTool && m_appMode != EAppMode::Game)
			{
				auto* _pCmdList = m_upGraphicsEngine->RefRenderDevice()->AcquireDirectCommandList();
				auto* _pHeapManager = m_upGraphicsEngine->RefDescriptorHeapManager();
				const auto* _pBackBuffer = m_upGraphicsEngine->GetBackBuffer();

				// ディスクリプタヒープをセット
				ID3D12DescriptorHeap* _heaps[] = {
						_pHeapManager->GetImGuiHeap()
				};
				_pCmdList->SetDescriptorHeaps(std::size(_heaps), _heaps);

				// 現在のフレームのレンダーターゲットビューのディスクリプタヒープの開始アドレスを取得
				auto _cpuHandle = _pHeapManager->GetCPU(
					_pBackBuffer->GetBackBuffer().GetRTV()
				);

				// レンダーターゲットを設定
				_pCmdList->OMSetRenderTargets(
					1,
					&_cpuHandle,
					FALSE,
					nullptr
				);

				// 新しいリストにビューポートとシザー矩形もセットする
				// ビューポートとシザー矩形を設定
				_pCmdList->RSSetViewports(1, &_pBackBuffer->GetViewport());
				_pCmdList->RSSetScissorRects(1, &_pBackBuffer->GetScissorRect());

				// エディター描画
				m_pDevTool->Draw(_pCmdList);
				m_upGraphicsEngine->RefRenderDevice()->SubmitDirectCommandList(_pCmdList);
			}

			m_upGraphicsEngine->EndFrame();
		}	// EditorPhase

		{
			ENGINE_PROFILE_SCOPE("EndFramePhase");

			// 描画終了 : バックバッファを表示できる状態へ落として画面を切り替える
			m_upGraphicsEngine->Present(GetFPSRate() == EFPSRate::VSync);
		}
	}

	UINT MainEngine::GetFPS() const
	{
		return m_upTimeManager->GetNowFPS();
	}

	float MainEngine::GetDeltaTime() const
	{
		return m_upTimeManager->GetDeltaTime();
	}

	//======================================================================================
	// アプリのモード切り替え
	//--------------------------------------------------------------------------------------
	// 切り替えを跨いで入力を持ち越さない。押しっぱなしのキーやマウスの移動量が残ると、
	//   ゲーム側 : 切り替え前の移動入力のままプレイヤーが走り出す
	//   エディタ側: 右クリック押しっぱなし扱いでフリーカメラが回りっぱなしになる
	// といった形で出る。どちらも「切り替えた瞬間に一度捨てる」だけで断てる。
	//======================================================================================
	void MainEngine::ChangeMode(EAppMode a_mode)
	{
		// 同じモードを指定し続けても毎フレーム入力を捨てないようにする
		// (モード切替はキーを押している間ずっと呼ばれる作りのため)
		if (m_appMode == a_mode) return;

		m_appMode = a_mode;

		if (m_pDevTool) m_pDevTool->ResetInput();
		m_upInputManager->ResetInput();
	}
	void MainEngine::ExecuteDrawCmd()
	{
		// 開発ツールが見せたいカメラ(エフェクトの確認・エディターのフリーカメラ)があれば割り込ませる。
		// 実際の上書きは GraphicsEngine::Execute() 内、ECS側のカメラ設定が終わった後
		DevTool::CameraOverride _camOverride = {};
		if (m_pDevTool && m_pDevTool->TryGetCameraOverride(m_appMode, _camOverride))
		{
			m_upGraphicsEngine->RefSceneView()->SetCameraOverride(
				_camOverride.worldMat, _camOverride.projMat, _camOverride.isCullByGameCamera);
		}
		// 割り込みが無ければECSのカメラをそのまま使う
		else
		{
			m_upGraphicsEngine->RefSceneView()->ClearCameraOverride();
		}

		// 描画の設定はここ(オプションの持ち主を知っている側)から流し込む。
		// グラフィックスエンジンはオプションを直接引かない
		m_upGraphicsEngine->RefSceneView()->SetJitterEnabled(
			m_upOptionManager->GetRenderingOption().useJitter);

		m_upGraphicsEngine->Execute();
	}
	const Window::NativeWindow* MainEngine::GetNativeWindow() const
	{
		return m_upWindow.get();
	}
	Window::NativeWindow* MainEngine::RefNativeWindow()
	{
		return m_upWindow.get();
	}
	Graphics::GraphicsEngine* MainEngine::RefGraphicsEngine()
	{
		return m_upGraphicsEngine.get();
	}
	const Graphics::RenderContext* MainEngine::GetRenderContext() const
	{
		return m_upGraphicsEngine->GetRenderContext();
	}
	Graphics::RenderContext* MainEngine::RefRenderContext()
	{
		return m_upGraphicsEngine->RefRenderContext();
	}
	Thread::JobSystem* MainEngine::RefJobSystem()
	{
		return m_upJobSystem.get();
	}
	//======================================================================================
	// アプリ寿命のサービス一式を組む
	//
	// 中身はすべて MainEngine か、その持ち物(GraphicsEngine など)が持っている実体。
	// 以前はワールドを作るたびに CreateSceneWorld が同じものを組んでいたが、
	// エディターからも同じものを見たいので正本をここへ移した
	//======================================================================================
	void MainEngine::BuildEngineServices()
	{
		if (!m_upEngineServices) m_upEngineServices = std::make_unique<ECS::EngineServices>();

		auto& _resourceManager = *m_upResourceManager;

		ECS::EngineServices& _services = *m_upEngineServices;
		_services.pMainEngine		= this;
		_services.pResourceManager	= &_resourceManager;
		_services.pAssetDatabase	= &_resourceManager.RefAssetDatabase();
		_services.pInputManager		= m_upInputManager.get();
		_services.pRayEngine		= m_upGraphicsEngine ? m_upGraphicsEngine->RefRayEngine() : nullptr;
		_services.pAudioManager		= m_upAudioManager.get();
		_services.pJobSystem		= m_upJobSystem.get();
		_services.pPhysicsEngine	= m_upPhysicsEngine.get();
		_services.pOptionManager	= m_upOptionManager.get();
		_services.pDebugDraw		= m_upGraphicsEngine ? m_upGraphicsEngine->RefDebugDraw() : nullptr;
		_services.pObjectRegistry	= m_upObjectRegistry.get();
		_services.pSceneManager		= m_upSceneManager.get();
	}

	void MainEngine::ReserveRelease(std::function<void()> a_releaseFunc)
	{
		// グラフィックスエンジンが無い(起動前・終了後)ときは、どの枠でもよいので先頭へ積む
		const UINT _frameIdx = m_upGraphicsEngine ? m_upGraphicsEngine->RefRenderDevice()->GetCurrentFrameIndex() : 0;

		std::lock_guard<std::mutex> _lock(m_releaseQueueMutex);
		m_releaseQueues[_frameIdx].push_back(std::move(a_releaseFunc));
	}
	void MainEngine::InitializeAssetDatabase()
	{
		// 持ち主はリソースマネージャー
		auto& _assetDB = m_upResourceManager->RefAssetDatabase();

		_assetDB.Init(
			"Asset/",			// クロールフォルダ指定
			".assetmeta"		// 作成拡張子
		);
		
		// ---- 対応する拡張子を登録 ----		
		// モデル
		Resource::TypeExtension _modelExt = {};
		_modelExt.type = "Model";
		_modelExt.typeExt.push_back(".obmdl");
		_modelExt.typeExt.push_back(".ojmdl");
		_modelExt.AddExtensions(".gltf");
		_modelExt.AddExtensions(".fbx");
		_modelExt.AddExtensions(".obj");
		_assetDB.AddSupporedExtensions(_modelExt);
		// メッシュ
		Resource::TypeExtension _meshExt = {};
		_meshExt.type = "Mesh";
		_meshExt.typeExt.push_back(".obmesh");
		_meshExt.typeExt.push_back(".ojmesh");
		_assetDB.AddSupporedExtensions(_meshExt);
		// マテリアル
		Resource::TypeExtension _materialExt = {};
		_materialExt.type = "Material";
		_materialExt.typeExt.push_back(".obmtrl");
		_materialExt.typeExt.push_back(".ojmtrl");
		_assetDB.AddSupporedExtensions(_materialExt);
		// アニメーション
		Resource::TypeExtension _animationExt = {};
		_animationExt.type = "Animation";
		_animationExt.typeExt.push_back(".obanim");
		_animationExt.typeExt.push_back(".ojanim");
		_assetDB.AddSupporedExtensions(_animationExt);
		// アニメーター(アニメ用ステートマシン)
		Resource::TypeExtension _stateExt = {};
		_stateExt.type = "AnimatorAsset";
		_stateExt.typeExt.push_back(".obstet");
		_stateExt.typeExt.push_back(".ojstet");
		_assetDB.AddSupporedExtensions(_stateExt);
		// パーティクル
		Resource::TypeExtension _particExt = {};
		_particExt.type = "ParticlesAsset";
		_particExt.typeExt.push_back(".obptic");
		_particExt.typeExt.push_back(".ojptic");
		_assetDB.AddSupporedExtensions(_particExt);
		// テクスチャ
		Resource::TypeExtension _texExt = {};
		_texExt.type = "Texture";
		_texExt.AddExtensions(".png");
		_texExt.AddExtensions(".jpg");
		_texExt.AddExtensions(".tag");
		_texExt.AddExtensions(".dds");
		_assetDB.AddSupporedExtensions(_texExt);
		// シェーダー
		Resource::TypeExtension _shaderExt = {};
		_shaderExt.type = "Shader";
		_shaderExt.AddExtensions(".hlsl");
		_shaderExt.AddExtensions(".cso");
		_assetDB.AddSupporedExtensions(_shaderExt);
		// シーン
		Resource::TypeExtension _sceneExt = {};
		_sceneExt.type = "Scene";
		_sceneExt.AddExtensions(".ojscene");
		_sceneExt.AddExtensions(".obscene");
		_assetDB.AddSupporedExtensions(_sceneExt);
		// プレハブ
		Resource::TypeExtension _prfb = {};
		_prfb.type = "Prefab";
		_prfb.AddExtensions(".ojprfb");
		_prfb.AddExtensions(".obprfb");
		_assetDB.AddSupporedExtensions(_prfb);
		// エフェクトプレハブ(炊いたら時間で消える大きな演出)
		Resource::TypeExtension _effectPrefab = {};
		_effectPrefab.type = "EffectPrefab";
		_effectPrefab.AddExtensions(".ojefprfb");
		_effectPrefab.AddExtensions(".obefprfb");
		_assetDB.AddSupporedExtensions(_effectPrefab);
		// サウンド
		Resource::TypeExtension _sound = {};
		_sound.type = "Sound";
		_sound.AddExtensions(".wav");
		_assetDB.AddSupporedExtensions(_sound);
		// オーディオビヘイビア(始動/継続/終了の音をまとめたもの)
		Resource::TypeExtension _audioBehavior = {};
		_audioBehavior.type = "AudioBehavior";
		_audioBehavior.AddExtensions(".ojaudbhv");
		_audioBehavior.AddExtensions(".obaudbhv");
		_assetDB.AddSupporedExtensions(_audioBehavior);
		// エフェクト(パーティクル+メッシュをまとめたもの)
		Resource::TypeExtension _effect = {};
		_effect.type = "EffectAsset";
		_effect.AddExtensions(".ojeffect");
		_effect.AddExtensions(".obeffect");
		_assetDB.AddSupporedExtensions(_effect);
		// レンダリングパイプライン(レンダーグラフの設計図)
		Resource::TypeExtension _renderingPipeline = {};
		_renderingPipeline.type = "RenderingPipelineAsset";
		_renderingPipeline.AddExtensions(".ojrpipe");
		_renderingPipeline.AddExtensions(".obrpipe");
		_assetDB.AddSupporedExtensions(_renderingPipeline);
		// フォント : 変換を挟まず .ttf などをそのまま読む
		Resource::TypeExtension _font = {};
		_font.type = "Font";
		_font.AddExtensions(".ttf");
		_font.AddExtensions(".otf");
		_font.AddExtensions(".ttc");
		_assetDB.AddSupporedExtensions(_font);
		// 全アセットに一括でメタファイル作成
		// すでにあれば無視
		_assetDB.CreateMetaFileForAllAssets();

		// ランタイムデータ作成
		_assetDB.CreateRuntimeData();
	}
}