#include "InputAxisForXInput.h"

#include "../../../Internal/InputContext.h"
#include "../../../InputDeviceManager.h"

namespace Engine::Input
{
	namespace
	{
		// スティックの最大値
		constexpr float THUMB_MAX = 32767.0f;

		// 遊びの内側は 0、外側は遊びの縁を 0 として 1 までに広げ直す
		// (縁でいきなり遊びの分の値が出ないように)
		Math::Vector2 ApplyDeadzone(SHORT a_x, SHORT a_y, SHORT a_deadzone)
		{
			const float _x = static_cast<float>(a_x);
			const float _y = static_cast<float>(a_y);
			const float _deadzone = static_cast<float>(a_deadzone);

			const float _length = std::sqrt(_x * _x + _y * _y);
			if (_length <= _deadzone) return Math::Vector2::Zero();

			const float _rate = (std::min(_length, THUMB_MAX) - _deadzone) / (THUMB_MAX - _deadzone);
			return Math::Vector2(_x / _length * _rate, _y / _length * _rate);
		}
	}

	void InputAxisForXInput::Update(InputContext& a_inputContext)
	{
		// この番号のコントローラーが繋がっていなければ入力なし
		// (倒したまま抜かれたときに値が残らないように)
		const XINPUT_GAMEPAD* _pPad = a_inputContext.pDeviceManager
			? a_inputContext.pDeviceManager->GetGamepadState(m_userIndex)
			: nullptr;
		if (!_pPad)
		{
			NoInput();
			return;
		}

		if(m_isLeft)
		{
			m_axis = ApplyDeadzone(_pPad->sThumbLX, _pPad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
		}
		else
		{
			m_axis = ApplyDeadzone(_pPad->sThumbRX, _pPad->sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
		}
	}
}
