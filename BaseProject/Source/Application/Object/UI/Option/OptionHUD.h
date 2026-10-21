#pragma once

#include "Application/Object/UI/UIBase.h"
#include "ScrollPanelBase.h"

#include "../../../Game/Core/InputSettings.h"

namespace App::Object
{
	//===============================================================================================
	// 設定画面
	//
	// 項目を抱える、中身は各パネルに任せる
	//
	// 設定画面はどこで開いても同じなので、専用のシーンに1つだけ置く。
	//   開く   … 開きたい側(ポーズ・ホームなど)が、このシーンを重ねる(Push)
	//   閉じる … 戻るボタンか閉じるキーで、UserData をファイルへ保存してから自分のシーンを外す(Pop)
	//
	//   ┌──────────────────────────────┐ ← OptionHUD(アンカーの矩形と、背景の飾り)
	//   │ [タブ]  ┌─────────────────────┐│
	//   │ [タブ]  │                     ││ ← 選んでいるパネルの描画範囲(項目とスクロール)
	//   │ [タブ]  │                     ││
	//   │ [タブ]  │                     ││
	//   │         │                     ││
	//   │ [戻る]  └─────────────────────┘│
	//   └──────────────────────────────┘
	//
	// ・配置の初期値(上の図)はここが持つ。基準は 1920x1080 で、すべてアンカーの位置からのずれ。
	//   OptionHUD を動かせば全部一緒に動く。
	// ・タブ・戻るボタン・パネルの項目とバーはシーンに置いた別の UI(UIButton など)で、GUID で指す。
	//   位置と当たり判定の大きさはここが毎フレーム入れるので、シーン側は置いて GUID を指すだけでよい
	//   (見た目 = 飾りの大きさは、それぞれの UI で作る)。
	// ・パネル(ScrollPanelBase の派生)はここが作って持つ。出ているのは選んでいるパネル1つだけ。
	// ・値そのものは変えた瞬間に UserData へ入る。開いた瞬間に各パネルが UserData から読み直す。
	// ・戻る・タブ・各パネルの根は、OptionHUD の表示に合わせてここが毎フレーム出し入れする
	//   (下に置いた UI へは表示も伝わるが、OptionHUD の外に置かれても同じに動くよう、ここでも入れている)。
	//   エディターでも同じなので、パネルの中身を触るときはインスペクターでそのパネルを選ぶこと。
	//===============================================================================================
	class OptionHUD : public UIBase
	{
	public:

		// 押されることのない入れ物なので、カーソルへの反応を持たない。
		// パネルと、配置・背景の初期値もここで作る
		OptionHUD();

		// 更新前処理 : タブ・戻るボタンと、選んでいるパネルの項目を並べる(項目の当たり判定より先に位置を決める)
		void PreUpdate(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : 開いた瞬間の読み直し・ボタンと閉じるキー・パネルの出し入れと操作
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// パネル
		//=======================================================================

		// パネルを選ぶ(範囲の外なら何もしない)
		void SelectPanel(size_t a_index);

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "OptionHUD"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 開いた瞬間 : パネルに値を読み直させる
		void OnOpen(Engine::GameObject::ObjectContext& a_context);

		// 閉じる : UserData をファイルへ保存してから、自分のシーンを外す
		void ReserveClose(Engine::GameObject::ObjectContext& a_context);

		// 戻る・タブのボタンと、閉じるキーを見る
		void UpdateButtons(Engine::GameObject::ObjectContext& a_context);

		// タブ・戻るボタンを置く
		void PlaceButtons(Engine::GameObject::ObjectContext& a_context);

		// 戻る・タブ・パネルの根を出し入れする
		void ApplyVisibility(Engine::GameObject::ObjectContext& a_context, bool a_isOpen);

		// パネルへ渡す並べ方を作る
		PanelLayout MakePanelLayout() const;

		// 選んでいるパネルの添え字(選んでいなければ -1)
		int GetSelectIndex() const;

	private:

		//------------------------------------------------------------------------------
		// オプション画面のボタン(保存される)
		//------------------------------------------------------------------------------
		Core::GUID m_backButtonGUID = {};					// 前の画面に戻る用の×
		std::vector<Core::GUID> m_tabButtonGUIDVec = {};	// パネルを切り替えるボタン(m_upPanelVec と同じ並び)

		// 閉じるのにも使う入力アクション
		Game::EGameAction m_closeAction = Game::EGameAction::Back;

		// 開いている間はカーソルの中央固定を切るか(固定したままだとボタンを狙えない)。
		// 閉じても戻さない : ゲーム中はポーズ画面から開く前提で、戻すのはポーズ画面の役
		bool m_isReleaseCursorLock = true;

		//------------------------------------------------------------------------------
		// パネル
		//------------------------------------------------------------------------------
		std::vector<std::unique_ptr<ScrollPanelBase>> m_upPanelVec = {};	// 保持しているパネルたち
		ScrollPanelBase* m_pSelectPanel = nullptr;							// 選択中のパネル

		//------------------------------------------------------------------------------
		// 配置(保存される)。基準は 1920x1080、ずれはすべてアンカーの位置から
		//------------------------------------------------------------------------------
		// タブ : 左上から縦に並べる
		Math::Vector2 m_tabOffset = { -820.0f, -420.0f };	// 一番上のタブの左上
		Math::Vector2 m_tabSize = { 340.0f, 72.0f };		// タブ1つの大きさ(px)
		float m_tabSpacing = 32.0f;							// タブとタブの間(px)

		// 戻るボタン : 左下
		Math::Vector2 m_backButtonOffset = { -840.0f, 380.0f };	// 左上
		Math::Vector2 m_backButtonSize = { 220.0f, 72.0f };		// 大きさ(px)

		// 各パネルの描画範囲 : 右側の大きな枠
		Math::Vector2 m_panelOffset = { -400.0f, -430.0f };	// 左上
		Math::Vector2 m_panelSize = { 1260.0f, 880.0f };	// 大きさ(px)。右端はスクロールバーが使う

		// パネル一つごとの項目のUIサイズ
		Math::Vector2 m_itemSize = { 1220.0f, 80.0f };	// 項目1つの大きさ(px)。行の送りに使う
		float m_itemSpacing = 12.0f;					// 行と行の間(px)

		//------------------------------------------------------------------------------
		// 状態(保存しない)
		//------------------------------------------------------------------------------
		bool m_wasOpen = false;		// 前のフレームに出ていたか(開いた瞬間を見る)
		bool m_isClosing = false;	// 閉じる要求を出したか(二重に Pop しないため)
	};
}
