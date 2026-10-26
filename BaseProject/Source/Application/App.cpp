#include "App.h"

#include "Engine/MainEngine.h"

#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Game/GameManager/GameManager.h"

#include "Engine/Graphics/Raytracing/RayEngine.h"

#include "Engine/DevTool/IDevTool.h"
namespace App
{
	//==================================================================================
	// 
	// 初回呼び出し
	// 
	//==================================================================================
	void Application::Execute()
	{
		// アプリケーション初期化
		Init();

		// メインループ（更新処理・描画処理）
		MainLoop();

		// 解放
		Release();
	}

	//==================================================================================
	// 
	// アプリケーション初期化
	// 
	//==================================================================================
	bool Application::Init()
	{
		// エンジンの初期化
		Engine::MainEngine::Instance().Init();

		// ゲームの初期化
		m_upGameManager = std::make_unique<Game::GameManager>();
		m_upGameManager->Init(Engine::MainEngine::Instance());

		return true;
	}

	void Application::Release()
	{
		// シーン解放 : 持ち主はエンジンだが、ゲームより先に片付ける(シーンのオブジェクトがゲームの記録を指している)
		if (auto* _pSceneManager = Engine::MainEngine::Instance().RefSceneManager()) _pSceneManager->Release();

		// ゲーム解放 : リソースの参照を握っているので、エンジンより先に手放す
		if (m_upGameManager)
		{
			m_upGameManager->Release();
			m_upGameManager.reset();
		}

		// エンジン解放
		Engine::MainEngine::Instance().Release();
	}

	//==================================================================================
	// 
	// メインループ
	// 
	//==================================================================================
	void Application::MainLoop()
	{
		while (true)
		{
			//===========================================================================
			// このフレームぶんの計測
			//
			// 計測は ENGINE_PROFILE_SCOPE を置くだけ。スコープを抜けた時点で
			// 結果がエディター(Profiler)へ飛ぶので、集計する EndProfileFrame よりは
			// 内側で閉じておくこと
			//===========================================================================
			{
				ENGINE_PROFILE_SCOPE("MainLoop");

				{
					ENGINE_PROFILE_SCOPE("MainLoop_Update");

					// フレーム開始
					if (!Engine::MainEngine::Instance().BeginFrame())
					{
						break;
					}

					// モード切替
					ToggleAppMode();

					// ゲームの更新
					m_upGameManager->Update(Engine::MainEngine::Instance().GetDeltaTime());
				}

				{
					ENGINE_PROFILE_SCOPE("MainLoop_Draw");

					// 描画開始
					{
						ENGINE_PROFILE_SCOPE("BeginDraw");
						Engine::MainEngine::Instance().BeginDraw();
					}

					{
						// ゲームの描画 : 描画命令(カメラ・モデル・UI・ライト)を積むだけで実行はしない。
						// BeginDraw でフレームが切り替わった後、ExecuteDrawCmd より前に呼ぶこと
						m_upGameManager->Draw();
					}

					{
						// 命令の実行
						ENGINE_PROFILE_SCOPE("RGDraw");
						Engine::MainEngine::Instance().ExecuteDrawCmd();
					}

					// 描画終了
					{
						ENGINE_PROFILE_SCOPE("EndDraw");
						Engine::MainEngine::Instance().EndDraw();
					}
				}

				// フレーム終了
				Engine::MainEngine::Instance().EndFrame();
			}

			// プロファイラのフレーム終了
			// ここで受け取った結果の集計・平均の確定・表示用の並べ替えが行われ、
			// 次フレームのパネル描画で使われる
			if (auto* _pDevTool = Engine::MainEngine::Instance().RefDevTool()) _pDevTool->EndProfileFrame();
		}
	}

	//==================================================================================
	//
	// エディターとゲームの切り替え
	//
	//----------------------------------------------------------------------------------
	// キーは InputManager が持っている(Ctrl+P / SYSTEM_ACTION_TOGGLE_APPMODE)。
	// 割り当てを変えたいときはあちらを触ること。
	//
	//   エディター    → ゲーム
	//   ゲーム        → エディター
	//   デバッグプレイ → エディター(抜ける)
	//
	//==================================================================================
	void Application::ToggleAppMode()
	{
		// エディターがモーダルな画面(エフェクトエディター)を出している間は切り替えない。
		// あちらが開いている間はゲームのシーンが止まっているので、
		// ここで切り替えると「プレイモードなのに何も動かない」状態になってしまう
		if (auto* _pDevTool = Engine::MainEngine::Instance().RefDevTool(); _pDevTool && _pDevTool->IsModalActive()) return;

		// プレイモードでなくても拾う取り方。エディターに居るときに押すため
		auto& _engine = Engine::MainEngine::Instance();

		const auto* _pInputManager = _engine.RefInputManager();
		if (!_pInputManager || !_pInputManager->IsSystemPress(
			Engine::Input::InputManager::SYSTEM_ACTION_TOGGLE_APPMODE)) return;

		// ゲームからでもデバッグプレイからでも、行き先はエディター
		const bool _isPlaying = (_engine.GetMode() != Engine::EAppMode::Editor);

		// 起動設定でエディターを切っているときは入らない(エディターからゲームへ戻るのは止めない)
		if (_isPlaying && !_engine.GetBuildConfig().isEditorEnabled) return;

		_engine.ChangeMode(_isPlaying ? Engine::EAppMode::Editor : Engine::EAppMode::Game);
	}

	Application::Application()
	{
	}

	Application::~Application()
	{
	}
}
