#pragma once

#include "../../../../Engine/GameObject/BaseObject/BaseObject.h"

#include "../../SequenceBgm.h"

namespace Engine
{
	class MainEngine;
}

namespace Engine::Scene
{
	class SceneManager;
}

namespace App::Object
{
	//==============================================================================
	// ホーム画面のモード
	//==============================================================================
	enum class EHomeMode : uint32_t
	{
		Home = 0,		// トップ : ミッションセレクト / ガレージ のボタンだけ
		MissionSelect,	// ミッションセレクトを開いている
	};

	//==============================================================================
	/// ホーム画面の進行役 : シーンに一つ置く
	/// </summary>
	/// <remarks>
	/// タイトルから来る画面。持っているのは「今どこを見ているか」だけで、
	/// 中身は他のオブジェクトへ任せる。
	///
	///   トップ           : ミッションセレクト / 倉庫 のボタンを出す
	///   ミッションセレクト : 覚えておいたオブジェクトを出す(MissionSelect など)
	///
	/// ・ボタン(UIButton)はシーンへ置いて GUID で引く
	///     押されて何をするかはここが差し込むので、ボタン側はホームの知識を持たない。
	///
	/// ・「押したら出すオブジェクト」は配列で覚える
	///     以前はステージ一覧をここが直接並べて描いていたが、それだと縦一列にしか
	///     並べられず、項目ごとに見た目を変えることもできなかった。
	///     いまは出すものを GUID の配列で持つだけにして、一覧の作りは
	///     出される側(MissionSelect)へ寄せてある。
	///     出す相手は UI でも進行役でもよい(BaseObject::SetVisible を通す)。
	///
	/// ・出し分けは SetVisible
	///     オブジェクトを作り直さずに表示だけ切り替える。
	///     非表示のUIは描画も入力も止まるので、裏で押せてしまうことがない。
	///     切り替えはボタンのコールバックからその場で行うので、押した瞬間に出る。
	///
	/// ・ガレージのボタン(保存名は旧来の Warehouse のまま)
	///     押すとガレージのシーンへ切り替える。シーンが未設定なら押せない状態(灰色)にする。
	///
	/// ・左上のメニュー(SideMenu)のボタン : 設定 / タイトルへ戻る / ゲームを終了
	///     開閉は SideMenu が持ち、押されて何をするかはここが差し込む。
	///     ゲームの終了は、起動モード(エディターの有無)によらずアプリを閉じる。
	//==============================================================================
	class HomeSequence : public Engine::GameObject::BaseObject
	{
	public:

		// 更新処理 : ボタンへの差し込みと、カーソル固定の解除
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 解放処理 : カーソル固定を設定値へ戻す
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//-----------------------------------------------------------------------
		// エディター用
		//-----------------------------------------------------------------------

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "HomeSequence"; }

		// インスペクター : ボタンと出し分けの設定
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// ボタンへ押下時の処理を差し込む(見つかったものだけ。見つからないものは知らせる)
		void TryBindButtons(Engine::GameObject::ObjectContext& a_context);

		// 見ている場所を切り替える(表示の出し分けもここで行う)
		void SetMode(EHomeMode a_mode, Engine::GameObject::GameObjectManager* a_pObjectManager);

		// 今のモードに合わせて表示を切り替える
		void ApplyVisible(Engine::GameObject::GameObjectManager* a_pObjectManager);

		// 指しているのに見つからないボタンを知らせる
		void WarnMissingButtons(Engine::GameObject::GameObjectManager* a_pObjectManager) const;

		// メニュー : 設定画面のシーンを重ねる
		void ReserveOpenOption(Engine::Scene::SceneManager& a_sceneManager);

		// メニュー : タイトルへ戻る
		void ReserveReturnTitle(Engine::Scene::SceneManager& a_sceneManager);

		// メニュー : ゲームを終了する(起動モードによらずアプリを閉じる)
		void ReserveQuitGame(Engine::MainEngine& a_engine);

		// ガレージのシーンへ切り替える
		void ReserveOpenGarage(Engine::Scene::SceneManager& a_sceneManager);

	private:

		//-------------------------------------------------------------------
		// 設定(保存される) : ボタン
		//-------------------------------------------------------------------
		Core::GUID m_missionSelectButtonGUID = {};	// トップ : ミッションセレクトを開く
		Core::GUID m_warehouseButtonGUID     = {};	// トップ : ガレージ(保存名は旧来の Warehouse のまま)
		Core::GUID m_backButtonGUID          = {};	// セレクト : トップへ戻る(任意)

		//-------------------------------------------------------------------
		// 設定(保存される) : 左上のメニュー(SideMenu の中のボタン)。どれも任意
		//-------------------------------------------------------------------
		Core::GUID m_optionButtonGUID = {};		// 設定画面を重ねる
		Core::GUID m_titleButtonGUID  = {};		// タイトルへ戻る
		Core::GUID m_quitButtonGUID   = {};		// ゲームを終了する
		Core::GUID m_optionSceneGUID  = {};		// 設定画面のシーン(Push で重ねる)
		Core::GUID m_titleSceneGUID   = {};		// タイトルのシーン(Replace で切り替える)
		Core::GUID m_garageSceneGUID  = {};		// ガレージのシーン(Replace で切り替える)

		//-------------------------------------------------------------------
		// 設定(保存される) : 出し分け
		//-------------------------------------------------------------------
		// トップでだけ出すもの(背景・ロゴ・見出しなど)
		std::vector<Core::GUID> m_homeUIGUIDVec = {};

		// ミッションセレクトボタンを押したときに出すもの
		// (MissionSelect 本体のほか、専用の背景なども並べられる)
		std::vector<Core::GUID> m_missionObjectGUIDVec = {};

		//-------------------------------------------------------------------
		// 設定(保存される) : ふるまい
		//-------------------------------------------------------------------
		// ガレージのボタンを押せる状態にするか(シーンが未設定なら、立っていても押せない)
		bool m_isWarehouseInteractable = false;

		// ホームの間はカーソルの中央固定を切るか(固定したままだと狙えない)
		bool m_isReleaseCursorLock = true;

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		EHomeMode m_mode = EHomeMode::Home;	// 今見ている場所

		bool m_isBound = false;	// ボタンへ差し込み済みか
		bool m_isLeaving = false;	// タイトルへ戻る・終了を頼んだか(二重に頼まないため)

		//-------------------------------------------------------------------
		// BGM
		//-------------------------------------------------------------------
		SequenceBgm m_bgm = {};
	};
}
