#include "InputCollector.h"

#include "../Internal/InputContext.h"
#include "../InputDeviceManager.h"

#include "../InputDevice/Button/InputButtonBase.h"
#include "../InputDevice/Axis/InputAxisBase.h"

namespace Engine::Input
{
	InputCollector::InputCollector(EInputDevice a_device)
		: m_device(a_device)
	{}
	InputCollector::~InputCollector()
	{}
	void InputCollector::Update(InputContext& a_inputContext)
	{
		// 更新前の準備
		{
			for (auto& _button : m_upButtonMap)
			{
				_button.second->PreUpdate();
			}
			for (auto& _axis : m_upAxisMap)
			{
				_axis.second->PreUpdate();
			}
		}

		// デバイスが繋がっているか
		// 繋がっていないデバイスを読みに行くと失敗するので、無効と同じく入力なしにする
		// (確かめ先が無いときは繋がっているものとして扱う)
		const bool _isConected = !a_inputContext.pDeviceManager
			|| a_inputContext.pDeviceManager->IsConected(m_device);

		// 有効 or 監視 : ボタンの状態を更新
		if ((GetActiveState() != EActiveState::Disable) && _isConected)
		{
			for (auto& _button : m_upButtonMap)
			{
				_button.second->Update(a_inputContext);
			}
			for(auto& _axis : m_upAxisMap)
			{
				_axis.second->SetDeltaTime(a_inputContext.deltaTime);
				_axis.second->Update(a_inputContext);
			}
		}
		// 無効・未接続 : すべて入力されていない状態に更新
		else
		{
			for (auto& _button : m_upButtonMap)
			{
				_button.second->NoInput();
			}
			for (auto& _axis : m_upAxisMap)
			{
				_axis.second->NoInput();
			}
		}
	}

	// 溜まっている入力状態を捨てる
	void InputCollector::ResetInput()
	{
		for (auto& _button : m_upButtonMap)
		{
			if (_button.second) _button.second->NoInput();
		}
		for (auto& _axis : m_upAxisMap)
		{
			// 軸は覚えている座標まで捨てさせたいので NoInput ではなくこちらを呼ぶ
			if (_axis.second) _axis.second->ResetInput();
		}
	}

	// 何かしらの入力検知したときにtureを返す
	bool InputCollector::IsSomethigInput()
	{
		for (auto& _button : m_upButtonMap)
		{
			if (!_button.second) continue;

			// 入力を受けていたらTrue
			if (_button.second->GetState() != InputButtonBase::EState::Free) return true;
		}

		for (auto& _axis : m_upAxisMap)
		{
			if (!_axis.second) continue;

			// 入力を受けていたらTrue
			if (_axis.second->GetState().LengthSquared() != 0.0f) return true;
		}

		// なんの入力も検知で着なかった
		return false;
	}

	// 任意のアプリケーションボタンの入力情報取得
	InputButtonBase::EState InputCollector::GetButtonState(ActionKey a_action) const
	{
		const InputButtonBase* _pButton = GetButton(a_action);

		if (!_pButton)
		{
			return InputButtonBase::EState::Free;
		}

		return _pButton->GetState();
	}

	// 任意の軸の入力情報取得
	Math::Vector2 InputCollector::GetAxisState(ActionKey a_action) const
	{
		const InputAxisBase* _pAxis = GetAxis(a_action);

		if (!_pAxis)
		{
			return Math::Vector2::Zero();
		}

		return _pAxis->GetState();
	}

	// アプリケーションボタン・入力軸の追加(同じアクションがあれば置き換える)
	void InputCollector::AddButton(ActionKey a_action, std::unique_ptr<InputButtonBase> a_upButton)
	{
		m_upButtonMap[a_action.id] = std::move(a_upButton);
	}
	void InputCollector::AddAxis(ActionKey a_action, std::unique_ptr<InputAxisBase> a_upAxis)
	{
		m_upAxisMap[a_action.id] = std::move(a_upAxis);
	}

	// 取得
	const InputButtonBase* InputCollector::GetButton(ActionKey a_action) const
	{
		auto _buttonIt = m_upButtonMap.find(a_action.id);
		if (_buttonIt == m_upButtonMap.end())
		{
			return nullptr;
		}
		return _buttonIt->second.get();
	}
	const InputAxisBase* InputCollector::GetAxis(ActionKey a_action) const
	{
		auto _axisIt = m_upAxisMap.find(a_action.id);

		if (_axisIt == m_upAxisMap.end())
		{
			return nullptr;
		}
		return _axisIt->second.get();
	}
	void InputCollector::Release()
	{
		m_upButtonMap.clear();
		m_upAxisMap.clear();
	}
}