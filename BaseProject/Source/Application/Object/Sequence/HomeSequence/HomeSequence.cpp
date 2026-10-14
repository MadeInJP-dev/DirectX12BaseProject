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

		// BGM : 初回で鳴らし始め、フェードを進める
		m_bgm.Update(a_context);
	}

	//======================================================================================
	// ボタンへ押下時の処理を差し込む
	//--------------------------------------------------------------------------------------
	// 設定されているのに見つからないものが1つでもあれば、まだ読み込みの途中とみなして
	// 次のフレームへ回す。全部そろってから差し込むので、差し込み漏れが起きない。
	//======================================================================================
	void HomeSequence::TryBindButtons(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isBound) return;

		auto* _pObjectManager = a_context.pObjectManager;
		if (!_pObjectManager) return;
		if (!IsAllReady(_pObjectManager)) return;

		// ボタンは同じシーンに居るので、this もマネージャーも寿命は一緒に尽きる
		if (auto* _pMissionSelect = Picker::Find<UIButton>(_pObjectManager, m_missionSelectButtonGUID))
		{
			_pMissionSelect->SetOnClick(
				[this, _pObjectManager]() { SetMode(EHomeMode::MissionSelect, _pObjectManager); });
		}

		if (auto* _pWarehouse = Picker::Find<UIButton>(_pObjectManager, m_warehouseButtonGUID))
		{
			// 倉庫はまだ無い。押せる状態にされていても、知らせるだけで何もしない
			// (押せるかどうかは ApplyVisible が設定値を見て切り替える)
			_pWarehouse->SetOnClick([]()
				{
					ENGINE_LOG("[HomeSequence] 倉庫は準備中");
				});
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
	// 指しているものがそろっているか
	//======================================================================================
	bool HomeSequence::IsAllReady(Engine::GameObject::GameObjectManager* a_pObjectManager) const
	{
		if (!Picker::IsReady<UIButton>(a_pObjectManager, m_missionSelectButtonGUID)) return false;
		if (!Picker::IsReady<UIButton>(a_pObjectManager, m_warehouseButtonGUID))     return false;
		if (!Picker::IsReady<UIButton>(a_pObjectManager, m_backButtonGUID))          return false;
		if (!Picker::IsReady<UIButton>(a_pObjectManager, m_optionButtonGUID))        return false;
		if (!Picker::IsReady<UIButton>(a_pObjectManager, m_titleButtonGUID))         return false;
		if (!Picker::IsReady<UIButton>(a_pObjectManager, m_quitButtonGUID))          return false;

		return true;
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
	// メニュー : ゲームを終了する
	//--------------------------------------------------------------------------------------
	// エディターが差し込まれているときは、アプリを閉じずにエディターへ戻るだけにする。
	// 閉じるとエディターごと終わり、保存していない編集が消えてしまう
	//======================================================================================
	void HomeSequence::ReserveQuitGame(Engine::MainEngine& a_engine)
	{
		if (m_isLeaving) return;

		if (a_engine.HasDevTool())
		{
			ENGINE_LOG("[HomeSequence] エディターが居るので、終了せずエディターへ戻ります");
			a_engine.ChangeMode(Engine::EAppMode::Editor);
			return;
		}

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

			// 倉庫の中身はまだ無い。既定では押せない状態(灰色)にしておく
			if (auto* _pButton = Core::TypeInfo::Cast<UIButton>(_pUI))
			{
				_pButton->SetInteractable(m_isWarehouseInteractable);
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
		if (Picker::DrawCombo<UIButton>("Warehouse", _pObjectManager, m_warehouseButtonGUID))         m_isBound = false;
		if (Picker::DrawCombo<UIButton>("Back", _pObjectManager, m_backButtonGUID))                   m_isBound = false;

		if (Engine::EditorField::Field("WarehouseInteractable", m_isWarehouseInteractable))
		{
			// その場で見た目へ反映する
			ApplyVisible(_pObjectManager);
		}
		Engine::EditorField::Tooltip("倉庫はまだ中身が無いので、押しても何も起きない");

		//----------------------------------------------------------------------
		// 左上のメニュー(SideMenu の中のボタン)
		//----------------------------------------------------------------------
		Engine::EditorField::Header("Menu");

		if (Picker::DrawCombo<UIButton>("Option", _pObjectManager, m_optionButtonGUID)) m_isBound = false;
		if (Picker::DrawCombo<UIButton>("Title", _pObjectManager, m_titleButtonGUID))   m_isBound = false;
		if (Picker::DrawCombo<UIButton>("Quit", _pObjectManager, m_quitButtonGUID))     m_isBound = false;
		Engine::EditorField::Tooltip("エディターが居るときは終了せず、エディターへ戻る");

		Engine::EditorField::AssetField(*a_context.pServices, "Scene", "OptionScene", m_optionSceneGUID);
		Engine::EditorField::Tooltip("Option を押したときに重ねる設定画面のシーン");
		Engine::EditorField::AssetField(*a_context.pServices, "Scene", "TitleScene", m_titleSceneGUID);
		Engine::EditorField::Tooltip("Title を押したときに切り替えるタイトルのシーン");

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
