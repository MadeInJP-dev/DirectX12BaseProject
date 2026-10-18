#include "DebugKeys.h"

#include "Engine/MainEngine.h"
#include "Engine/DevTool/IDevTool.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Input/InputCollector/InputCollector.h"
#include "Engine/Input/InputDevice/Button/InputButtonForWindows/InputButtonForWindows.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Profile/GraphicsProfiler.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/ECS/World/World.h"

#include "Application/Object/UI/Decoration/DecorationDraw.h"
#include "Application/Components/Core/PhaseTag/ActiveTag.h"
#include "Application/Components/Boid/BoidMembershipComponent.h"

#include "../UserData/UserData.h"

namespace App::Game
{
	namespace
	{
		//==================================================================================
		// キー
		//----------------------------------------------------------------------------------
		// 番号は名前から作る。ゲーム側の EGameAction(0 から順の値)とはまず重ならない
		//==================================================================================
		constexpr const char* DEVICE_NAME = "DebugKeys";

		constexpr Engine::Input::ActionID ACTION_INVINCIBLE      = Engine::Input::ToActionID("DebugKeys.Invincible");
		constexpr Engine::Input::ActionID ACTION_SKIP_WAVE       = Engine::Input::ToActionID("DebugKeys.SkipWave");
		constexpr Engine::Input::ActionID ACTION_BOSS_LOW_HEALTH = Engine::Input::ToActionID("DebugKeys.BossLowHealth");
		constexpr Engine::Input::ActionID ACTION_TIME_SCALE      = Engine::Input::ToActionID("DebugKeys.TimeScale");
		constexpr Engine::Input::ActionID ACTION_FRAME_STEP      = Engine::Input::ToActionID("DebugKeys.FrameStep");
		constexpr Engine::Input::ActionID ACTION_TOGGLE_HUD      = Engine::Input::ToActionID("DebugKeys.ToggleHUD");
		constexpr Engine::Input::ActionID ACTION_TOGGLE_STATS    = Engine::Input::ToActionID("DebugKeys.ToggleStats");
		constexpr Engine::Input::ActionID ACTION_SHIFT           = Engine::Input::ToActionID("DebugKeys.Shift");

		struct KeyBind
		{
			Engine::Input::ActionID action = 0;
			int keyCode = 0;
		};

		// 使うキーは F1 から並べる(IsReservedKey はこの範囲で判定する)
		constexpr KeyBind KEY_BINDS[] =
		{
			{ ACTION_INVINCIBLE,      VK_F1 },
			{ ACTION_SKIP_WAVE,       VK_F2 },
			{ ACTION_BOSS_LOW_HEALTH, VK_F3 },
			{ ACTION_TIME_SCALE,      VK_F4 },
			{ ACTION_FRAME_STEP,      VK_F5 },
			{ ACTION_TOGGLE_HUD,      VK_F6 },
			{ ACTION_TOGGLE_STATS,    VK_F7 },
		};
		constexpr int FIRST_KEY = VK_F1;
		constexpr int LAST_KEY  = VK_F7;

		// F4 で回す時間の倍率
		constexpr float TIME_SCALES[] = { 1.0f, 0.25f, 0.0f };

		//==================================================================================
		// 情報の表示(F7)
		//==================================================================================
		// 文字はゲームの UI と同じ経路で描く(ゲームの画面では ImGui が描かれないため)
		const Core::GUID STATS_FONT_GUID = Core::GUID("732a351f-cbc5-485b-a447-76ae733f15a1");	// LINESeedJP Bd

		constexpr float STATS_REFRESH_SEC = 0.25f;		// 数字を取り直す間隔(秒)
		constexpr float STATS_FONT_PIXEL  = 22.0f;		// 文字の高さ(px)
		constexpr float STATS_LINE_RATE   = 1.0f;		// 1行の高さ(文字の高さに対する倍率。下敷きの大きさに使う)
		constexpr float STATS_PADDING     = 10.0f;		// 下敷きの内側の余白(px)
		constexpr float STATS_WIDTH       = 260.0f;		// 下敷きの幅(px)
		constexpr Math::Vector2 STATS_POS = { 16.0f, 16.0f };	// 画面の左上からの位置(px)

		// どの UI よりも手前、マウスカーソル(10000)よりは奥
		constexpr float STATS_LAYER = 9000.0f;

		constexpr float BYTES_TO_MB = 1.0f / (1024.0f * 1024.0f);
	}

	DebugKeys::DebugKeys()
	{}

	DebugKeys::~DebugKeys()
	{}

	bool DebugKeys::IsEnabled()
	{
		return Engine::MainEngine::Instance().GetBuildMode() != Engine::EBuildConfiguration::Shipping;
	}

	bool DebugKeys::IsReservedKey(int a_keyCode)
	{
		if (a_keyCode < FIRST_KEY || a_keyCode > LAST_KEY) return false;
		return IsEnabled();
	}

	//======================================================================================
	// 初期化
	//======================================================================================
	void DebugKeys::Init(Engine::MainEngine& a_engine)
	{
		m_pEngine = &a_engine;

		RegisterKeys();

		//------------------------------------------------------------------
		// 情報の表示 : 下敷きの板と文字
		//------------------------------------------------------------------
		m_statsBackground.name  = "DebugStatsBackground";
		m_statsBackground.pivot = { 0.0f, 0.0f };
		m_statsBackground.color = { 0.0f, 0.0f, 0.0f, 0.6f };

		m_statsText.name = "DebugStatsText";
		m_statsText.SetType(Object::Decoration::EDecorationType::Text);
		m_statsText.pivot       = { 0.0f, 0.0f };
		m_statsText.offsetPos   = { STATS_PADDING, STATS_PADDING };
		m_statsText.layerOffset = 1.0f;
		if (auto* _pText = m_statsText.RefText())
		{
			_pText->text          = "";
			_pText->fontGUID      = STATS_FONT_GUID;
			_pText->fontPixelSize = STATS_FONT_PIXEL;
			_pText->textAlign     = Object::Decoration::ETextAlign::Left;
		}
		Object::Decoration::RequestResources(m_statsText, a_engine.GetEngineServices().pResourceManager);

		// エディター登録
		if (auto* _pDevTool = a_engine.RefDevTool())
			_pDevTool->RegisterEditFunc(
			[this]()
			{
				if (Engine::EditorField::WindowScope _window{ "DebugKeys" })
				{
					Edit();
				}
			}
		);
	}

	void DebugKeys::Release()
	{
		// フォントの参照を返す
		if (auto* _pText = m_statsText.RefText()) _pText->fontRef = {};

		// 止めたまま終わっても、次に作るものへ倍率を持ち越さない
		if (m_pEngine) m_pEngine->SetTimeScale(1.0f);
	}

	//======================================================================================
	// キーの登録
	//--------------------------------------------------------------------------------------
	// 操作の割り当て("Keyboard" / "Mouse")とは別のデバイスにしておく。
	// あちらは割り当てを変えるたびに作り直されるので、同じ束に入れると消えてしまう
	//======================================================================================
	void DebugKeys::RegisterKeys()
	{
		auto* _pInputManager = m_pEngine->RefInputManager();
		if (!_pInputManager) return;

		auto _upDevice = std::make_unique<Engine::Input::InputCollector>();

		for (const KeyBind& _bind : KEY_BINDS)
		{
			_upDevice->AddButton(
				_bind.action,
				std::make_unique<Engine::Input::InputButtonForWindows>(_bind.keyCode));
		}

		// Shift+F2 を見分けるため(Shift は左右どちらでもよい)
		_upDevice->AddButton(
			ACTION_SHIFT,
			std::make_unique<Engine::Input::InputButtonForWindows>(VK_SHIFT));

		_pInputManager->AddDevice(DEVICE_NAME, std::move(_upDevice));
	}

	//======================================================================================
	// 更新 : キーを見る
	//======================================================================================
	void DebugKeys::Update(UserData* a_pUserData)
	{
		m_pUserData = a_pUserData;

		const auto* _pInput = m_pEngine->RefInputManager();
		if (!_pInput) return;

		// F1 : 無敵
		if (_pInput->IsPress(ACTION_INVINCIBLE))
		{
			m_commands.isPlayerInvincible = !m_commands.isPlayerInvincible;
			ENGINE_LOG("[DebugKeys] 無敵 : %s", m_commands.isPlayerInvincible ? "ON" : "OFF");
		}

		// F2 : 次のウェーブへ / Shift+F2 : 最後のウェーブ(ボス)へ
		if (_pInput->IsPress(ACTION_SKIP_WAVE))
		{
			if (_pInput->IsHold(ACTION_SHIFT))
			{
				m_commands.isSkipToLastWaveReserved = true;
				ENGINE_LOG("[DebugKeys] 最後のウェーブへ飛ばします");
			}
			else
			{
				m_commands.isSkipWaveReserved = true;
				ENGINE_LOG("[DebugKeys] 次のウェーブへ飛ばします");
			}
		}

		// F3 : ボスの体力を残りわずかに
		if (_pInput->IsPress(ACTION_BOSS_LOW_HEALTH))
		{
			m_commands.isBossLowHealthReserved = true;
			ENGINE_LOG("[DebugKeys] ボスの体力を減らします");
		}

		// F4 : 時間の倍率
		if (_pInput->IsPress(ACTION_TIME_SCALE)) StepTimeScale();

		// F5 : コマ送り(止めているときだけ)
		if (_pInput->IsPress(ACTION_FRAME_STEP) && m_pEngine->GetTimeScale() <= 0.0f)
		{
			m_pEngine->ReserveFrameStep();
		}

		// F6 : HUD。設定画面と同じ値を切り替えるが、保存はしない(発表の後に消えたままにならないように)
		if (_pInput->IsPress(ACTION_TOGGLE_HUD) && a_pUserData)
		{
			bool& _isVisible = a_pUserData->RefGraphicsSettings().isHUDVisible;
			_isVisible = !_isVisible;
			ENGINE_LOG("[DebugKeys] HUD : %s", _isVisible ? "ON" : "OFF");
		}

		// F7 : 情報の表示。出した瞬間に数字を取る
		if (_pInput->IsPress(ACTION_TOGGLE_STATS))
		{
			m_isStatsVisible = !m_isStatsVisible;
			m_statsTimer = 0.0f;
		}
	}

	void DebugKeys::EndUpdate()
	{
		// 予約はシーンが拾い終えたので下ろす(拾う相手が居ないシーンへ持ち越さない)
		m_commands.ClearReserved();

		if (!m_isStatsVisible) return;

		m_statsTimer -= m_pEngine->GetUnscaledDeltaTime();
		if (m_statsTimer > 0.0f) return;
		m_statsTimer = STATS_REFRESH_SEC;

		RefreshStatsText();
	}

	void DebugKeys::StepTimeScale()
	{
		const float _now = m_pEngine->GetTimeScale();

		// 今の段の次へ。表に無い値(エディターで変えたなど)なら等倍へ戻す
		float _next = TIME_SCALES[0];
		constexpr size_t COUNT = std::size(TIME_SCALES);
		for (size_t _i = 0; _i < COUNT; ++_i)
		{
			if (std::abs(TIME_SCALES[_i] - _now) > 1e-4f) continue;
			_next = TIME_SCALES[(_i + 1) % COUNT];
			break;
		}

		m_pEngine->SetTimeScale(_next);
		ENGINE_LOG("[DebugKeys] 時間の倍率 : x%.2f", _next);
	}

	//======================================================================================
	// 情報の表示 : 数字を集める
	//--------------------------------------------------------------------------------------
	// 数えるのは3Dのゲームのシーン(環境設定を流しているシーン)。ポーズ画面を重ねても変わらない。
	// VRAM はグラフィックスのプロファイラに取ってもらう(頼んだフレームの終わりに取るので、
	// 出るのは前回頼んだぶん)
	//======================================================================================
	void DebugKeys::RefreshStatsText()
	{
		// ---- FPS ----
		// 1フレームの時間は FPS から出す(デルタタイムはミリ秒単位で測っているので揺れが大きい)
		const UINT _fps = m_pEngine->GetFPS();
		const float _frameMs = (_fps > 0) ? (1000.0f / static_cast<float>(_fps)) : 0.0f;

		// ---- エンティティ・ボイド ----
		uint32_t _entityCount = 0;
		uint32_t _boidCount = 0;
		if (auto* _pSceneManager = m_pEngine->RefSceneManager())
		{
			Engine::Scene::BaseScene* _pScene = _pSceneManager->RefAmbientSourceScene();
			if (!_pScene) _pScene = _pSceneManager->RefCurrentTopScene();

			if (Engine::ECS::World* _pWorld = _pScene ? _pScene->RefWorld() : nullptr)
			{
				_entityCount = _pWorld->GetAliveEntityCount();

				_pWorld->ForEach<const Component::ActiveTag, const Component::BoidMembershipComponent>(
					[&_boidCount](
						Engine::ECS::Chunk* /*a_pChunk*/,
						uint32_t a_count,
						const Component::ActiveTag* /*a_activeTagArray*/,
						const Component::BoidMembershipComponent* /*a_memberArray*/)
					{
						_boidCount += a_count;
					}
				);
			}
		}

		// ---- VRAM ----
		std::string _vramText = "--";
		if (auto* _pGE = m_pEngine->RefGraphicsEngine())
		{
			if (auto* _pProfiler = _pGE->RefProfiler())
			{
				const auto& _videoMemory = _pProfiler->GetSnapshot().videoMemory;
				if (_videoMemory.isValid)
				{
					char _buf[64] = {};
					std::snprintf(_buf, sizeof(_buf), "%.0f / %.0f MB",
						static_cast<float>(_videoMemory.localUsage) * BYTES_TO_MB,
						static_cast<float>(_videoMemory.localBudget) * BYTES_TO_MB);
					_vramText = _buf;
				}
				_pProfiler->RequestCapture();
			}
		}

		const bool _isHUDVisible = !m_pUserData || m_pUserData->GetGraphicsSettings().isHUDVisible;

		char _text[512] = {};
		std::snprintf(_text, sizeof(_text),
			"FPS : %u (%.1f ms)\n"
			"Entities : %u\n"
			"Boids : %u\n"
			"VRAM : %s\n"
			"Time : x%.2f\n"
			"Invincible : %s\n"
			"HUD : %s",
			_fps, _frameMs,
			_entityCount,
			_boidCount,
			_vramText.c_str(),
			m_pEngine->GetTimeScale(),
			m_commands.isPlayerInvincible ? "ON" : "OFF",
			_isHUDVisible ? "ON" : "OFF");

		if (auto* _pText = m_statsText.RefText()) _pText->text = _text;

		// 下敷きの大きさを行数に合わせる
		const float _lineCount = static_cast<float>(std::count(std::begin(_text), std::end(_text), '\n') + 1);
		m_statsBackground.pixelSize = {
			STATS_WIDTH,
			_lineCount * STATS_FONT_PIXEL * STATS_LINE_RATE + STATS_PADDING * 2.0f
		};
	}

	//======================================================================================
	// 情報の表示 : 積む
	//======================================================================================
	void DebugKeys::SubmitUI(Engine::Graphics::GraphicsEngine* a_pGraphicsEngine) const
	{
		if (!m_isStatsVisible || !a_pGraphicsEngine) return;

		auto* _pResourceManager = m_pEngine->GetEngineServices().pResourceManager;
		if (!_pResourceManager) return;

		Object::Decoration::ParentTransform _parentTr = {};
		_parentTr.pixelPos = STATS_POS;
		_parentTr.layer    = STATS_LAYER;

		const Object::Decoration::ParentOption _parentOp = {};

		Object::Decoration::DrawDecoration(a_pGraphicsEngine, _pResourceManager, m_statsBackground, _parentTr, _parentOp);
		Object::Decoration::DrawDecoration(a_pGraphicsEngine, _pResourceManager, m_statsText, _parentTr, _parentOp);
	}

	//======================================================================================
	// エディター
	//======================================================================================
	void DebugKeys::Edit()
	{
		Engine::EditorField::HelpText("F1 無敵 / F2 次のウェーブ(Shift+F2 でボスへ) / F3 ボスの体力を残りわずかに");
		Engine::EditorField::HelpText("F4 時間の倍率 / F5 コマ送り(倍率 0 のとき) / F6 HUD / F7 情報の表示");
		Engine::EditorField::Line();

		Engine::EditorField::Field("Invincible", m_commands.isPlayerInvincible);

		float _timeScale = m_pEngine->GetTimeScale();
		if (Engine::EditorField::Slider("Time Scale", _timeScale, 0.0f, 2.0f, "%.2f"))
		{
			m_pEngine->SetTimeScale(_timeScale);
		}

		Engine::EditorField::Field("Show Stats", m_isStatsVisible);
	}
}
