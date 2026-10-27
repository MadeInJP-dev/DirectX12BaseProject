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
#include "../../InstanceResource/DebugCommandResource.h"

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
#include "Application/Object/UI/Widget/UIRadioButton/UIRadioButton.h"
#include "Application/Object/UI/Widget/UIElement/UIElement.h"
#include "Application/Object/UI/Widget/UIGauge/UIGauge.h"
#include "Application/Object/UI/Option/OptionHUD.h"
#include "Application/Object/UI/Widget/SideMenu/SideMenu.h"
#include "Application/Object/Sequence/UINavigationGroup/UINavigationGroup.h"
#include "Application/Object/Sequence/TitleSequence/TitleSequence.h"
#include "Application/Object/Sequence/LoadingSequence/LoadingSequence.h"
#include "Application/Object/Sequence/HomeSequence/HomeSequence.h"
#include "Application/Object/Sequence/GarageSequence/GarageSequence.h"
#include "Application/Object/Sequence/PauseSequence/PauseSequence.h"
#include "Application/Object/Sequence/MissionSelect/MissionSelect.h"
#include "../../Object/Scene/AmbientDustObject/AmbientDustObject.h"
#include "../../Object/Scene/PlayerSpawner/PlayerSpawner.h"
#include "Application/Object/Scene/TitleRunBackdrop/TitleRunBackdrop.h"
#include "Application/Object/Scene/ControllNavigation/ControllNavigation.h"
#include "Application/Object/Scene/Boundary/Boundary.h"
#include "Application/Object/Sequence/SceneSequence/SceneSequence.h"
#include "Application/Object/Controller/SwarmBossController/SwarmBossController.h"

// App
#include "../UserData/UserData.h"
#include "../InputActions/InputManager/InputActionManager.h"
#include "../MouseCursor/MouseCursor.h"
#include "../DebugKeys/DebugKeys.h"
#include "../FPSDisplay/FPSDisplay.h"

// エディター

namespace App::Game
{
	//======================================================================================
	// ゲーム設定ファイルの置き場所
	//
	// 持っているのは「起動時に立ち上げるシーン」と「既定の出撃構成」だけ。アセットではないので
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

		// ゲーム設定(起動時に立ち上げるシーン・既定の出撃構成)の読み込み
		LoadGameSetting();

		// 出撃構成は既定のものから始める(ホームで選び直せば上書きされる)
		m_gameData.loadout = m_defaultLoadout;

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

		// FPS の表示 : 出すかどうかはユーザーデータが決める
		if (!m_upFPSDisplay)
		{
			m_upFPSDisplay = std::make_unique<FPSDisplay>();
			m_upFPSDisplay->Init(*m_pEngine);
		}

		// デバッグキー : 起動設定で切っていれば作らない(ワールドへ入口も置かれないので、受け取る側も何もしない)
		if (!m_upDebugKeys && DebugKeys::IsEnabled())
		{
			m_upDebugKeys = std::make_unique<DebugKeys>();
			m_upDebugKeys->Init(*m_pEngine);
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
			_objRegistry.RegisterType<App::Object::UIElement>("UIElement");						// 振る舞いを持たない UI(背景・見出し・窓・入れ物)。見た目は飾りで決まる。カーソルを遮るかは1つずつの設定
			App::Object::UIElement::RegisterLegacyNames(_objRegistry);							// 旧 UIImage(遮る)/ UIPanel(遮らない)を UIElement へ流す
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
			_objRegistry.RegisterType<App::Object::GarageSequence>("GarageSequence");			// ガレージの進行役。取り付け口から引き出し線を出し、寄って武器を付け替える(出撃構成を書き換える)
			_objRegistry.RegisterType<App::Object::Boundary>("Boundary");						// 行ける範囲の境界。近づいた面だけが見えてくる壁を描く(外へ出さないのはシステム側)
			_objRegistry.RegisterType<App::Object::SideMenu>("SideMenu");						// カーソルを乗せると開く横のメニュー(ホームの左上)。開閉だけを持ち、中のボタンは進行役が差し込む
			_objRegistry.RegisterType<App::Object::TitleRunBackdrop>("TitleRunBackdrop");		// タイトルの背景。出した機体を床の上でブーストさせ続け、板1枚の床を足元へ寄せてカメラで追う
			_objRegistry.RegisterType<App::Object::UINavigationGroup>("UINavigationGroup");		// キー・パッドで UI を順に選ぶ。最初の UI だけを持ち、各 UI の前・次をたどる
			_objRegistry.RegisterType<App::Object::ControllNavigation>("ControllNavigation");	// 画面下部の操作説明(Enter で決定など)。今できる操作の項目だけを帯の左端から詰めて並べる
			_objRegistry.RegisterType<App::Object::UIRadioButton>("UIRadioButton");				// 並んだ選択肢のうち1つだけが選ばれている押せる UI。選ばれているかで飾りを出し分ける(組は UIRadioGroup)
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
		// ユーザーデータ(設定画面が書き換えるもの)も同じく UserDataResource で引かせる。
		// デバッグキーの命令(Development まで)は DebugCommandResource で、キーを作ったときだけ置く
		// ------------------------------------------------------------------
		m_pEngine->RefSceneManager()->SetWorldFactory(
			[pGameData = &m_gameData,
			 pUserData = m_upUserData.get(),
			 pInputActionManager = m_upInputActionManager.get(),
			 pDebugCommands = m_upDebugKeys ? &m_upDebugKeys->GetCommands() : nullptr]() -> std::unique_ptr<Engine::ECS::World>
			{
				auto _upWorld = std::make_unique<App::ECS::APPWorld>();
				_upWorld->AddResource<App::InstanceResource::GameDataResource>();
				_upWorld->RefResource<App::InstanceResource::GameDataResource>().pGameData = pGameData;

				_upWorld->AddResource<App::InstanceResource::UserDataResource>();
				auto& _userDataResource = _upWorld->RefResource<App::InstanceResource::UserDataResource>();
				_userDataResource.pUserData = pUserData;
				_userDataResource.pInputActionManager = pInputActionManager;

				if (pDebugCommands)
				{
					_upWorld->AddResource<App::InstanceResource::DebugCommandResource>();
					_upWorld->RefResource<App::InstanceResource::DebugCommandResource>().pCommands = pDebugCommands;
				}
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

		// デバッグキー : 予約はシーンの更新で拾われるので、その前に立てて後で下ろす
		if (m_upDebugKeys) m_upDebugKeys->Update(m_upUserData.get());

		// シーンマネージャーの更新
		const auto& _services = m_pEngine->GetEngineServices();
		m_pEngine->RefSceneManager()->Update(*_services.pResourceManager, a_dt);

		if (m_upDebugKeys) m_upDebugKeys->EndUpdate();

		// FPS の表示 : 設定画面で変えたぶんもこのフレームから効く
		if (m_upFPSDisplay) m_upFPSDisplay->Update(m_upUserData.get());
	}
	void GameManager::Draw()
	{
		ENGINE_PROFILE_SCOPE("GameDraw");

		// シーンの描画 : 描画命令を積むだけで実行はしない
		m_pEngine->RefSceneManager()->Draw();

		// FPS の表示はシーンのUIより手前、デバッグの表示より奥
		if (m_upFPSDisplay) m_upFPSDisplay->SubmitUI(m_pEngine->RefGraphicsEngine());

		// デバッグキーの情報の表示はシーンのUIより手前、マウスカーソルより奥
		if (m_upDebugKeys) m_upDebugKeys->SubmitUI(m_pEngine->RefGraphicsEngine());

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
		// フォントの参照を返す。リソースの解放(MainEngine::Release)より前であること
		if (m_upDebugKeys)
		{
			m_upDebugKeys->Release();
			m_upDebugKeys.reset();
		}

		// フォントの参照を返す。リソースの解放(MainEngine::Release)より前であること
		if (m_upFPSDisplay)
		{
			m_upFPSDisplay->Release();
			m_upFPSDisplay.reset();
		}

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

		ArchiveGameSetting(_arch);
	}
	void GameManager::SaveGameSetting()
	{
		Engine::Persistence::Archive _arch(
			Engine::Persistence::Archive::EMode::Save,
			GAME_SETTING_DIR, GAME_SETTING_NAME, GAME_SETTING_EXT);

		ArchiveGameSetting(_arch);
	}
	void GameManager::ArchiveGameSetting(Engine::Persistence::Archive& a_ar)
	{
		a_ar.Field("m_farstScene", m_farstScene);
		a_ar.Field("m_loadingScene", m_loadingScene);

		// ※ 追加は末尾に。バイナリは順次読みなので途中に挿すと既存データが全部ずれる
		a_ar.Field("defaultLoadoutBody", m_defaultLoadout.bodyPrefabGUID);
		a_ar.Field("defaultLoadoutRightHand", m_defaultLoadout.rightHandWeaponPrefabGUID);
		a_ar.Field("defaultLoadoutLeftHand", m_defaultLoadout.leftHandWeaponPrefabGUID);
		a_ar.Field("defaultLoadoutShoulder", m_defaultLoadout.shoulderWeaponPrefabGUID);
		a_ar.Field("defaultLoadoutLeftShoulder", m_defaultLoadout.leftShoulderWeaponPrefabGUID);
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

		//------------------------------------------------------------------
		// 既定の出撃構成
		//
		// 選び直したら今の出撃構成(GlobalGameContext)にもすぐ入れる。
		// エディターでゲームシーンを開き直したときに、そのまま試せるようにするため
		//------------------------------------------------------------------
		Engine::EditorField::Line();
		Engine::EditorField::Header("Default Loadout");

		const auto& _services = m_pEngine->GetEngineServices();
		bool _isLoadoutChanged = false;
		_isLoadoutChanged |= Engine::EditorField::AssetField(_services, "Body", "Prefab", m_defaultLoadout.bodyPrefabGUID);
		Engine::EditorField::Tooltip("取り付け口(WeaponMountComponent)を持つ機体。未設定なら PlayerSpawner のプレハブをそのまま出す");
		_isLoadoutChanged |= Engine::EditorField::AssetField(_services, "Right Hand", "Prefab", m_defaultLoadout.rightHandWeaponPrefabGUID);
		_isLoadoutChanged |= Engine::EditorField::AssetField(_services, "Left Hand", "Prefab", m_defaultLoadout.leftHandWeaponPrefabGUID);
		_isLoadoutChanged |= Engine::EditorField::AssetField(_services, "Right Shoulder", "Prefab", m_defaultLoadout.shoulderWeaponPrefabGUID);
		_isLoadoutChanged |= Engine::EditorField::AssetField(_services, "Left Shoulder", "Prefab", m_defaultLoadout.leftShoulderWeaponPrefabGUID);
		Engine::EditorField::Tooltip("反映は次にゲームシーンを開いたとき(プレイヤーを出すのはシーンの始まり)");

		if (_isLoadoutChanged) m_gameData.loadout = m_defaultLoadout;

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