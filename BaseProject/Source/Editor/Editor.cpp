#include "Editor.h"

#include "ImGui/ImGuiContext.h"
#include "Panel/LogPanel/LogPanel.h"

#include "Profiler/Profiler.h"

#include "Engine/Graphics/D3D12/DescriptorHeapManager/DescriptorHeapManager.h"

#include "Engine/MainEngine.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"

#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/ECS/World/World.h"

#include "Panel/PanelManager.h"
#include "EditorCamera/EditorCamera.h"
#include "EffectEditor/EffectEditor.h"
#include "PrefabEditor/PrefabEditor.h"

namespace Editor
{
	MainEditor::MainEditor()
	{}
	MainEditor::~MainEditor()
	{}

	bool MainEditor::Init(HWND a_hwnd, Graphics::D3D12::DescriptorHeapManager* a_pHeapManager, ECS::EngineServices* a_pServices)
	{
		if (m_isInit) return true;

		m_isInit = true;

		// ImGui関連
		if (!m_upImGuiContext)
		{
			m_upImGuiContext = std::make_unique<ImGuiContext>();
			m_upImGuiContext->Init(a_hwnd, a_pHeapManager);
		}
		// エディター用フリーカメラ
		if (!m_upEditorCamera)
		{
			m_upEditorCamera = std::make_unique<EditorCamera>();
			m_upEditorCamera->Init(a_pServices && a_pServices->pMainEngine ? a_pServices->pMainEngine->RefGraphicsEngine() : nullptr);
		}

		// エフェクト確認用のモーダル画面
		if (!m_upEffectEditor)
		{
			m_upEffectEditor = std::make_unique<EffectEditor>(a_pServices);
		}

		// プレハブ編集用のモーダル画面
		if (!m_upPrefabEditor)
		{
			m_upPrefabEditor = std::make_unique<PrefabEditor>(a_pServices);
		}

		// プロファイラ
		// パネルより先に作る : パネルへは参照だけを渡す
		if (!m_upProfiler)
		{
			m_upProfiler = std::make_unique<Profiler>();
		}

		// パネルの登録
		if (!m_upPanelManager)
		{
			m_upPanelManager = std::make_unique<PanelManager>();
			m_upPanelManager->Init(m_upEditorCamera.get(), m_upEffectEditor.get(), m_upPrefabEditor.get(), m_upProfiler.get(), a_pServices);
		}

		// ログパネルの参照を取得しておく。
		// ログの追加はここを経由するので、パネル登録より後で引くこと
		m_pLogPanel = m_upPanelManager->RefPanel<LogPanel>();

		m_editFuncVec.clear();

		Debug::SetLogCallback(
			[this](const char* a_msg)
			{
				if (!m_pLogPanel) return;
				m_pLogPanel->AddLogRow(a_msg);
			}
		);

		//===================================================================
		// スコープ計測(ENGINE_PROFILE_SCOPE)の受け口を登録する
		//-------------------------------------------------------------------
		// 計測側(Core::Debug::TimeProfileScope)はスコープを抜けた時間を
		// コールバックへ投げるだけで、平均も並べ替えも知らない。
		// それらが欲しいのは表示するエディターなので、
		// 受け取った結果から組み立てるのはこちら(Profiler)の仕事にする。
		//
		// ログと同じで、エディターが無ければコールバックが刺さらないだけで
		// 計測箇所はそのまま素通りする。
		//
		// ワーカースレッドから飛んでくることがあるので、
		// 受け取り側は積むだけにしてある(集計はフレーム末尾)
		//===================================================================
		Debug::SetProfileCallback(
			[this](const Debug::ProfileResult& a_result)
			{
				if (!m_upProfiler) return;
				m_upProfiler->PushResult(a_result);
			}
		);
		return true;
	}
	void MainEditor::Release()
	{
		Debug::SetLogCallback(nullptr);

		// 解放したプロファイラへ計測結果が飛んでこないように、先に受け口を外す
		Debug::SetProfileCallback(nullptr);

		// パネル本体より先に参照を切っておく
		m_pLogPanel = nullptr;

		// 確認用の画面が抱えているワールドを捨てる
		if (m_upEffectEditor)
		{
			m_upEffectEditor->Release();
		}
		if (m_upPrefabEditor)
		{
			m_upPrefabEditor->Release();
		}

		m_upImGuiContext->Release();
	}
	void MainEditor::Update(float a_dt)
	{
		//===================================================================
		// デバッグプレイ中はエディターへマウスを渡さない
		//-------------------------------------------------------------------
		// あちらはエディターを出したまま遊ぶモードで、カーソルは毎フレーム
		// 画面中央へ固定されている。そのままだとカーソルが乗っている
		// パネルがずっとホバー扱いになり、撃つつもりの左クリックが
		// インスペクターのドラッグ欄を掴んで値を書き換えてしまう。
		//
		// キーボードは渡したままにしておく。抜けるための Ctrl+P を拾うのは
		// アプリ側(InputManager のシステム入力)だが、ImGui のショートカットも生かしておきたい。
		//
		// 毎フレーム入れ直しているのは、どのモードから来ても辻褄が合うようにするため
		//===================================================================
		if (ImGui::GetCurrentContext() != nullptr)
		{
			ImGuiIO& _io = ImGui::GetIO();
			if (MainEngine::Instance().GetMode() == EAppMode::DebugPlay)
			{
				_io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
			}
			else
			{
				_io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
			}
		}

		//===================================================================
		// プレイモード中はエディター操作を一切受け付けない
		//-------------------------------------------------------------------
		// プレイ中は Draw を呼ばない = ImGui::NewFrame が回らないので、
		// ImGui の入力状態(押しているボタン・MouseDelta)は最後にエディターを
		// 描いたフレームのまま止まる。そこでフリーカメラを回し続けると、
		// 「右クリックを押したまま切り替えた」状態が凍って毎フレーム同じ回転量が
		// 入り続け、画面が回りっぱなしになる。
		//
		// さらに、ウィンドウメッセージは ImGui へ流れ続けるので、プレイ中の
		// マウス移動(カーソル中央固定のため毎フレーム発生する)が入力イベントの
		// 待ち行列に溜まる。ImGui は1フレームに処理できる量が決まっているため、
		// エディターへ戻った後もその再生に何フレームもかかり、
		// 「しばらく操作を受け付けない」状態になる。
		//
		// どちらもプレイ中に「触らない・溜めない」で断てるので、ここで捨てておく。
		//===================================================================
		if (MainEngine::Instance().GetMode() == EAppMode::Game)
		{
			if (m_upEditorCamera) m_upEditorCamera->CancelControl();

			// プレイ中に届いた入力イベントは毎フレーム捨てる(溜めない)
			if (ImGui::GetCurrentContext() != nullptr)
			{
				ImGui::GetIO().ClearEventsQueue();
			}
			return;
		}

		//===================================================================
		// デバッグプレイ中はフリーカメラを動かさない
		//-------------------------------------------------------------------
		// 映しているのはゲームのカメラ(ExecuteDrawCmd は Editor のときしか
		// 割り込まない)なので、ここで回しても画は変わらない。
		// それでも止めておくのは、右クリックがゲーム側では武器の引き金だから。
		// 撃つたびにフリーカメラが見えないところで回っていると、
		// 抜けた瞬間に明後日の方向を向いている。
		//
		// Game モードと違って入力イベントは捨てない。エディターは描き続けているので
		// 毎フレーム消化されており、溜まって遅れることが無いため
		//===================================================================
		if (MainEngine::Instance().GetMode() == EAppMode::DebugPlay)
		{
			if (m_upEditorCamera) m_upEditorCamera->CancelControl();
			return;
		}

		// モーダルな画面(エフェクト / プレハブエディター)が出ている間は、そちらのカメラを回す。
		// あちらも同じ EditorCamera だが実体は別。シーンビュー側の位置を動かさないため、
		// ここでシーンビューのフリーカメラは止めておく
		if (IsModalActive())
		{
			if (m_upEditorCamera) m_upEditorCamera->CancelControl();
			if (m_upEffectEditor) m_upEffectEditor->UpdateCamera(a_dt);
			if (m_upPrefabEditor) m_upPrefabEditor->UpdateCamera(a_dt);
			return;
		}

		// フリーカメラの更新。
		// ここは ExecuteDrawCmd より前に呼ばれるので、この結果がそのフレームの描画に間に合う。
		// 参照している ImGui の入力は前フレームの NewFrame 時点のもの
		// (シーンビューのホバー状態も同じく前フレーム基準なので、ずれは生じない)。
		if (m_upEditorCamera)
		{
			m_upEditorCamera->Update(a_dt);
		}
	}

	//======================================================================================
	// モーダルな画面が出ているか
	//======================================================================================
	bool MainEditor::IsModalActive() const
	{
		return IsScenePreviewActive();
	}

	//======================================================================================
	// カメラの割り込み
	//--------------------------------------------------------------------------------------
	// エフェクト / プレハブエディターが開いているなら、そちらのカメラが最優先。
	// 描いているのがあちらの確認用ワールドなので、フリーカメラで見ても何も映らない。
	// それ以外はエディターモードのときだけフリーカメラを割り込ませる
	// (実際の上書きは GraphicsEngine::Execute() 内、ECS側のカメラ設定が終わった後)
	//
	// カリングをゲームカメラで行うのはフリーカメラのときだけ。
	// 確認用の画面は別のワールドを描いているので、ゲームカメラで間引く意味が無い
	//======================================================================================
	bool MainEditor::TryGetCameraOverride(EAppMode a_mode, Engine::DevTool::CameraOverride& a_outOverride) const
	{
		if (m_upEffectEditor && m_upEffectEditor->TryGetCameraOverride(a_outOverride.worldMat, a_outOverride.projMat))
		{
			a_outOverride.isCullByGameCamera = false;
			return true;
		}
		if (m_upPrefabEditor && m_upPrefabEditor->TryGetCameraOverride(a_outOverride.worldMat, a_outOverride.projMat))
		{
			a_outOverride.isCullByGameCamera = false;
			return true;
		}

		if (a_mode == EAppMode::Editor && m_upEditorCamera && m_upEditorCamera->IsEnable())
		{
			a_outOverride.worldMat = m_upEditorCamera->GetWorldMatrix();
			a_outOverride.projMat = m_upEditorCamera->GetProjMatrix();
			a_outOverride.isCullByGameCamera = m_upEditorCamera->IsCullByGameCamera();
			return true;
		}
		return false;
	}

	//======================================================================================
	// 確認用の画面(エフェクト / プレハブエディター)のシーン
	//--------------------------------------------------------------------------------------
	// 単体を見るための画面なので、後ろでゲームが動いていると
	// 描画も当たり判定も混ざってしまう。開いている間は SceneManager がゲームのシーンの代わりにこちらを回す。
	// どちらもモーダルなので、同時に開いていることは無い
	//======================================================================================
	bool MainEditor::IsScenePreviewActive() const
	{
		return (m_upEffectEditor && m_upEffectEditor->IsOpen()) ||
			(m_upPrefabEditor && m_upPrefabEditor->IsOpen());
	}

	void MainEditor::UpdateScenePreview(float a_deltaTime)
	{
		if (m_upEffectEditor && m_upEffectEditor->IsOpen())
		{
			m_upEffectEditor->UpdateScene(a_deltaTime);
			return;
		}
		if (m_upPrefabEditor && m_upPrefabEditor->IsOpen())
		{
			m_upPrefabEditor->UpdateScene(a_deltaTime);
		}
	}

	void MainEditor::DrawScenePreview()
	{
		if (m_upEffectEditor && m_upEffectEditor->IsOpen())
		{
			m_upEffectEditor->DrawScene();
			return;
		}
		if (m_upPrefabEditor && m_upPrefabEditor->IsOpen())
		{
			m_upPrefabEditor->DrawScene();
		}
	}

	// 背景の環境設定はエフェクトエディターだけが選べる(プレハブエディターはゲームのシーンのもの)
	const Engine::Scene::SceneAmbient* MainEditor::GetScenePreviewAmbient() const
	{
		if (m_upEffectEditor && m_upEffectEditor->IsOpen())
		{
			return m_upEffectEditor->GetPreviewAmbient();
		}
		return nullptr;
	}

	//======================================================================================
	// エディター側に残っている入力を捨てる
	//--------------------------------------------------------------------------------------
	// モードの切り替え時に呼ぶ。切り替えを跨いで押しっぱなし扱いが残らないようにする。
	//======================================================================================
	void MainEditor::ResetInput()
	{
		if (m_upEditorCamera) m_upEditorCamera->CancelControl();

		if (ImGui::GetCurrentContext() == nullptr) return;

		ImGuiIO& _io = ImGui::GetIO();

		_io.ClearEventsQueue();		// 未処理の入力イベント(切り替え前の操作)を捨てる
		_io.ClearInputKeys();		// 押しっぱなしのキーを離した扱いにする
		_io.ClearInputMouse();		// マウスのボタンと座標も同様
	}
	//======================================================================================
	// シーン切り替えの通知
	//--------------------------------------------------------------------------------------
	// 選択していたものは切り替え先には無い。次の描画が触りにいく前に捨てる
	//======================================================================================
	void MainEditor::OnSceneChanged()
	{
		if (!m_upPanelManager) return;

		m_upPanelManager->ClearSceneContext();
	}

	void MainEditor::Draw(Graphics::D3D12::GraphicsCommandList * a_pCmdList)
	{
		// ImGui描画開始
		m_upImGuiContext->Begin();

		// パネルマネージャー(ログパネルもここで描画される)
		m_upPanelManager->OnDrawPanels();

		// モーダル画面はパネルの後に出す。
		// ImGui のポップアップは「開く要求を出したときのIDスタック」に紐づくので、
		// パネルの中(インスペクター)からではなくここで開くこと
		if (m_upEffectEditor)
		{
			m_upEffectEditor->OnDrawImGui();
		}
		if (m_upPrefabEditor)
		{
			m_upPrefabEditor->OnDrawImGui();
		}

		// 各登録された関数を実行
		for (auto _func : m_editFuncVec)
		{
			if (!_func) continue;
			_func();
		}

		// ImGui描画実行
		m_upImGuiContext->End(a_pCmdList);
	}
	void MainEditor::EndProfileFrame()
	{
		if (!m_isInit || !m_upProfiler) return;
		m_upProfiler->EndFrame();
	}
	void MainEditor::RegisterEditFunc(std::function<void()> a_func)
	{
		m_editFuncVec.push_back(a_func);
	}
}
