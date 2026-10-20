#include "InputAxisForXInput.h"

namespace Engine::Input
{
	void InputAxisForXInput::PreUpdate()
	{
		m_prevConState = m_conState;
	}

	// 入力コンテキストは使用しない
	// 繋がっているかの確かめはコレクター(InputCollector)が済ませている
	void InputAxisForXInput::Update(InputContext&)
	{
		// 別の番号のコントローラーだけが繋がっているときや、
		// 確かめた直後に抜かれたときはここで失敗する。止めずに入力なしとして扱う
		DWORD _res = XInputGetState(m_userIndex, &m_conState);
		if (_res != ERROR_SUCCESS)
		{
			NoInput();
			return;
		}

		if(m_isLeft)
		{
			m_axis.x = m_conState.Gamepad.sThumbLX / 32767.f;
			m_axis.y = m_conState.Gamepad.sThumbLY / 32767.f;
		}
		else
		{
			m_axis.x = m_conState.Gamepad.sThumbRX / 32767.f;
			m_axis.y = m_conState.Gamepad.sThumbRY / 32767.f;
		}
	}
}
