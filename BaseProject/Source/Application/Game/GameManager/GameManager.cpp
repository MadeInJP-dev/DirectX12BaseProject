#include "GameManager.h"

#include "Engine/DevTool/IDevTool.h"

// エンジン
#include "../../../Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"

// シーン関係
#include "../../../Engine/Scene/SceneManager/SceneManager.h"

// ECS関係(ゲーム用のワールド)
#include "../../ECS/World/APPWorld.h"
#include "../../InstanceResource/GameDataResource.h"
#include "../../InstanceResource/UserDataResource.h"

// ECS外オブジェクト(クラスメタマネージャー / 登録するクラス)
#include "../../../Engine/GameObject/ObjectMetaRegistry/ObjectMetaRegistry.h"
#include "Application/Object/UI/HUD/CombatReticleHUD/CombatReticleHUD.h"
#include "Application/Object/UI/HUD/TargetBoxHUD/TargetBoxHUD.h"
#include "Application/Object/UI/HUD/AimReticleHUD/AimReticleHUD.h"
#include "Application/Object/UI/HUD/HitEffectHUD/HitEffectHUD.h"
#include "Application/Object/UI/HUD/ScoreHUD/ScoreHUD.h"
#include "Application/Object/UI/HUD/WaveAnnounceHUD/WaveAnnounceHUD.h"
#include "Application/Object/Sequence/ResultSequence/ResultSequence.h"
#include "Application/Object/UI/HUD/MissileLockBoxHUD/MissileLockBoxHUD.h"
#include "Application/Object/UI/Widget/UIButton/UIButton.h"
#include "Application/Object/UI/Widget/UIImage/UIImage.h"
#include "Application/Object/UI/Widget/UIPanel/UIPanel.h"
#include "Application/Object/UI/Widget/UIGauge/UIGauge.h"
#include "Application/Object/UI/Option/OptionHUD.h"
#include "Application/Object/UI/Widget/SideMenu/SideMenu.h"
#include "Application/Object/Sequence/TitleSequence/TitleSequence.h"
#include "Application/Object/Sequence/LoadingSequence/LoadingSequence.h"
#include "Application/Object/Sequence/HomeSequence/HomeSequence.h"
#include "Application/Object/Sequence/PauseSequence/PauseSequence.h"
#include "Application/Object/Sequence/MissionSelect/MissionSelect.h"
#include "../../Object/Scene/AmbientDustObject/AmbientDustObject.h"
#include "../../Object/Scene/PlayerSpawner/PlayerSpawner.h"
#include "Application/Object/Sequence/SceneSequence/SceneSequence.h"
#include "Application/Object/Controller/SwarmBossController/SwarmBossController.h"

// App
#include "../UserData/UserData.h"
#include "../InputActions/InputManager/InputActionManager.h"
#include "../MouseCursor/MouseCursor.h"

// エディター

namespace App::Game
{
	//======================================================================================
	// ゲーム設定ファイルの置き場所
	//
	// 持っているのは「起動時に立ち上げるシーン」だけ。アセットではないので
	// AssetDatabase には載せず、エンジン設定(EngineData)と同じく Asset/Data の下へ直接置く。
	//======================================================================================
	namespace
	{
		constexpr const char* GAME_SETTING_DIR  = "Asset/Data/Game";
		constexpr const char* GAME_SETTING_NAME = "GameData";
		constexpr const char* GAME_SETTING_EXT  = "gmdt";
	}

	void App::Game::GameManager::Init(Engine::MainEngine& a_engine)
	{
		m_pEngine = &a_engine;

		// ゲーム設定(起動時に立ち上げるシーン)の読み込み
		LoadGameSetting();

		// ユーザーデータの復元
		if (!m_upUserData)
		{
			m_upUserData = std::make_unique<UserData>();
			m_upUserData->Load();

			// プレイヤーの設定(音量・画面・FPS・視点)をエンジンへ入れる。
			// エンジン設定は書き換えず、作り手の値の上へ重ねる
			m_upUserData->ApplyAll(*m_pEngine);
		}

		// 入力設定の復元
		if (!m_upInputActionManager)
		{
			m_upInputActionManager = std::make_unique<Input::InputActionManager>();
			m_upInputActionManager->Init(m_upUserData.get(), m_pEngine->RefInputManager());
		}

		// マウスカーソル
		if (!m_upMouseCursor)
		{
			m_upMouseCursor = std::make_unique<MouseCursor>();
			m_upMouseCursor->Init(&m_pEngine->GetEngineServices());
		}

		// ------------------------------------------------------------------
		// ECS外オブジェクト(GameObject)のクラスをメタマネージャーへ登録する。
		// 登録したクラスはシーンの保存/読み込みと、エディターの AddObject 一覧で使われる。
		// 新しいオブジェクトクラスを追加したら、ここに RegisterType を足すこと。
		//
		// タイプIDは第2引数の「登録名」のハッシュなので、どこへ足しても、
		// 並べ替えても既存シーンには影響しない。逆に登録名を変えるとIDが変わるため、
		// 名前を変えたいときは MigrateName で旧名を引き継がせること。
		// ------------------------------------------------------------------
		{
			auto& _objRegistry = *m_pEngine->GetEngineServices().pObjectRegistry;
			_objRegistry.RegisterType<App::Object::CombatReticleHUD>("CombatReticleHUD");
			_objRegistry.RegisterType<App::Object::TargetBoxHUD>("TargetBoxHUD");
			_objRegistry.RegisterType<App::Object::SceneSequence>("SceneSequence");
			_objRegistry.RegisterType<App::Object::AimReticleHUD>("AimReticleHUD");
			_objRegistry.RegisterType<App::Object::HitEffectHUD>("HitEffectHUD");
			_objRegistry.RegisterType<App::Object::MissileLockBoxHUD>("MissileLockBoxHUD");
			_objRegistry.RegisterType<App::Object::UIButton>("UIButton");						// 押せるUI。押されて何をするかは SetOnClick で外から差し込む
			_objRegistry.RegisterType<App::Object::UIPanel>("UIPanel");							// UIをまとめて出し入れする入れ物。下に置いたUIはパネルの表示に従う
			_objRegistry.RegisterType<App::Object::UIImage>("UIImage");							// 置くだけの画像(タイトルの背景など)
			_objRegistry.RegisterType<App::Object::TitleSequence>("TitleSequence");				// タイトル画面の進行役。ボタンへ「押されたらシーンを切り替える」を差し込む
			_objRegistry.RegisterType<App::Object::AmbientDustObject>("AmbientDustObject");		// カメラに追従する空間のチリ。環境光・フォグ・空はシーン(SceneAmbient)の持ち物
			_objRegistry.RegisterType<App::Object::ScoreHUD>("ScoreHUD");						// スコアの表示。数える側(ScoreSystem)とは分かれていて、ここは出すだけ
			_objRegistry.RegisterType<App::Object::ResultSequence>("ResultSequence");			// リザルト画面の進行役。ホームのボタンへ「押されたらタイトルへ」を差し込む
			_objRegistry.RegisterType<App::Object::HomeSequence>("HomeSequence");				// ホーム画面の進行役。ステージセレクト(一覧・詳細・出撃)と倉庫のボタンを束ねる
			_objRegistry.RegisterType<App::Object::PauseSequence>("PauseSequence");				// ポーズ画面の進行役。重ねたシーンを閉じる側(重ねるのは SceneSequence)
			_objRegistry.RegisterType<App::Object::MissionSelect>("MissionSelect");				// ミッションセレクト。ホームから出し入れされ、選ぶと確認ボックスを出して出撃する
			_objRegistry.RegisterType<App::Object::UIGauge>("UIGauge");							// ゲージ(HP / オーバーヒート / ブーストなど)。値は SetValue で外から入れる
			_objRegistry.RegisterType<App::Object::WaveAnnounceHUD>("WaveAnnounceHUD");			// ウェーブが出た合図(何番目かの表示と音)
			_objRegistry.RegisterType<App::Object::SwarmBossController>("SwarmBossController");	// 群れのボス。リーダー→小隊長→ボイドを生成して束ねる
			_objRegistry.RegisterType<App::Object::LoadingSequence>("LoadingSequence");			// ロード画面の進行役。読み込みの進み具合をゲージへ流す
			_objRegistry.RegisterType<App::Object::PlayerSpawner>("PlayerSpawner");				// プレイヤーのスポナー。シーンの始まりにプレハブから出し、カメラの追従先へつなぐ
			_objRegistry.RegisterType<App::Object::OptionHUD>("OptionHUD");						// 設定画面。専用シーンに置き、開く側が Push する。パネル(ゲーム・グラフィック・サウンド・入力)を抱え、選んだ1つをスクロールして出す
			_objRegistry.RegisterType<App::Object::SideMenu>("SideMenu");						// カーソルを乗せると開く横のメニュー(ホームの左上)。開閉だけを持ち、中のボタンは進行役が差し込む
		}

		// ------------------------------------------------------------------
		// シーンが持つワールドの作り手を差し込む。
		//
		// エンジンは基盤の Engine::ECS::World としてしか触らないので、
		// 「どの種類のワールドを立てるか」はゲーム側のここが決める。
		// 何を登録するかは App::ECS::APPWorld::RegisterGameTypes が持っている
		// (中身は Application/ECS/World/WorldTypeRegister.cpp)。
		//
		// シーンをまたぐ記録(m_gameData)の入口もここで各ワールドへ置く。
		// 使う側(システム・オブジェクト)はワールドから GameDataResource で引く。
		// ユーザーデータ(設定画面が書き換えるもの)も同じく UserDataResource で引かせる
		// ------------------------------------------------------------------
		m_pEngine->RefSceneManager()->SetWorldFactory(
			[pGameData = &m_gameData,
			 pUserData = m_upUserData.get(),
			 pInputActionManager = m_upInputActionManager.get()]() -> std::unique_ptr<Engine::ECS::World>
			{
				auto _upWorld = std::make_unique<App::ECS::APPWorld>();
				_upWorld->AddResource<App::InstanceResource::GameDataResource>();
				_upWorld->RefResource<App::InstanceResource::GameDataResource>().pGameData = pGameData;

				_upWorld->AddResource<App::InstanceResource::UserDataResource>();
				auto& _userDataResource = _upWorld->RefResource<App::InstanceResource::UserDataResource>();
				_userDataResource.pUserData = pUserData;
				_userDataResource.pInputActionManager = pInputActionManager;
				return _upWorld;
			}
		);

		// ------------------------------------------------------------------
		// ロード画面
		//
		// スタックの外で常駐させ、シーンの読み込みが長引いたときだけ重ねて出す。
		// ワールドを作るのでワールドの作り手を差し込んだ後に置くこと。
		// 最初のシーンより先に渡しておくと、最初のシーンの読み込みにも間に合いやすい
		// ------------------------------------------------------------------
		m_pEngine->RefSceneManager()->SetLoadingScreen(
			*m_pEngine->GetEngineServices().pResourceManager, m_loadingScene);

		// 最初のシーンを挿入
		if (m_farstScene.IsValid())
		{
			m_pEngine->RefSceneManager()->ReserveChangeScene(m_farstScene, Engine::Scene::ESceneChangeType::Push);
		}
		else
		{
			ENGINE_ERRLOG(false,"初めのシーンが設定されていません");
		}

		// エディター関数登録
		if (auto* _pDevTool = m_pEngine->RefDevTool())
			_pDevTool->RegisterEditFunc(
			[&]()
			{
				if (Engine::EditorField::WindowScope _window{ "GameSetting" })
				{
					DrawGameSettingEdit();
				}
			}
		);
	}
	void GameManager::Update(float a_dt)
	{	
		ENGINE_PROFILE_SCOPE("GameUpdate");

		// マウスカーソルの位置決め。
		// ゲームモードかどうかで出し方が変わるので、モード切替(Application::ToggleAppMode)の後で呼ばれるここで決める
		if (m_upMouseCursor) m_upMouseCursor->Update();

		// シーンマネージャーの更新
		const auto& _services = m_pEngine->GetEngineServices();
		m_pEngine->RefSceneManager()->Update(*_services.pResourceManager, a_dt);
	}
	void GameManager::Draw()
	{
		ENGINE_PROFILE_SCOPE("GameDraw");

		// シーンの描画 : 描画命令を積むだけで実行はしない
		m_pEngine->RefSceneManager()->Draw();

		// マウスカーソルはどのUIよりも手前に出したいので、シーンのUIを積み終えた最後に積む
		if (m_upMouseCursor)
		{
			if (auto* _pGE = m_pEngine->RefGraphicsEngine())
			{
				m_upMouseCursor->SubmitUI(_pGE->RefDrawSubmitter());
			}
		}
	}
	void GameManager::Release()
	{
		// カーソル画像の参照を返す。リソースの解放(MainEngine::Release)より前であること
		if (m_upMouseCursor)
		{
			m_upMouseCursor->Release();
			m_upMouseCursor.reset();
		}
	}
	void GameManager::FireGlobalEvent(const std::string & /*a_eventName*/)
	{}
	void GameManager::EditDraw()
	{
	}
	//======================================================================================
	// ゲーム設定の読み込み / 保存
	//
	// 中身は初回シーンのGUIDが1つだけ。エディターで選び直したら Save ボタンで書き戻す。
	//======================================================================================
	void GameManager::LoadGameSetting()
	{
		Engine::Persistence::Archive _arch(
			Engine::Persistence::Archive::EMode::Load,
			GAME_SETTING_DIR, GAME_SETTING_NAME, GAME_SETTING_EXT);

		_arch.Field("m_farstScene", m_farstScene);
		_arch.Field("m_loadingScene", m_loadingScene);
	}
	void GameManager::SaveGameSetting()
	{
		Engine::Persistence::Archive _arch(
			Engine::Persistence::Archive::EMode::Save,
			GAME_SETTING_DIR, GAME_SETTING_NAME, GAME_SETTING_EXT);

		_arch.Field("m_farstScene", m_farstScene);
		_arch.Field("m_loadingScene", m_loadingScene);
	}
	//======================================================================================
	// ゲーム設定の編集UI
	//
	// 選んだ時点では覚えるだけで、ファイルへ残すのは Save ボタン
	//======================================================================================
	void GameManager::DrawGameSettingEdit()
	{
		Engine::EditorField::Value("Farst Scene", "%s", m_farstScene.String().c_str());

		Engine::EditorField::AssetField(
			m_pEngine->GetEngineServices(),
			"##FarstScene",
			"Scene",
			m_farstScene);

		Engine::EditorField::Value("Loading Scene", "%s", m_loadingScene.String().c_str());

		Engine::EditorField::AssetField(
			m_pEngine->GetEngineServices(),
			"##LoadingScene",
			"Scene",
			m_loadingScene);
		Engine::EditorField::Tooltip("読み込みが長引いたときに重ねて出すシーン。反映は次の起動から");

		Engine::EditorField::Line();

		if (Engine::EditorField::Button("Save"))
		{
			SaveGameSetting();
		}
	}
	GameManager::GameManager()
	{}
	GameManager::~GameManager()
	{}
}