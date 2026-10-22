#include "HomeSequence.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Option/OptionManager.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/MainEngine.h"
#include "Engine/EditorField/EditorField.h"

#include "../../ObjectPicker.h"
#include "Application/Object/UI/UIBase.h"
#include "Application/Object/UI/Widget/UIButton/UIButton.h"
#include "Application/Object/UI/Widget/SideMenu/SideMenu.h"
#include "Application/Object/Sequence/MissionSelect/MissionSelect.h"
#include "Application/Object/Scene/ControllNavigation/ControllNavigation.h"

//==========================================================================================
// HomeSequence
//
// タイトルの次に来る画面。持っているのは「今どこを見ているか」だけ。
//
// ・ボタンはシーンへ置いたものを GUID で引く(TitleSequence と同じ)
//     押されて何をするかをここが差し込むので、ボタン側はホームの知識を持たない。
//
// ・「押したら出すオブジェクト」は GUID の配列
//     一覧の中身は出される側(MissionSelect)が持つ。ここは出し入れするだけ。
//     出す相手の型を知らずに済むよう、BaseObject::SetVisible を通す。
//
// ・切り替えはコールバックからその場で行う
//     頼まれごとを覚えて次の更新で処理する形にすると、押してから1フレーム遅れて出る。
//     コールバックの中では ObjectContext を持てないので、差し込むときに
//     マネージャーを一緒に掴ませてある(シングルトンを名指ししないための経路は保つ)。
//
// ・カーソルの中央固定を切る
//     プレイ中は視点操作のためにカーソルを毎フレーム画面中央へ戻している。
//     そのままではカーソルを動かせず、中央のボタン以外を狙えない。
//==========================================================================================
namespace App::Object
{
	//======================================================================================
	// 更新
	//======================================================================================
	void HomeSequence::Update(Engine::GameObject::ObjectContext& a_context)
	{
		//==============================================================
		// カーソルの固定を切る
		//--------------------------------------------------------------
		// 他所で固定を入れ直されてもホームの間は切れているように、毎フレーム見る
		//==============================================================
		if (m_isReleaseCursorLock && a_context.pServices && a_context.pServices->pInputManager)
		{
			if (a_context.pServices->pInputManager->IsCursorLockActive())
			{
				a_context.pServices->pInputManager->SetCursorCentered(false);
			}
		}

		// ボタンへの差し込み(済んでいれば何もしない)
		TryBindButtons(a_context);

		// 戻るのキーと、今できる操作の説明
		UpdateBackInput(a_context);
		UpdateControllNavigation(a_context);

		// BGM : 初回で鳴らし始め、フェードを進める
		m_bgm.Update(a_context);
	}

	//======================================================================================
	// ボタンへ押下時の処理を差し込む
	//--------------------------------------------------------------------------------------
	// 見つかったボタンにだけ差し込む。見つからないものは知らせるだけで待たない。
	// シーンのオブジェクトは読み込みの時点で全部できているので、ここで引けないものは
	// 消したボタンを指したままの設定漏れ。待つと残りのボタンまで全部押せなくなる。
	//======================================================================================
	void HomeSequence::TryBindButtons(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isBound) return;

		auto* _pObjectManager = a_context.pObjectManager;
		if (!_pObjectManager) return;

		WarnMissingButtons(_pObjectManager);

		// ボタンは同じシーンに居るので、this もマネージャーも寿命は一緒に尽きる
		if (auto* _pMissionSelect = Picker::Find<UIButton>(_pObjectManager, m_missionSelectButtonGUID))
		{
			_pMissionSelect->SetOnClick(
				[this, _pObjectManager]() { SetMode(EHomeMode::MissionSelect, _pObjectManager); });
		}

		// ガレージ(押せるかどうかは ApplyVisible が設定値とシーンの有無を見て切り替える)
		if (auto* _pGarage = Picker::Find<UIButton>(_pObjectManager, m_warehouseButtonGUID);
			_pGarage && a_context.pServices && a_context.pServices->pSceneManager)
		{
			auto* _pSceneManager = a_context.pServices->pSceneManager;
			_pGarage->SetOnClick([this, _pSceneManager]() { ReserveOpenGarage(*_pSceneManager); });
		}

		if (auto* _pBack = Picker::Find<UIButton>(_pObjectManager, m_backButtonGUID))
		{
			_pBack->SetOnClick(
				[this, _pObjectManager]() { SetMode(EHomeMode::Home, _pObjectManager); });
		}

		//--------------------------------------------------------------
		// 左上のメニュー(SideMenu)のボタン
		//--------------------------------------------------------------
		auto* _pSceneManager = a_context.pServices ? a_context.pServices->pSceneManager : nullptr;
		auto* _pEngine = a_context.pServices ? a_context.pServices->pMainEngine : nullptr;

		if (auto* _pOption = Picker::Find<UIButton>(_pObjectManager, m_optionButtonGUID); _pOption && _pSceneManager)
		{
			_pOption->SetOnClick([this, _pSceneManager]() { ReserveOpenOption(*_pSceneManager); });
		}
		if (auto* _pTitle = Picker::Find<UIButton>(_pObjectManager, m_titleButtonGUID); _pTitle && _pSceneManager)
		{
			_pTitle->SetOnClick([this, _pSceneManager]() { ReserveReturnTitle(*_pSceneManager); });
		}
		if (auto* _pQuit = Picker::Find<UIButton>(_pObjectManager, m_quitButtonGUID); _pQuit && _pEngine)
		{
			_pQuit->SetOnClick([this, _pEngine]() { ReserveQuitGame(*_pEngine); });
		}

		m_isBound = true;

		// そろったところで、今のモードの見た目にしておく
		ApplyVisible(_pObjectManager);
	}

	//======================================================================================
	// 戻る : 手前にあるものから1つ閉じる
	//--------------------------------------------------------------------------------------
	// 1回押して閉じるのは1つだけ。重なっている順(メニュー → 出撃の確認 → ミッションセレクト)に見る。
	// 閉じる処理はそれぞれのボタン(CANCEL・BACK)を押したときと同じものを呼ぶ
	//======================================================================================
	void HomeSequence::UpdateBackInput(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isLeaving) return;
		if (!a_context.pServices || !a_context.pServices->pInputManager) return;

		// エディター操作中・文字入力中は受けない
		const auto& _input = *a_context.pServices->pInputManager;
		if (!_input.IsGameInputEnable()) return;
		if (!_input.IsPress(m_backAction)) return;

		auto* _pObjectManager = a_context.pObjectManager;

		if (auto* _pSideMenu = Picker::Find<SideMenu>(_pObjectManager, m_sideMenuGUID); _pSideMenu && _pSideMenu->IsOpen())
		{
			_pSideMenu->Close();
			return;
		}

		if (auto* _pMissionSelect = FindMissionSelect(_pObjectManager); _pMissionSelect && _pMissionSelect->IsConfirmOpen())
		{
			_pMissionSelect->CancelConfirm();
			return;
		}

		if (m_mode == EHomeMode::MissionSelect)
		{
			SetMode(EHomeMode::Home, _pObjectManager);
		}
	}

	//======================================================================================
	// 操作説明 : 決定・戻るを、今できるかどうかで出し入れする
	//--------------------------------------------------------------------------------------
	// メニュー(SideMenu の開閉)はどこでもできるので、操作説明の側で出しっぱなしにしてある
	//======================================================================================
	void HomeSequence::UpdateControllNavigation(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;

		auto* _pNavigation = Picker::Find<ControllNavigation>(_pObjectManager, m_controllNavigationGUID);
		if (!_pNavigation) return;

		const auto* _pSideMenu = Picker::Find<SideMenu>(_pObjectManager, m_sideMenuGUID);
		const bool _isMenuOpen = _pSideMenu && _pSideMenu->IsOpen();

		const auto* _pMissionSelect = FindMissionSelect(_pObjectManager);
		const bool _isConfirmOpen = _pMissionSelect && _pMissionSelect->IsConfirmOpen();

		// 決定 : キーで項目を選べる(UINavigationGroup を置いてある)のはトップとメニューだけ
		_pNavigation->SetActionEnabled(Game::EGameAction::Decide, _isMenuOpen || (m_mode == EHomeMode::Home));

		// 戻る : 閉じる相手があるとき(UpdateBackInput と同じものを見る)
		_pNavigation->SetActionEnabled(m_backAction, _isMenuOpen || _isConfirmOpen || (m_mode == EHomeMode::MissionSelect));
	}

	MissionSelect* HomeSequence::FindMissionSelect(Engine::GameObject::GameObjectManager* a_pObjectManager) const
	{
		for (const Core::GUID& _guid : m_missionObjectGUIDVec)
		{
			if (auto* _pMissionSelect = Picker::Find<MissionSelect>(a_pObjectManager, _guid)) return _pMissionSelect;
		}
		return nullptr;
	}

	//======================================================================================
	// 指しているのに見つからないボタンを知らせる
	//======================================================================================
	void HomeSequence::WarnMissingButtons(Engine::GameObject::GameObjectManager* a_pObjectManager) const
	{
		constexpr const char* OWNER = "HomeSequence";
		Picker::CheckFound<UIButton>(a_pObjectManager, m_missionSelectButtonGUID, OWNER, "MissionSelectButton");
		Picker::CheckFound<UIButton>(a_pObjectManager, m_warehouseButtonGUID,     OWNER, "WarehouseButton");
		Picker::CheckFound<UIButton>(a_pObjectManager, m_backButtonGUID,          OWNER, "BackButton");
		Picker::CheckFound<UIButton>(a_pObjectManager, m_optionButtonGUID,        OWNER, "OptionButton");
		Picker::CheckFound<UIButton>(a_pObjectManager, m_titleButtonGUID,         OWNER, "TitleButton");
		Picker::CheckFound<UIButton>(a_pObjectManager, m_quitButtonGUID,          OWNER, "QuitButton");
	}

	//======================================================================================
	// メニュー : 設定画面を重ねる
	//--------------------------------------------------------------------------------------
	// ホームは閉じずに上へ重ねる。更新されるのは一番上のシーンだけなので、
	// 設定画面が自分を外せばホームの続きへ戻る
	//======================================================================================
	void HomeSequence::ReserveOpenOption(Engine::Scene::SceneManager& a_sceneManager)
	{
		if (m_isLeaving) return;

		if (!m_optionSceneGUID.IsValid())
		{
			ENGINE_WARNING("[HomeSequence] 設定画面のシーンが設定されていません");
			return;
		}

		a_sceneManager.ReserveChangeScene(m_optionSceneGUID, Engine::Scene::ESceneChangeType::Push);
	}

	//======================================================================================
	// メニュー : タイトルへ戻る
	//======================================================================================
	void HomeSequence::ReserveReturnTitle(Engine::Scene::SceneManager& a_sceneManager)
	{
		if (m_isLeaving) return;

		if (!m_titleSceneGUID.IsValid())
		{
			ENGINE_WARNING("[HomeSequence] タイトルのシーンが設定されていません");
			return;
		}

		m_isLeaving = true;
		a_sceneManager.ReserveChangeScene(m_titleSceneGUID, Engine::Scene::ESceneChangeType::Replace);
	}

	//======================================================================================
	// ガレージのシーンへ切り替える
	//======================================================================================
	void HomeSequence::ReserveOpenGarage(Engine::Scene::SceneManager& a_sceneManager)
	{
		if (m_isLeaving) return;

		if (!m_garageSceneGUID.IsValid())
		{
			ENGINE_WARNING("[HomeSequence] ガレージのシーンが設定されていません");
			return;
		}

		m_isLeaving = true;
		a_sceneManager.ReserveChangeScene(m_garageSceneGUID, Engine::Scene::ESceneChangeType::Replace);
	}

	//======================================================================================
	// メニュー : ゲームを終了する
	//--------------------------------------------------------------------------------------
	// 起動モード(エディターの有無)によらず、アプリを閉じる
	//======================================================================================
	void HomeSequence::ReserveQuitGame(Engine::MainEngine& a_engine)
	{
		if (m_isLeaving) return;

		m_isLeaving = true;
		a_engine.ReserveQuit();
	}

	//======================================================================================
	// 見ている場所を切り替える
	//======================================================================================
	void HomeSequence::SetMode(EHomeMode a_mode, Engine::GameObject::GameObjectManager* a_pObjectManager)
	{
		m_mode = a_mode;
		ApplyVisible(a_pObjectManager);
	}

	//======================================================================================
	// 今のモードに合わせて表示を切り替える
	//--------------------------------------------------------------------------------------
	// 消して作り直すのではなく SetVisible で出し入れする。
	// 非表示のUIは描画も入力も止まるので、裏で押せてしまうことがない。
	//======================================================================================
	void HomeSequence::ApplyVisible(Engine::GameObject::GameObjectManager* a_pObjectManager)
	{
		if (a_pObjectManager == nullptr) return;

		const bool _isHome = (m_mode == EHomeMode::Home);

		// トップのボタン
		if (auto* _pUI = Picker::Find<Engine::GameObject::BaseObject>(a_pObjectManager, m_missionSelectButtonGUID))
		{
			_pUI->SetVisible(_isHome);
		}
		if (auto* _pUI = Picker::Find<Engine::GameObject::BaseObject>(a_pObjectManager, m_warehouseButtonGUID))
		{
			_pUI->SetVisible(_isHome);

			// ガレージのシーンが無ければ押せない状態(灰色)にしておく
			if (auto* _pButton = Core::TypeInfo::Cast<UIButton>(_pUI))
			{
				_pButton->SetInteractable(m_isWarehouseInteractable && m_garageSceneGUID.IsValid());
			}
		}

		// 戻るボタンはミッションセレクト側
		if (auto* _pUI = Picker::Find<Engine::GameObject::BaseObject>(a_pObjectManager, m_backButtonGUID))
		{
			_pUI->SetVisible(!_isHome);
		}

		// トップでだけ出すもの
		for (const Core::GUID& _guid : m_homeUIGUIDVec)
		{
			if (auto* _pUI = Picker::Find<Engine::GameObject::BaseObject>(a_pObjectManager, _guid))
			{
				_pUI->SetVisible(_isHome);
			}
		}

		// 押したときに出すもの(MissionSelect など)
		for (const Core::GUID& _guid : m_missionObjectGUIDVec)
		{
			if (auto* _pObject = Picker::Find<Engine::GameObject::BaseObject>(a_pObjectManager, _guid))
			{
				_pObject->SetVisible(!_isHome);
			}
		}
	}

	//======================================================================================
	// 解放 : カーソルの固定を設定値へ戻す
	//======================================================================================
	void HomeSequence::Release(Engine::GameObject::ObjectContext& a_context)
	{
		// 借りているBGMを返す
		m_bgm.Release(a_context);

		if (!m_isReleaseCursorLock) return;
		if (!a_context.pServices) return;
		if (!a_context.pServices->pInputManager || !a_context.pServices->pOptionManager) return;

		// ホームで切ったぶんを戻す。切りっぱなしにすると、
		// 出撃先で視点操作のカーソル固定が効かなくなる
		const bool _isLocked =
			a_context.pServices->pOptionManager->GetInputOption().isCursorLockedToCenter;

		a_context.pServices->pInputManager->SetCursorCentered(_isLocked);
	}

	//======================================================================================
	// シリアライズ
	//======================================================================================
	void HomeSequence::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		//----------------------------------------------------------------------
		// ボタン
		//----------------------------------------------------------------------
		a_ar.GUIDField("MissionSelectButtonGUID", m_missionSelectButtonGUID);
		a_ar.GUIDField("WarehouseButtonGUID", m_warehouseButtonGUID);
		a_ar.GUIDField("BackButtonGUID", m_backButtonGUID);

		//----------------------------------------------------------------------
		// 出し分け
		//----------------------------------------------------------------------
		a_ar.GUIDVectorField("HomeUIGUIDs", m_homeUIGUIDVec);
		a_ar.GUIDVectorField("MissionObjectGUIDs", m_missionObjectGUIDVec);

		//----------------------------------------------------------------------
		// ふるまい
		//----------------------------------------------------------------------
		a_ar.Field("IsWarehouseInteractable", m_isWarehouseInteractable);
		a_ar.Field("IsReleaseCursorLock", m_isReleaseCursorLock);

		m_bgm.Archive(a_ar);

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		// 区切りを持たない古い .ob* には無いので読まない
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.GUIDField("OptionButtonGUID", m_optionButtonGUID);
			a_ar.GUIDField("TitleButtonGUID", m_titleButtonGUID);
			a_ar.GUIDField("QuitButtonGUID", m_quitButtonGUID);
			a_ar.GUIDField("OptionSceneGUID", m_optionSceneGUID);
			a_ar.GUIDField("TitleSceneGUID", m_titleSceneGUID);
			a_ar.GUIDField("GarageSceneGUID", m_garageSceneGUID);
			Game::ActionField(a_ar, "BackActionName", m_backAction);
			a_ar.GUIDField("SideMenuGUID", m_sideMenuGUID);
			a_ar.GUIDField("ControllNavigationGUID", m_controllNavigationGUID);
		}

		if (a_ar.IsLoading())
		{
			// 指し先が変わったので差し込み直す。開くのはトップから
			m_isBound = false;
			m_mode = EHomeMode::Home;
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void HomeSequence::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;

		//----------------------------------------------------------------------
		// 今どこを見ているか
		//----------------------------------------------------------------------
		// エディターでは押して切り替えられないので、ここから切り替えて配置を見る
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Mode");

		EHomeMode _mode = m_mode;
		if (Engine::EditorField::Field("Mode", _mode))
		{
			SetMode(_mode, _pObjectManager);
		}
		Engine::EditorField::Tooltip("配置を見るときはここで切り替える(保存はされない)");

		//----------------------------------------------------------------------
		// ボタン
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Buttons");

		if (Picker::DrawCombo<UIButton>("MissionSelect", _pObjectManager, m_missionSelectButtonGUID)) m_isBound = false;
		if (Picker::DrawCombo<UIButton>("Garage", _pObjectManager, m_warehouseButtonGUID))            m_isBound = false;
		if (Picker::DrawCombo<UIButton>("Back", _pObjectManager, m_backButtonGUID))                   m_isBound = false;

		if (Engine::EditorField::Field("GarageInteractable", m_isWarehouseInteractable))
		{
			// その場で見た目へ反映する
			ApplyVisible(_pObjectManager);
		}
		Engine::EditorField::Tooltip("ガレージのシーンが未設定なら、立っていても押せない");
		Engine::EditorField::AssetField(*a_context.pServices, "Scene", "GarageScene", m_garageSceneGUID);
		Engine::EditorField::Tooltip("GARAGE を押したときに切り替えるシーン");

		//----------------------------------------------------------------------
		// 左上のメニュー(SideMenu の中のボタン)
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Menu");

		if (Picker::DrawCombo<UIButton>("Option", _pObjectManager, m_optionButtonGUID)) m_isBound = false;
		if (Picker::DrawCombo<UIButton>("Title", _pObjectManager, m_titleButtonGUID))   m_isBound = false;
		if (Picker::DrawCombo<UIButton>("Quit", _pObjectManager, m_quitButtonGUID))     m_isBound = false;
		Engine::EditorField::Tooltip("起動モードによらず、アプリを閉じる");

		Engine::EditorField::AssetField(*a_context.pServices, "Scene", "OptionScene", m_optionSceneGUID);
		Engine::EditorField::Tooltip("Option を押したときに重ねる設定画面のシーン");
		Engine::EditorField::AssetField(*a_context.pServices, "Scene", "TitleScene", m_titleSceneGUID);
		Engine::EditorField::Tooltip("Title を押したときに切り替えるタイトルのシーン");

		//----------------------------------------------------------------------
		// 戻る・操作説明
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Back");

		Engine::EditorField::Field("BackAction", m_backAction);
		Engine::EditorField::Tooltip("押すと手前にあるものから1つ閉じる : メニュー → 出撃の確認 → ミッションセレクト");
		Picker::DrawCombo<SideMenu>("SideMenu", _pObjectManager, m_sideMenuGUID);
		Engine::EditorField::Tooltip("左上のメニュー。開いていれば戻るで閉じる");
		Picker::DrawCombo<ControllNavigation>("ControllNavigation", _pObjectManager, m_controllNavigationGUID);
		Engine::EditorField::Tooltip("画面下部の操作説明。決定・戻るの項目を、今できるかどうかで出し入れする");

		//----------------------------------------------------------------------
		// 出し分け
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Visible Group");

		Engine::EditorField::HelpText("トップでだけ出すもの(背景・ロゴ・見出しなど)");
		if (Picker::DrawList<Engine::GameObject::BaseObject>("Home Only", _pObjectManager, m_homeUIGUIDVec))
		{
			ApplyVisible(_pObjectManager);
		}

		Engine::EditorField::HelpText("MissionSelect ボタンを押したときに出すもの");
		if (Picker::DrawList<Engine::GameObject::BaseObject>("Mission Objects", _pObjectManager, m_missionObjectGUIDVec))
		{
			ApplyVisible(_pObjectManager);
		}

		//----------------------------------------------------------------------
		// その他
		//----------------------------------------------------------------------
		m_bgm.DrawInspector(a_context);

		Engine::EditorField::Header("Cursor");
		Engine::EditorField::Field("ReleaseCursorLock", m_isReleaseCursorLock);
		Engine::EditorField::Tooltip("ホームの間はカーソルの中央固定を切る");

		// 実行中の状態は表示のみ
		Engine::EditorField::Header("Runtime");
		Engine::EditorField::Value("Bound", "%s", m_isBound ? "yes" : "no");
	}
}
