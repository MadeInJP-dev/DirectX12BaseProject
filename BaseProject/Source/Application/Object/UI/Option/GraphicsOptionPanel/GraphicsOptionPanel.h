#pragma once

#include "Application/Object/UI/Option/ScrollPanelBase.h"
#include "Application/Object/UI/Option/OptionEnumSelector.h"

#include "../../../../Game/UserData/UserData.h"

namespace App::Object
{
	//===============================================================================================
	// 設定画面のグラフィック設定
	//
	// 配置と記憶のみ
	// スクロール処理はベースクラスに任せる
	// 描画範囲は親に任せる
	//
	//   Display  … ウィンドウモード・明るさ・ブルーム
	//   Gameplay … 画面の揺れ・HUD の表示
	//
	// ・値の実体は UserData(GraphicsSettings)。ここが持つのは表示用の写しで、開いたときに読み直す。
	// ・変えた瞬間に UserData へ書き、エンジンへも流し込む(作り手の値の上へ重ねる。エンジン設定は書き換えない)。
	// ・画面の揺れは値を持つだけ。揺らす側(カメラ)ができたら UserData から読んで掛けること。
	//===============================================================================================
	class GraphicsOptionPanel final : public ScrollPanelBase
	{
	public:

		// パネルの名前(保存の区切りにも使うので変えないこと)
		const char* GetPanelName() const override { return "Graphics"; }

		// 開かれたとき : 写しを UserData から読み直す
		void OnOpen(Engine::GameObject::ObjectContext& a_context) override;

		// プルダウンが開いている間は、戻るキーをプルダウンを閉じるのに使う
		bool IsCapturingInput() const override { return m_windowModeSelector.IsOpen(); }

	protected:

		// 並べる : 見出し → 画面の行 → 余白 → 見出し → 遊びの行
		void LayoutItems(Engine::GameObject::ObjectContext& a_context) override;

		// 項目の操作
		void UpdateItems(Engine::GameObject::ObjectContext& a_context) override;

		// 保存
		void ArchiveItems(Engine::Persistence::Archive& a_ar) override;

		// インスペクター
		void DrawItemsInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 写しを UserData へ書いて、エンジンへ流し込む
		void ApplySettings(Engine::GameObject::ObjectContext& a_context);

		// 今のウィンドウモード : 選ばれていなければウィンドウの今の状態
		Engine::EWindowMode GetCurrentWindowMode(Engine::GameObject::ObjectContext& a_context) const;

	private:

		// ボタン1回で動く量(%)
		static constexpr uint32_t BRIGHTNESS_STEP = 5;
		static constexpr uint32_t SCREEN_SHAKE_STEP = 10;

		// 表示用の写し(実体は UserData)
		Game::UserData::GraphicsSettings m_settings = {};

		//------------------------------------------------------------------------------
		// 画面
		//------------------------------------------------------------------------------
		OptionItemUI m_displayHeader = {};				// 見出し。未設定なら行を取らない
		// ウィンドウモード(プルダウンの候補の上からの並び)
		OptionEnumSelector<Engine::EWindowMode> m_windowModeSelector = {
			{ Engine::EWindowMode::Windowed,    "Windowed" },
			{ Engine::EWindowMode::Borederless, "Borderless" },
			{ Engine::EWindowMode::FullScreen,  "Fullscreen" },
		};
		NumericMutate m_brightnessNumeric = {};			// 明るさ(%)
		OptionToggleUI m_bloomToggle = {};			// ブルーム(オン/オフ)

		//------------------------------------------------------------------------------
		// 遊び
		//------------------------------------------------------------------------------
		float m_sectionSpace = 32.0f;					// 画面と遊びの間(px)
		OptionItemUI m_gameplayHeader = {};				// 見出し。未設定なら行を取らない
		NumericMutate m_screenShakeNumeric = {};		// 画面の揺れ(%)
		OptionToggleUI m_hudToggle = {};			// HUD の表示(オン/オフ)
	};
}
