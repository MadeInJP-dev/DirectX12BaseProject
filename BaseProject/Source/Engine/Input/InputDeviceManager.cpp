#include "InputDeviceManager.h"

#include "../MainEngine.h"
#include "../Window/NativeWindow.h"

namespace Engine::Input
{
	namespace
	{
		// マウスが動いたとみなす移動量(マウスのカウント)
		// 机に触れた程度の小さな揺れでは切り替えない
		constexpr int MOUSE_MOVE_THRESHOLD = 2;

		// スティックを倒したまま動かしたとみなす1フレームの移動量
		// 倒しっぱなしの細かな揺れでは切り替えない
		constexpr float STICK_MOVE_THRESHOLD = 3000.0f;

		// スティックが遊びの外まで倒れているか
		bool IsStickOutsideDeadzone(SHORT a_x, SHORT a_y, SHORT a_deadzone)
		{
			const float _x = static_cast<float>(a_x);
			const float _y = static_cast<float>(a_y);
			const float _deadzone = static_cast<float>(a_deadzone);
			return (_x * _x + _y * _y) > (_deadzone * _deadzone);
		}

		// スティックが触られたか : 倒し始めた、または倒したまま大きく動かした
		bool IsStickTouched(SHORT a_x, SHORT a_y, SHORT a_prevX, SHORT a_prevY, SHORT a_deadzone)
		{
			if (!IsStickOutsideDeadzone(a_x, a_y, a_deadzone)) return false;
			if (!IsStickOutsideDeadzone(a_prevX, a_prevY, a_deadzone)) return true;

			const float _dx = static_cast<float>(a_x) - static_cast<float>(a_prevX);
			const float _dy = static_cast<float>(a_y) - static_cast<float>(a_prevY);
			return (_dx * _dx + _dy * _dy) > (STICK_MOVE_THRESHOLD * STICK_MOVE_THRESHOLD);
		}

		// トリガーが引かれたか : しきい値をまたいだ瞬間
		bool IsTriggerTouched(BYTE a_trigger, BYTE a_prevTrigger)
		{
			return (a_trigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
				&& (a_prevTrigger <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
		}

		// ゲームパッドが前フレームから触られたか
		bool IsGamepadTouched(const XINPUT_GAMEPAD& a_now, const XINPUT_GAMEPAD& a_prev)
		{
			// 新しく押されたボタン(離しただけでは触ったことにしない)
			if ((a_now.wButtons & ~a_prev.wButtons) != 0) return true;

			if (IsTriggerTouched(a_now.bLeftTrigger, a_prev.bLeftTrigger)) return true;
			if (IsTriggerTouched(a_now.bRightTrigger, a_prev.bRightTrigger)) return true;

			if (IsStickTouched(a_now.sThumbLX, a_now.sThumbLY, a_prev.sThumbLX, a_prev.sThumbLY,
				XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)) return true;
			if (IsStickTouched(a_now.sThumbRX, a_now.sThumbRY, a_prev.sThumbRX, a_prev.sThumbRY,
				XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)) return true;

			return false;
		}
	}

	void InputDeviceManager::Init()
	{
		// キーボードマウスは接続されている前提
		m_conectedDevices[EInputDevice::KeyboardMouse] = true;
	}

	//==================================================================================
	// デバイス検知と、最後に触ったデバイスの判定
	//----------------------------------------------------------------------------------
	// 押しっぱなしのキーや倒しっぱなしのスティックでは切り替えず、
	// 押した瞬間・倒し始めた瞬間・動かした瞬間だけを「触った」とする。
	// キーボードとマウスは1つのデバイスとして扱う(キーで動きながらマウスで
	// 視点を回すたびに切り替わらないように)。
	// 同じフレームに両方が触られたときは、今のデバイスを保つ
	//==================================================================================
	void InputDeviceManager::Update()
	{
		// ゲームパッドの接続状態を確認
		bool _isGamepadTouched = false;
		m_conectedDevices[EInputDevice::Gamepad] = UpdateGamepad(_isGamepadTouched);

		// キーボードマウス : キー・マウスのボタンを押したか、マウスを動かしたか
		const bool _isKeyboardMouseTouched = UpdateKeys() || IsMouseMoved();

		// 両方触られたら今のデバイスのまま
		if (_isGamepadTouched && _isKeyboardMouseTouched && (m_activeDevice != EInputDevice::None)) return;

		if (_isGamepadTouched)				m_activeDevice = EInputDevice::Gamepad;
		else if (_isKeyboardMouseTouched)	m_activeDevice = EInputDevice::KeyboardMouse;
	}

	bool InputDeviceManager::IsConected(EInputDevice a_inputDevice) const
	{
		auto _it = m_conectedDevices.find(a_inputDevice);
		if (_it != m_conectedDevices.end())
		{
			return _it->second;
		}

		return false;
	}

	// どれか1台でも繋がっていれば接続とする。触ったかどうかは全台を見る
	bool InputDeviceManager::UpdateGamepad(bool& a_outIsTouched)
	{
		a_outIsTouched = false;

		bool _isGamepadConnected = false;
		for (DWORD _userIndex = 0; _userIndex < XUSER_MAX_COUNT; ++_userIndex)
		{
			XINPUT_STATE _state = {};
			const bool _isConected = (XInputGetState(_userIndex, &_state) == ERROR_SUCCESS);

			if (_isConected)
			{
				_isGamepadConnected = true;

				// 繋いだフレームは基準が無いので触ったことにしない
				// (スティックが遊びの外で止まっている個体でいきなり切り替わらないように)
				if (m_prevGamepadConected[_userIndex])
				{
					a_outIsTouched = a_outIsTouched || IsGamepadTouched(_state.Gamepad, m_prevGamepadStates[_userIndex]);
				}
			}

			m_prevGamepadConected[_userIndex] = _isConected;
			m_prevGamepadStates[_userIndex] = _state.Gamepad;
		}
		return _isGamepadConnected;
	}

	// 仮想キーを一通り見て、押された瞬間のものがあるかを調べる
	// マウスのボタンも仮想キーに含まれるので、ここで一緒に見る
	bool InputDeviceManager::UpdateKeys()
	{
		bool _isTouched = false;

		// 0 と 0xFF は仮想キーとして使われていない
		for (int _vKey = 1; _vKey < 0xFF; ++_vKey)
		{
			const bool _isDown = (GetAsyncKeyState(_vKey) & 0x8000) != 0;
			if (_isDown && !m_prevKeyDown[_vKey]) _isTouched = true;
			m_prevKeyDown[_vKey] = _isDown;
		}
		return _isTouched;
	}

	// 読み出すと0に戻るので、ここでは覗くだけにする
	bool InputDeviceManager::IsMouseMoved() const
	{
		const auto* _pWind = MainEngine::Instance().GetNativeWindow();
		if (!_pWind) return false;

		if (_pWind->GetRawMouseWheelDelta() != 0) return true;

		const int _dx = _pWind->GetRawMouseDeltaX();
		const int _dy = _pWind->GetRawMouseDeltaY();
		return (std::abs(_dx) + std::abs(_dy)) > MOUSE_MOVE_THRESHOLD;
	}
}
