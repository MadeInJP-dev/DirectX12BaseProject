#pragma once

#include <variant>	// 入力の割り当ては軸とボタンのどちらかを持つ

#include "Engine/Input/Core/GamepadKey.h"	// コントローラーのボタンの番号

namespace App::Game
{
	//------------------------------------------------------------------------------
	// アクション一覧
	//------------------------------------------------------------------------------
	enum class EGameAction : uint32_t
	{
		//--------------------------------------------------------------------------
		// プレイヤーアクション
		Look,			// 視点操作
		Move,			// 移動 : 左右前後
		Down,			// 下方向移動
		Boost,			// ブースト : 移動入力がなかったら上方向
		ChargeBoost,	// チャージブースト : ブーストダッシュ中はBoostボタンで上に移動
		RWeaponAttack,	// 右の武器のアタック
		LWeaponAttack,	// 左の武器のアタック
		RMissileLock,	// 右肩のミサイルの照準開始、離すと発射
		LMissileLock,	// 左肩のミサイルの照準開始、離すと発射
		Pose,			// 一時停止

		GameActionMax,		// ここまでがゲーム側のアクション
		//--------------------------------------------------------------------------
		// UIアクション
		Select,			// 選択
		Back,			// 一つ戻る(Esc / 右クリック / B)。HomeSequence・GarageSequence・設定画面が読む
		Menu,			// オプションやタイトルに戻るが出てくる(ホームの左上のメニュー SideMenu を開閉する。Tab / Start)
		BottonMove,		// 選択できる項目のみを順番に移動できる(上・左で前、下・右で次。UINavigationGroup が読む)
		NextItem,		// 次の項目へ移る(RB。キーボードは Tab をメニューへ回したので割り当て無し)。UINavigationGroup が読む
		PrevItem,		// 前の項目へ移る(LB)。UINavigationGroup が読む
		Decide,			// 項目の移動で選んでいるものを決める(Select はカーソルで押すほう)
		//--------------------------------------------------------------------------
	};

	//------------------------------------------------------------------------------
	// 入力設定
	//------------------------------------------------------------------------------
	// アクション1つに対して、押すキー(ボタン)か4方向のキー(軸)のどちらかを持つ。
	// コントローラーはキーの代わりに GamepadKey の番号を入れる。
	// 軸はスティックなので、4方向にはそのスティックの上右下左を入れる。

	struct ButtonInputData
	{
		int key;
	};

	struct AxisInputData
	{
		int up;
		int right;
		int down;
		int left;
	};

	using InputBindingData = std::variant<ButtonInputData, AxisInputData>;

	struct InputSettings
	{
		std::unordered_map<Game::EGameAction, InputBindingData> keyboard;
		std::unordered_map<Game::EGameAction, InputBindingData> mouse;
		std::unordered_map<Game::EGameAction, InputBindingData> controller;

		float mouseSensitivity = 1.0f;		// マウス感度 : 作り手の感度(エンジンの InputOption)へ掛ける倍率

		// 視点の上下反転。未選択(nullopt)なら作り手の値(エンジンの InputOption)のまま
		std::optional<bool> opIsInvertLookY = std::nullopt;

		// コントローラーの視点(スティック)。マウスとは別に持つ
		float stickSensitivity = 1.0f;		// スティック感度 : 視点の速さ(InputActionManager の STICK_LOOK_SPEED)へ掛ける倍率
		bool isInvertStickY = false;		// スティックの上下反転
	};

	//------------------------------------------------------------------------------
	// アクション1つの読み書き
	//------------------------------------------------------------------------------
	// 「どのアクションで反応するか」を持つオブジェクト(UIのボタンなど)用。
	//
	// 保存は enum の値ではなく名前。数値のままだと、アクションを足したり
	// 並べ替えたりしただけで別のアクションを指してしまう。
	// 読めない名前(消えたアクションや、名前で持っていた頃の古いデータ)は
	// 呼び出し側の既定を残す。
	//------------------------------------------------------------------------------
	inline void ActionField(
		Engine::Persistence::Archive& a_ar, const std::string& a_name, EGameAction& a_action)
	{
		std::string _actionName{ magic_enum::enum_name(a_action) };
		a_ar.StringField(a_name, _actionName);

		if (!a_ar.IsLoading()) return;

		const auto _loaded = magic_enum::enum_cast<EGameAction>(_actionName);
		if (_loaded.has_value()) a_action = _loaded.value();
	}

	//------------------------------------------------------------------------------
	// 既定の割り当て
	//------------------------------------------------------------------------------
	// ユーザーデータに何も入っていないときと、エディターの「既定へ戻す」で使う。
	// 後からアクションを足したときも、保存済みのデータに無いものはここから補う。
	//
	// 視点のマウス軸(EGameAction::Look)はキーではないので、ここには入れない。
	// 割り当てに関係なく InputActionManager がマウスへ積む。
	//------------------------------------------------------------------------------
	inline InputSettings MakeDefaultInputSettings()
	{
		InputSettings _settings;

		// ---- キーボード / マウスボタン ----
		_settings.keyboard[EGameAction::Move] = AxisInputData{ 'W', 'D', 'S', 'A' };			// 移動
		_settings.keyboard[EGameAction::Look] = AxisInputData{ VK_UP, VK_RIGHT, VK_DOWN, VK_LEFT };	// 視点(キー操作)

		_settings.keyboard[EGameAction::Boost]       = ButtonInputData{ VK_LSHIFT };			// ブースト
		_settings.keyboard[EGameAction::ChargeBoost] = ButtonInputData{ VK_SPACE };			// チャージブースト
		_settings.keyboard[EGameAction::Down]        = ButtonInputData{ VK_LCONTROL };		// 下方向ブースト

		_settings.keyboard[EGameAction::LWeaponAttack] = ButtonInputData{ VK_LBUTTON };		// 左武器攻撃
		_settings.keyboard[EGameAction::RWeaponAttack] = ButtonInputData{ VK_RBUTTON };		// 右武器攻撃

		_settings.keyboard[EGameAction::LMissileLock] = ButtonInputData{ 'Q' };				// 左肩ミサイル
		_settings.keyboard[EGameAction::RMissileLock] = ButtonInputData{ 'E' };				// 右肩ミサイル

		_settings.keyboard[EGameAction::Pose] = ButtonInputData{ VK_ESCAPE };				// 一時停止

		// ---- UI ----
		_settings.keyboard[EGameAction::Select]     = ButtonInputData{ VK_LBUTTON };			// 決定
		_settings.keyboard[EGameAction::Back]       = ButtonInputData{ VK_ESCAPE };			// 戻る(一時停止と同じキー。ゲーム中は戻るを読む相手が居ない)
		_settings.keyboard[EGameAction::Menu]       = ButtonInputData{ VK_TAB };				// メニュー(ホームの左上のメニューを開閉する)
		_settings.keyboard[EGameAction::BottonMove] = AxisInputData{ VK_UP, VK_RIGHT, VK_DOWN, VK_LEFT };	// 項目の移動
		_settings.keyboard[EGameAction::Decide]     = ButtonInputData{ VK_RETURN };			// 選んでいる項目を決める
		// 次の項目へ(NextItem)はキーボードでは割り当てない(Tab はメニューへ回した。項目移動は矢印キーで)

		// ---- マウス(キーボードと別に、もう1つ割り当てられる) ----
		_settings.mouse[EGameAction::Back] = ButtonInputData{ VK_RBUTTON };					// 戻る(右クリック)

		// ---- コントローラー ----
		namespace Pad = Engine::Input::GamepadKey;
		const AxisInputData _leftStick  = { Pad::LThumbUp, Pad::LThumbRight, Pad::LThumbDown, Pad::LThumbLeft };
		const AxisInputData _rightStick = { Pad::RThumbUp, Pad::RThumbRight, Pad::RThumbDown, Pad::RThumbLeft };

		_settings.controller[EGameAction::Move] = _leftStick;									// 移動
		_settings.controller[EGameAction::Look] = _rightStick;									// 視点

		_settings.controller[EGameAction::Boost]       = ButtonInputData{ Pad::A };				// ブースト
		_settings.controller[EGameAction::ChargeBoost] = ButtonInputData{ Pad::X };				// チャージブースト
		_settings.controller[EGameAction::Down]        = ButtonInputData{ Pad::B };				// 下方向ブースト

		_settings.controller[EGameAction::LWeaponAttack] = ButtonInputData{ Pad::LTrigger };		// 左武器攻撃
		_settings.controller[EGameAction::RWeaponAttack] = ButtonInputData{ Pad::RTrigger };		// 右武器攻撃

		_settings.controller[EGameAction::LMissileLock] = ButtonInputData{ Pad::LShoulder };		// 左肩ミサイル
		_settings.controller[EGameAction::RMissileLock] = ButtonInputData{ Pad::RShoulder };		// 右肩ミサイル

		_settings.controller[EGameAction::Pose] = ButtonInputData{ Pad::Start };					// 一時停止

		// UI
		_settings.controller[EGameAction::Select]     = ButtonInputData{ Pad::A };				// 決定
		_settings.controller[EGameAction::Back]       = ButtonInputData{ Pad::B };				// 戻る
		_settings.controller[EGameAction::Menu]       = ButtonInputData{ Pad::Start };			// メニュー
		_settings.controller[EGameAction::BottonMove] = _leftStick;								// 項目の移動(十字キーも効く : InputActionManager)
		_settings.controller[EGameAction::NextItem]   = ButtonInputData{ Pad::RShoulder };		// 次の項目へ
		_settings.controller[EGameAction::PrevItem]   = ButtonInputData{ Pad::LShoulder };		// 前の項目へ
		_settings.controller[EGameAction::Decide]     = ButtonInputData{ Pad::A };				// 選んでいる項目を決める

		return _settings;
	}
}