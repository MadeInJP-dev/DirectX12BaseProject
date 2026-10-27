#pragma once

#include "Application/Object/UI/Option/ScrollPanelBase.h"
#include "Application/Object/UI/Option/OptionEnumSelector.h"

#include "../../../../Game/UserData/UserData.h"

namespace App::Object
{
	//===============================================================================================
	// 設定画面のゲーム設定
	//
	// 配置と記憶のみ
	// スクロール処理はベースクラスに任せる
	// 描画範囲は親に任せる
	//
	//   Performance … FPS の上限・FPS の表示(右上)
	//
	// ・値の実体は UserData(GameSettings)。ここが持つのは表示用の写しで、開いたときに読み直す。
	// ・変えた瞬間に UserData へ書き、エンジンへも流し込む(エンジン設定は書き換えない)。
	//   まだ選ばれていない間は、作り手の値(エンジンの WindowOption)のまま動く。
	//===============================================================================================
	class GameOptionPanel final : public ScrollPanelBase
	{
	public:

		// パネルの名前(保存の区切りにも使うので変えないこと)
		const char* GetPanelName() const override { return "Game"; }

		// 開かれたとき : 写しを UserData から読み直す
		void OnOpen(Engine::GameObject::ObjectContext& a_context) override;

		// プルダウンが開いている間は、戻るキーをプルダウンを閉じるのに使う
		bool IsCapturingInput() const override { return m_fpsRateSelector.IsOpen(); }

	protected:

		// 並べる : 見出し → FPS の上限 → FPS の表示
		void LayoutItems(Engine::GameObject::ObjectContext& a_context) override;

		// 項目の操作
		void UpdateItems(Engine::GameObject::ObjectContext& a_context) override;

		// 保存
		void ArchiveItems(Engine::Persistence::Archive& a_ar) override;

		// インスペクター
		void DrawItemsInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 今の FPS の上限 : 選ばれていなければエンジンが今使っている値
		Engine::EFPSRate GetCurrentFPSRate(Engine::GameObject::ObjectContext& a_context) const;

		// 写しを UserData へ書く
		void WriteSettings(Engine::GameObject::ObjectContext& a_context);

	private:

		// 表示用の写し(実体は UserData)
		Game::UserData::GameSettings m_settings = {};

		OptionItemUI m_performanceHeader = {};		// 見出し。未設定なら行を取らない
		// FPS の上限(プルダウンの候補の上からの並び)
		OptionEnumSelector<Engine::EFPSRate> m_fpsRateSelector = {
			{ Engine::EFPSRate::FPS_30,    "30" },
			{ Engine::EFPSRate::FPS_60,    "60" },
			{ Engine::EFPSRate::FPS_120,   "120" },
			{ Engine::EFPSRate::FPS_144,   "144" },
			{ Engine::EFPSRate::FPS_240,   "240" },
			{ Engine::EFPSRate::VSync,     "VSync" },
			{ Engine::EFPSRate::Unlimited, "Unlimited" },
		};
		OptionToggleUI m_fpsVisibleToggle = {};		// FPS の表示(オン/オフ)。画面の右上に出す
	};
}
