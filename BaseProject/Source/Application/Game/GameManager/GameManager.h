#pragma once

#include "../GlobalGameContext.h"

namespace Engine
{
	class MainEngine;
}

namespace App::Input
{
	class InputActionManager;
}

namespace App::Game
{
	class UserData;
	class MouseCursor;
	class FPSDisplay;
	class DebugKeys;

	/// <summary>
	/// ゲーム全体を通しての流れを管理するクラス
	/// </summary>
	/// <remarks>
	/// 実体は App::Application が1つだけ持つ。
	/// シーンをまたぐ記録(GlobalGameContext)は、ワールドを作るときに
	/// GameDataResource として各ワールドへ配るので、使う側はここを名指ししない
	/// </remarks>
	class GameManager
	{
	public:

		GameManager();
		~GameManager();
		NON_COPYABLE_NON_MOVABLE(GameManager);

		/// <summary>
		/// 初期化 : ゲーム起動時の一度のみ呼ばれる
		/// </summary>
		/// <param name="a_engine">エンジン(借り物)。シーン・入力・描画はここから引く</param>
		void Init(Engine::MainEngine& a_engine);

		/// <summary>
		/// メインループからマイフレーム呼ばれる
		/// </summary>
		void Update(float a_dt);

		/// <summary>
		/// メインループから呼ばれる : 命令を積むだけで実行はしない
		/// </summary>
		void Draw();

		/// <summary>
		/// ゲーム終了処理 : リソースの参照を返すので、エンジンの解放より前に呼ぶこと
		/// </summary>
		void Release();

		// ---- アクセサ ----
		const GlobalGameContext& GetGameData() const { return m_gameData; }
		GlobalGameContext& RefGameData() { return m_gameData; }

		/// <summary>
		/// ゲーム内イベントの発火（UIのボタンやシステムから呼ばれる） 
		/// </summary>
		/// <param name="a_eventName">イベント名</param>
		void FireGlobalEvent(const std::string& a_eventName);

		/// <summary>
		/// エディター描画用
		/// </summary>
		void EditDraw();

	private:

		/// <summary>
		/// ゲーム設定の読み込み : Init から一度だけ
		/// </summary>
		void LoadGameSetting();

		/// <summary>
		/// ゲーム設定の保存 : エディターの Save ボタンから
		/// </summary>
		void SaveGameSetting();

		/// <summary>
		/// ゲーム設定の編集UI(起動時に立ち上げるシーン・既定の出撃構成を選ぶ)
		/// </summary>
		void DrawGameSettingEdit();

		/// <summary>
		/// ゲーム設定の項目の読み書き(Load / Save で同じ並びを使う)
		/// </summary>
		void ArchiveGameSetting(Engine::Persistence::Archive& a_ar);

	private:

		// シーンをまたいで保持できる情報
		GlobalGameContext m_gameData;

		// 一時停止フラグ（ポーズ画面用）
		bool m_isPaused = false;

		// ゲーム開始時の初回シーン : 起動時に出現させる
		Core::GUID m_farstScene;

		// ロード画面のシーン : 起動時に SceneManager へ渡して常駐させる(無効なら出さない)
		Core::GUID m_loadingScene;

		// 既定の出撃構成 : 起動時に m_gameData.loadout へ入れる。
		// ホームで選び直したものはここへは書かない(ゲーム設定の保存に混ざらないように)
		PlayerLoadout m_defaultLoadout = {};

		// ユーザーデータ
		std::unique_ptr<UserData> m_upUserData;

		// ---- 設定 ----
		// 入力
		std::unique_ptr<Input::InputActionManager> m_upInputActionManager;

		// ゲーム中に自前で描くマウスカーソル
		std::unique_ptr<MouseCursor> m_upMouseCursor;

		// 画面の右上の FPS の表示(設定画面で出し入れする。Shipping でも作る)
		std::unique_ptr<FPSDisplay> m_upFPSDisplay;

		// 発表・確認用のデバッグキー(F1〜F7)。Shipping では作らない
		std::unique_ptr<DebugKeys> m_upDebugKeys;

		// エンジン(借り物)
		Engine::MainEngine* m_pEngine = nullptr;
	};
}