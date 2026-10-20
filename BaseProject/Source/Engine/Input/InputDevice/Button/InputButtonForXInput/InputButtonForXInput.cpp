#include "InputButtonForXInput.h"

#include "../../../Core/GamepadKey.h"
#include "../../../Internal/InputContext.h"
#include "../../../InputDeviceManager.h"

namespace Engine::Input
{
	namespace
	{
		// GamepadKey は XInput の VK_PAD_* と同じ値で保存している
		static_assert(GamepadKey::A == VK_PAD_A);
		static_assert(GamepadKey::RTrigger == VK_PAD_RTRIGGER);
		static_assert(GamepadKey::DPadUp == VK_PAD_DPAD_UP);
		static_assert(GamepadKey::RThumbPress == VK_PAD_RTHUMB_PRESS);
		static_assert(GamepadKey::LThumbUp == VK_PAD_LTHUMB_UP);
		static_assert(GamepadKey::RThumbLeft == VK_PAD_RTHUMB_LEFT);

		// 番号のボタンが押されているか
		// スティックの向きはボタンとしては扱わない(軸の割り当てで使う)
		bool IsPadKeyDown(const XINPUT_GAMEPAD& a_pad, int a_padKey)
		{
			auto _isButton = [&](WORD a_mask) { return (a_pad.wButtons & a_mask) != 0; };

			switch (a_padKey)
			{
				case GamepadKey::A:				return _isButton(XINPUT_GAMEPAD_A);
				case GamepadKey::B:				return _isButton(XINPUT_GAMEPAD_B);
				case GamepadKey::X:				return _isButton(XINPUT_GAMEPAD_X);
				case GamepadKey::Y:				return _isButton(XINPUT_GAMEPAD_Y);
				case GamepadKey::RShoulder:		return _isButton(XINPUT_GAMEPAD_RIGHT_SHOULDER);
				case GamepadKey::LShoulder:		return _isButton(XINPUT_GAMEPAD_LEFT_SHOULDER);
				case GamepadKey::LTrigger:		return a_pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
				case GamepadKey::RTrigger:		return a_pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
				case GamepadKey::DPadUp:		return _isButton(XINPUT_GAMEPAD_DPAD_UP);
				case GamepadKey::DPadDown:		return _isButton(XINPUT_GAMEPAD_DPAD_DOWN);
				case GamepadKey::DPadLeft:		return _isButton(XINPUT_GAMEPAD_DPAD_LEFT);
				case GamepadKey::DPadRight:		return _isButton(XINPUT_GAMEPAD_DPAD_RIGHT);
				case GamepadKey::Start:			return _isButton(XINPUT_GAMEPAD_START);
				case GamepadKey::Back:			return _isButton(XINPUT_GAMEPAD_BACK);
				case GamepadKey::LThumbPress:	return _isButton(XINPUT_GAMEPAD_LEFT_THUMB);
				case GamepadKey::RThumbPress:	return _isButton(XINPUT_GAMEPAD_RIGHT_THUMB);
				default:						return false;
			}
		}
	}

	InputButtonForXInput::InputButtonForXInput(UINT a_userIndex, int a_padKey)
		: m_userIndex(a_userIndex)
	{
		m_padKeyVec.push_back(a_padKey);
	}
	InputButtonForXInput::InputButtonForXInput(UINT a_userIndex, const std::vector<int>& a_padKeyVec)
		: m_userIndex(a_userIndex)
		, m_padKeyVec(a_padKeyVec)
	{}

	void InputButtonForXInput::Update(InputContext& a_inputContext)
	{
		// 更新済みなら飛ばす
		if (!m_needUpdate) return;

		// この番号のコントローラーが繋がっていなければ離したことにする
		// (押したまま抜かれても Release を挟んで止まる)
		const XINPUT_GAMEPAD* _pPad = a_inputContext.pDeviceManager
			? a_inputContext.pDeviceManager->GetGamepadState(m_userIndex)
			: nullptr;

		// 登録されているボタンが押されているかどうか
		bool _isButtonDown = false;
		if (_pPad)
		{
			for (int _padKey : m_padKeyVec)
			{
				if (IsPadKeyDown(*_pPad, _padKey))
				{
					_isButtonDown = true;
				}
			}
		}

		// Press / Hold / Release の組み立ては基底と同じ
		UpdateState(_isButtonDown);

		// 更新済み
		m_needUpdate = false;
	}
	void InputButtonForXInput::GetCode(std::vector<int>&a_ret) const
	{
		for (int _code : m_padKeyVec)
		{
			a_ret.push_back(_code);
		}
	}
}
