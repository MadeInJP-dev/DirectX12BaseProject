#pragma once

#include "Core/InputDevice.h"

// XInput はコントローラーを扱うところだけで使う。
// プリコンパイル済みヘッダーへ置くと全翻訳単位に広がるため
#include <Xinput.h>

namespace Engine::Input
{
	//=================================================================================================
	// 入力が来るデバイスを管理するもの
	//
	// 入力自体は InputMnager が管理しているのでここでは接続されているデバイスを管理する
	//
	// 接続状態の管理				- デバイスが接続されたか、切断されたかを管理
	// アクティブデバイスの管理		- 最後に操作が行われたデバイスを管理
	// 状態変化の通知				- 接続・切断やアクティイブデバイスの変更をシステムに通知する
	// デバイス情報の提供			- 接続中のデバイスやその種類などを他システムから参照できるようにする
	//=================================================================================================
	class InputDeviceManager
	{
	public:

		InputDeviceManager() = default;
		~InputDeviceManager() = default;
		NON_COPYABLE_NON_MOVABLE(InputDeviceManager);

		//--------------------------------------------------------------------------------------------
		// 初期化
		//--------------------------------------------------------------------------------------------
		void Init();

		//--------------------------------------------------------------------------------------------
		// 更新
		//--------------------------------------------------------------------------------------------
		// デバイス検知と、最後に触ったデバイスの判定
		// 生のマウス移動量・ホイールは InputManager が読み出すと0に戻るので、それより前に呼ぶ
		void Update();

		//--------------------------------------------------------------------------------------------
		// アクセサ
		//--------------------------------------------------------------------------------------------
		EInputDevice GetActiveDevice() const { return m_activeDevice; }
		bool IsConected(EInputDevice a_inputDevice) const;

		// このフレームのコントローラーの入力。繋がっていなければ nullptr
		// XInput は1台につき1フレーム1回だけここで読み、ボタンや軸はこれを見る
		const XINPUT_GAMEPAD* GetGamepadState(UINT a_userIndex) const;

	private:

		// ゲームパッドの状態を読み、接続されているかどうかを返す
		// 触られていれば a_outIsTouched が true になる
		bool UpdateGamepad(bool& a_outIsTouched);

		// キーボードのキー・マウスのボタンを読み、このフレームで押されたものがあるかを返す
		bool UpdateKeys();

		// マウスが動かされたか・ホイールが回されたか
		bool IsMouseMoved() const;

	private:

		// 最後の入力があったデバイス
		EInputDevice m_activeDevice = EInputDevice::None;

		// 現在接続されているデバイスたち
		std::unordered_map<EInputDevice, bool> m_conectedDevices = {};

		// このフレームのコントローラーの接続と入力(1台ごと)
		// 最後に触ったデバイスの判定では、書き換える前の値を前フレームとして使う
		std::array<bool, XUSER_MAX_COUNT> m_gamepadConected = {};
		std::array<XINPUT_GAMEPAD, XUSER_MAX_COUNT> m_gamepadStates = {};

		// 最後に触ったデバイスの判定に使う、前フレームのキーの押下(仮想キーごと)
		// 「押されている」ではなく「押された瞬間」で判定するため。
		// 押しっぱなしやスティックの戻りの悪さで、別のデバイスへ切り替えられなくなるのを防ぐ
		std::bitset<256> m_prevKeyDown = {};
	};
}
