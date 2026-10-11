#pragma once

#include "../ScrollPanelBase.h"

#include "../../../../Game/Core/InputSettings.h"

#include <bitset>	// キーの待ち受け : 前のフレームに押されていたキー

namespace App::Object
{
	// 軸(4方向のキー)のうち、どの向きのキーを変えるか
	enum class EAxisDirection : uint32_t
	{
		None,	// 軸ではない(ボタン1つのアクション)
		Up,		// 上・前(移動なら W)
		Right,	// 右(D)
		Down,	// 下・後(S)
		Left,	// 左(A)
	};

	// 画面上では縦に配列で並べられる項目の一つ : アクション1つぶんのキー割り当て
	//
	// 移動のように軸(4方向のキー)で持つアクションは、向きごとに1行ずつ作る(axisDirection)
	struct ButtonElement
	{
		Game::EGameAction action = Game::EGameAction::Boost;	// 割り当てを変えるアクション
		EAxisDirection axisDirection = EAxisDirection::None;	// 軸のアクションなら、どの向きのキーか
		OptionItemUI rowUI = {};		// 行の UI(UIButton)。押すとキーの待ち受けに入る
		uint32_t keyTextId = 0;			// 今のキー名を書き込む文字の飾り(行の UI の飾りの番号)。0 で書かない

		int currentActionKey = -1;		// 今の割り当て(仮想キーコード)。表示用の写しで保存しない。-1 で未設定
	};

	//===============================================================================================
	// 設定画面の入力設定
	//
	// 配置と記憶のみ
	// スクロール処理はベースクラスに任せる
	// 描画範囲は親に任せる
	//
	// ・値の実体は UserData(InputSettings)。ここが持つのは表示用の写しで、開いたときに読み直す。
	// ・変えた瞬間に UserData へ書き、キー割り当ては InputActionManager::Apply で入力へ流し直す。
	//   ファイルへの保存は OptionHUD が閉じたときにまとめて行う。
	// ・キーの割り当ては「行を押す → その後に新しく押されたキー」で決める。
	//   移動のように軸(4方向のキー)で持つアクションは、向きごとの行でその向きのキーだけを変える。
	//===============================================================================================
	class InputOptionPanel final : public ScrollPanelBase
	{
	public:

		// 既定の行 : ゲーム側のアクションのうち、ボタン1つで持つものを全部並べる
		InputOptionPanel();

		// パネルの名前(保存の区切りにも使うので変えないこと)
		const char* GetPanelName() const override { return "Input"; }

		// 開かれたとき : 感度とキー割り当ての写しを UserData から読み直す
		void OnOpen(Engine::GameObject::ObjectContext& a_context) override;

		// キーの待ち受け中か : 押したキーで画面が閉じないよう OptionHUD が見る
		bool IsCapturingInput() const override { return m_waitingIndex >= 0; }

	protected:

		// 並べる : 視点側の設定(見出し・感度) → 余白 → ボタン側の設定(見出し・アクション)
		void LayoutItems(Engine::GameObject::ObjectContext& a_context) override;

		// 項目の操作
		void UpdateItems(Engine::GameObject::ObjectContext& a_context) override;

		// 保存
		void ArchiveItems(Engine::Persistence::Archive& a_ar) override;

		// インスペクター
		void DrawItemsInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 並べる : 視点側(感度の行)
		void LayoutAxisSettings(Engine::GameObject::ObjectContext& a_context);

		// 並べる : ボタン側(アクションごとの行)
		void LayoutButtonSettings(Engine::GameObject::ObjectContext& a_context);

		// マウス、コントローラーの軸などの視点側の設定
		void AxisSettings(Engine::GameObject::ObjectContext& a_context);

		// キーボード、コントローラーなどのボタン側の設定
		void ButtonSettings(Engine::GameObject::ObjectContext& a_context);

		// 感度を書き込む : 0.01 単位に丸めて範囲に詰め、UserData へ入れて入力へ流し込む
		void ApplySensitivity(Engine::GameObject::ObjectContext& a_context, float a_sensitivity);

		// 上下反転を書き込む : UserData へ入れて入力へ流し込む
		void ApplyInvertLookY(Engine::GameObject::ObjectContext& a_context, bool a_isInvert);

		// 視点の設定(感度・上下反転)を入力へ流し込む
		void ApplyLookSettings(Engine::GameObject::ObjectContext& a_context);

		// 今の上下反転 : 選ばれていなければ入力が今使っている値
		bool IsCurrentInvertLookY(Engine::GameObject::ObjectContext& a_context) const;

		// キーの割り当てを書き込む : UserData へ入れて、入力デバイスへ流し直す
		void ApplyButtonBinding(Engine::GameObject::ObjectContext& a_context, ButtonElement& a_element, int a_key);

		// 行の文字の飾りへ今のキー名を書く(待ち受け中は "...")
		void RefreshKeyText(Engine::GameObject::ObjectContext& a_context, const ButtonElement& a_element, bool a_isWaiting);

		// 今押されているキーを覚え直す(待ち受けに入った瞬間)。ここで押されていたキーは割り当てない
		void CaptureKeyState();

		// 前のフレームから新しく押されたキー(仮想キーコード)を1つ探す。無ければ -1
		int FindNewlyPressedKey();

		// 割り当ての対象にしないキーか(IME の切り替えなど)
		static bool IsIgnoredKey(int a_key);

		// キーの表示名
		static std::string MakeKeyName(int a_key);

	private:

		// 感度の刻みと範囲
		static constexpr float SENSITIVITY_STEP = 0.01f;
		static constexpr float SENSITIVITY_MIN = 0.01f;
		static constexpr float SENSITIVITY_MAX = 10.0f;

		// 仮想キーコードの数(0x00〜0xFF)
		static constexpr size_t KEY_CODE_COUNT = 256;

		//------------------------------------------------------------------------------
		// マウス、コントローラーの軸などの視点側の設定
		//------------------------------------------------------------------------------
		float m_moveSensitivity = 1.0f;			// 感度設定 : 0.01単位での変更(表示用の写し。実体は UserData)

		OptionItemUI m_axisHeader = {};				// 見出し(文字だけの UI)。未設定なら行を取らない
		OptionStepperUI m_sensitivityStepper = {};	// 感度のゲージと上げ下げのボタン

		// 上下反転(表示用の写し。未選択なら作り手の値のまま)
		std::optional<bool> m_opIsInvertLookY = std::nullopt;
		OptionSelectorUI m_invertLookYSelector = {};	// 上下反転(オン/オフ)

		//------------------------------------------------------------------------------
		// キーボード、コントローラーなどのボタン側の設定
		//------------------------------------------------------------------------------
		float m_sectionSpace = 32.0f;			// 視点側とボタン側の間(px)
		OptionItemUI m_buttonHeader = {};		// 見出し(文字だけの UI)。未設定なら行を取らない
		std::vector<ButtonElement> m_buttonVec = {};

		//------------------------------------------------------------------------------
		// キーの待ち受け(保存しない)
		//------------------------------------------------------------------------------
		int m_waitingIndex = -1;				// 待ち受け中の行(m_buttonVec の添え字)。-1 で待っていない

		// 行のクリックを受け付けるか。割り当てた直後は切り、次に押し始めたら入れる。
		// 左クリックを割り当てたとき、その離した瞬間が行のクリックになって
		// すぐまた待ち受けに入ってしまうのを防ぐ
		bool m_isRowClickArmed = true;

		// 前のフレームに押されていたキー(仮想キーコードが添え字)。
		// 「押された瞬間」だけを拾うために持つ。押しっぱなしのキーや、
		// 押された扱いのまま戻らないキー(日本語キーボードの IME のキー)を拾わないため
		std::bitset<KEY_CODE_COUNT> m_keyDownBits = {};
	};
}
