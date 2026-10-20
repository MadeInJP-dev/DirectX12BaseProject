#pragma once

#include "../../Core/InputSettings.h"

namespace Engine::Input
{
	class InputManager;
}

namespace App::Game
{
	class UserData;
}

namespace App::Input
{
	// 入力デバイス一覧
	enum class EInputDevice
	{
		KeyboardAndMouse,
		Controller
	};
	ENUM_ATTR_BITFLAG(EInputDevice);

	/// <summary>
	/// ゲームのアクションと実際のキーの結び付けを持つクラス
	/// </summary>
	/// <remarks>
	/// 割り当ての実体はユーザーデータ(UserData::InputSettings)側にあり、
	/// ここはそれを入力デバイスへ流し込む役。
	///
	///   起動時 : ユーザーデータを読み、足りないアクションを既定で補って Apply
	///   編集時 : ユーザーデータを書き換えて Apply → そのまま保存
	///
	/// 既定の割り当ては MakeDefaultInputSettings()(InputSettings.h)が持つ。
	/// 「既定へ戻す」もユーザーデータをそれで置き換えるだけ。
	/// </remarks>
	class InputActionManager
	{
	public:

		// 登録するデバイスの名前
		// (AddDevice は同じ名前で束ごと上書きする。他所から同名で登録しないこと)
		static constexpr const char* DEVICE_KEYBOARD = "Keyboard";
		static constexpr const char* DEVICE_MOUSE = "Mouse";
		static constexpr const char* DEVICE_CONTROLLER = "Controller";

		// ユーザーデータから入力設定情報を復元
		// a_pInputManager : 割り当てを流し込む先(借り物。持ち主は MainEngine)
		void Init(Game::UserData* a_pUserData, Engine::Input::InputManager* a_pInputManager);

		// 今の設定を入力デバイスへ反映する : 割り当てを変えたら必ず通す
		void Apply();

		// 既定の割り当てへ戻す(反映と保存まで行う)
		void ResetToDefault();

	private:

		// エディターに登録する関数。UI側からいじれるようになるまでのつなぎ
		void Edit();

		// 保存されていないアクションを既定で埋める
		// (後からアクションを足したとき、古い保存データに無い分がここで入る)
		void ComplementWithDefault(Game::InputSettings& a_settings) const;

		// ユーザーデータへ反映した後の後始末 : デバイスへ流して保存する
		void ApplyAndSave();

	private:

		// 保存先のポインタ
		Game::UserData* m_pUserData = nullptr;

		// 割り当ての流し込み先
		Engine::Input::InputManager* m_pInputManager = nullptr;

		// 既定の割り当て : 「既定へ戻す」と、保存に無いアクションの穴埋めに使う
		Game::InputSettings m_defaultSettings;

		// 現在接続されているデバイス一覧
		EInputDevice m_conectedDevices;

		// 優先されているデバイス : UIの表示用。直近触ったデバイスが登録される
		EInputDevice m_device;
		float m_durationTime = 0.0f;		// 切り替え間隔時間、意図しない入力でちらつきを抑えるため


	};
}
