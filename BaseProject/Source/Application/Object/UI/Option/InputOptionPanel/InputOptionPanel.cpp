#include "InputOptionPanel.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Input/InputDeviceManager.h"		// 最後に触ったデバイスと、コントローラーのボタンの待ち受け
#include "Engine/EditorField/EditorField.h"

#include "Application/ECS/World/APPWorld.h"
#include "Application/InstanceResource/UserDataResource.h"
#include "Application/InstanceResource/UINavigationResource.h"	// 待ち受けの間は項目移動を止める
#include "Application/Game/UserData/UserData.h"
#include "Application/Game/InputActions/InputManager/InputActionManager.h"
#include "Application/Game/DebugKeys/DebugKeys.h"

#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	namespace
	{
		// 軸の割り当てのうち、その向きのキーの置き場所(向きが無ければ nullptr)
		int* RefAxisKey(Game::AxisInputData& a_axis, EAxisDirection a_direction)
		{
			switch (a_direction)
			{
			case EAxisDirection::Up:    return &a_axis.up;
			case EAxisDirection::Right: return &a_axis.right;
			case EAxisDirection::Down:  return &a_axis.down;
			case EAxisDirection::Left:  return &a_axis.left;
			default:                    return nullptr;
			}
		}

		// 行が今指しているキー(無ければ -1)
		int FindBindingKey(const Game::InputBindingData& a_binding, EAxisDirection a_direction)
		{
			if (a_direction == EAxisDirection::None)
			{
				const auto* _pButton = std::get_if<Game::ButtonInputData>(&a_binding);
				return _pButton ? _pButton->key : -1;
			}

			const auto* _pAxis = std::get_if<Game::AxisInputData>(&a_binding);
			if (!_pAxis) return -1;

			Game::AxisInputData _axis = *_pAxis;
			const int* _pKey = RefAxisKey(_axis, a_direction);
			return _pKey ? *_pKey : -1;
		}

		// コントローラーの行が今指しているもの(無ければ -1)。軸はスティック1本なので上の値で表す
		int FindPadBindingKey(const Game::InputBindingData& a_binding, EAxisDirection a_direction)
		{
			if (a_direction == EAxisDirection::None)
			{
				const auto* _pButton = std::get_if<Game::ButtonInputData>(&a_binding);
				return _pButton ? _pButton->key : -1;
			}

			const auto* _pAxis = std::get_if<Game::AxisInputData>(&a_binding);
			return _pAxis ? _pAxis->up : -1;
		}

		// スティック1本ぶんの軸の割り当て
		Game::AxisInputData MakeStickAxis(bool a_isLeft)
		{
			namespace Pad = Engine::Input::GamepadKey;
			return a_isLeft
				? Game::AxisInputData{ Pad::LThumbUp, Pad::LThumbRight, Pad::LThumbDown, Pad::LThumbLeft }
				: Game::AxisInputData{ Pad::RThumbUp, Pad::RThumbRight, Pad::RThumbDown, Pad::RThumbLeft };
		}

		// コントローラーの番号 : 割り当てを流す InputActionManager と同じ1台目
		constexpr UINT GAMEPAD_USER_INDEX = 0;

		// スティックを倒したとみなす量(XInput の値)。遊びより大きく、はっきり倒したときだけ拾う
		constexpr float STICK_CAPTURE_THRESHOLD = 32767.0f * 0.6f;

		// ボタンの印(XInput)と GamepadKey の対応
		struct PadButtonMap
		{
			WORD mask;
			int padKey;
		};

		constexpr PadButtonMap PAD_BUTTON_MAP_ARRAY[] =
		{
			{ XINPUT_GAMEPAD_A,					Engine::Input::GamepadKey::A },
			{ XINPUT_GAMEPAD_B,					Engine::Input::GamepadKey::B },
			{ XINPUT_GAMEPAD_X,					Engine::Input::GamepadKey::X },
			{ XINPUT_GAMEPAD_Y,					Engine::Input::GamepadKey::Y },
			{ XINPUT_GAMEPAD_RIGHT_SHOULDER,	Engine::Input::GamepadKey::RShoulder },
			{ XINPUT_GAMEPAD_LEFT_SHOULDER,		Engine::Input::GamepadKey::LShoulder },
			{ XINPUT_GAMEPAD_DPAD_UP,			Engine::Input::GamepadKey::DPadUp },
			{ XINPUT_GAMEPAD_DPAD_DOWN,			Engine::Input::GamepadKey::DPadDown },
			{ XINPUT_GAMEPAD_DPAD_LEFT,			Engine::Input::GamepadKey::DPadLeft },
			{ XINPUT_GAMEPAD_DPAD_RIGHT,		Engine::Input::GamepadKey::DPadRight },
			{ XINPUT_GAMEPAD_START,				Engine::Input::GamepadKey::Start },
			{ XINPUT_GAMEPAD_BACK,				Engine::Input::GamepadKey::Back },
			{ XINPUT_GAMEPAD_LEFT_THUMB,		Engine::Input::GamepadKey::LThumbPress },
			{ XINPUT_GAMEPAD_RIGHT_THUMB,		Engine::Input::GamepadKey::RThumbPress },
		};

		// スティックをはっきり倒しているか
		bool IsStickTilted(SHORT a_x, SHORT a_y)
		{
			const float _x = static_cast<float>(a_x);
			const float _y = static_cast<float>(a_y);
			return (_x * _x + _y * _y) > (STICK_CAPTURE_THRESHOLD * STICK_CAPTURE_THRESHOLD);
		}
	}

	InputOptionPanel::InputOptionPanel()
	{
		// 移動は軸(4方向のキー)なので、向きごとに1行ずつ。並びは W・A・S・D
		for (EAxisDirection _direction : { EAxisDirection::Up, EAxisDirection::Left, EAxisDirection::Down, EAxisDirection::Right })
		{
			ButtonElement _element = {};
			_element.action = Game::EGameAction::Move;
			_element.axisDirection = _direction;
			m_buttonVec.push_back(_element);
		}

		// 残りはボタン1つで持つゲーム側のアクションを全部並べる。
		// 視点(Look)の軸はマウスで動かすものなので、ここには出さない
		for (uint32_t _i = 0; _i < static_cast<uint32_t>(Game::EGameAction::GameActionMax); ++_i)
		{
			const auto _action = static_cast<Game::EGameAction>(_i);
			if (_action == Game::EGameAction::Look || _action == Game::EGameAction::Move) continue;

			ButtonElement _element = {};
			_element.action = _action;
			m_buttonVec.push_back(_element);
		}
	}

	//======================================================================================
	// 開かれたとき
	//--------------------------------------------------------------------------------------
	// 写しは開くたびに UserData から取り直す。閉じている間に別の場所
	// (エディターの InputActionManager の編集など)で変わっていても、開けば合う
	//======================================================================================
	void InputOptionPanel::OnOpen(Engine::GameObject::ObjectContext& a_context)
	{
		// 待ち受けの途中で閉じられていても、開き直したら仕切り直す
		m_waitingIndex = -1;
		m_isRowClickArmed = true;

		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;
		const Game::InputSettings& _settings = _pUserDataResource->pUserData->GetInputSettings();

		m_moveSensitivity = _settings.mouseSensitivity;
		m_opIsInvertLookY = _settings.opIsInvertLookY;
		m_stickSensitivity = _settings.stickSensitivity;
		m_isInvertStickY = _settings.isInvertStickY;

		for (ButtonElement& _element : m_buttonVec)
		{
			_element.currentActionKey = -1;
			_element.currentPadKey = -1;

			const auto _it = _settings.keyboard.find(_element.action);
			if (_it != _settings.keyboard.end())
			{
				_element.currentActionKey = FindBindingKey(_it->second, _element.axisDirection);
			}

			const auto _padIt = _settings.controller.find(_element.action);
			if (_padIt != _settings.controller.end())
			{
				_element.currentPadKey = FindPadBindingKey(_padIt->second, _element.axisDirection);
			}
		}
	}

	//======================================================================================
	// 並べる
	//======================================================================================
	void InputOptionPanel::LayoutItems(Engine::GameObject::ObjectContext& a_context)
	{
		// どちらの設定を出すかで並ぶ行が変わるので、並べる前に決める
		UpdateGamepadMode(a_context);

		LayoutAxisSettings(a_context);
		AdvanceSpace(m_sectionSpace);
		LayoutButtonSettings(a_context);
	}

	void InputOptionPanel::LayoutAxisSettings(Engine::GameObject::ObjectContext& a_context)
	{
		PlaceHeaderRow(a_context, m_axisHeader);

		// 感度 : 数値の行(ボタンかスクロールバーかはエディターで選ぶ)
		PlaceNumeric(a_context, m_sensitivityNumeric);
		AdvanceRow();

		// 上下反転
		PlaceSelector(a_context, m_invertLookYSelector);
		AdvanceRow();
	}

	void InputOptionPanel::LayoutButtonSettings(Engine::GameObject::ObjectContext& a_context)
	{
		PlaceHeaderRow(a_context, m_buttonHeader);

		for (const ButtonElement& _element : m_buttonVec)
		{
			// 出さない行は隠して詰める(移動の行はコントローラーではスティック1本ぶんの1行)
			const bool _isShown = IsRowShown(_element);
			if (_element.axisDirection != EAxisDirection::None)
			{
				if (UIBase* _pRow = FindUI(a_context, _element.rowUI.guid)) _pRow->SetVisible(_isShown);
			}
			if (!_isShown) continue;

			PlaceItem(a_context, _element.rowUI);
			AdvanceRow();
		}
	}

	//======================================================================================
	// 操作
	//======================================================================================
	void InputOptionPanel::UpdateItems(Engine::GameObject::ObjectContext& a_context)
	{
		AxisSettings(a_context);
		ButtonSettings(a_context);
	}

	void InputOptionPanel::AxisSettings(Engine::GameObject::ObjectContext& a_context)
	{
		// 感度の行 : 今見せているデバイスの感度を動かす。
		// ゲージの数値(Text の飾り)にもそのまま感度が出る(小数の桁はゲージの Decimals)
		float _sensitivity = m_isGamepadMode ? m_stickSensitivity : m_moveSensitivity;
		const bool _isSensitivityChanged = m_sensitivityNumeric.Update(
			a_context, _sensitivity, SENSITIVITY_MIN, SENSITIVITY_MAX, SENSITIVITY_STEP);

		//------------------------------------------------------------------
		// コントローラーの表示 : スティック(視点)の感度と上下反転
		//------------------------------------------------------------------
		if (m_isGamepadMode)
		{
			if (_isSensitivityChanged) ApplyStickSensitivity(a_context, _sensitivity);

			if (UpdateSelector(a_context, m_invertLookYSelector) != 0)
			{
				ApplyInvertStickY(a_context, !m_isInvertStickY);
			}
			SetSelectorText(a_context, m_invertLookYSelector, m_isInvertStickY ? "On" : "Off");
			return;
		}

		//------------------------------------------------------------------
		// キーボード・マウスの表示 : マウスの感度と上下反転
		//------------------------------------------------------------------
		if (_isSensitivityChanged) ApplySensitivity(a_context, _sensitivity);

		// 上下反転 : 選んだ時点で「選ばれている」扱いになり、作り手の値より優先される
		if (UpdateSelector(a_context, m_invertLookYSelector) != 0)
		{
			ApplyInvertLookY(a_context, !IsCurrentInvertLookY(a_context));
		}
		SetSelectorText(a_context, m_invertLookYSelector, IsCurrentInvertLookY(a_context) ? "On" : "Off");
	}

	void InputOptionPanel::ButtonSettings(Engine::GameObject::ObjectContext& a_context)
	{
		// エディターの編集で行が減っていたら、待ち受けをやめる
		if (m_waitingIndex >= static_cast<int>(m_buttonVec.size()))
		{
			m_waitingIndex = -1;
		}

		// キーは OS から直に拾うので、ゲームの入力を受け付けない間(エディター操作中・
		// 文字入力中・フォーカスが無いとき)は見ない。見るとエディターで打った文字が割り当てられる
		const bool _isInputEnable =
			a_context.pServices && a_context.pServices->pInputManager &&
			a_context.pServices->pInputManager->IsGameInputEnable();

		if (m_waitingIndex < 0)
		{
			//--------------------------------------------------------------
			// 待っていない : 押された行を待ち受けに入れる
			//
			// 割り当てた直後は、次に押し始めるまで行のクリックを見ない
			// (割り当てに使ったクリックを離した瞬間が、行のクリックとして届くため)
			//--------------------------------------------------------------
			if (_isInputEnable && a_context.pServices->pInputManager->IsPress(Game::EGameAction::Select))
			{
				m_isRowClickArmed = true;
			}

			for (size_t _i = 0; _i < m_buttonVec.size() && m_isRowClickArmed; ++_i)
			{
				if (!IsRowShown(m_buttonVec[_i])) continue;

				UIBase* _pRow = FindUI(a_context, m_buttonVec[_i].rowUI.guid);
				if (!_pRow || !_pRow->IsClicked()) continue;

				m_waitingIndex = static_cast<int>(_i);

				// 今押されているキーは割り当てない。押された瞬間だけを拾うので、
				// 行を押したクリックも、押された扱いのまま戻らないキーも、ここで覚えておけば拾われない
				CaptureKeyState();
				CapturePadState(a_context);
				break;
			}
		}
		else if (m_isGamepadMode)
		{
			//--------------------------------------------------------------
			// 待ち受け中(コントローラー) : 待ち受けに入った後に新しく押されたボタンを割り当てる
			//
			// 移動の行はスティックを倒して選ぶ(ボタンは拾わない)。ほかの行はボタンだけを拾う。
			// スティック・十字キー・LB/RB で項目が動かないよう、待っている間は項目移動を止める
			//--------------------------------------------------------------
			LockNavigation(a_context);

			if (_isInputEnable)
			{
				ButtonElement& _element = m_buttonVec[static_cast<size_t>(m_waitingIndex)];
				const bool _isStickWanted = (_element.axisDirection != EAxisDirection::None);

				const int _padKey = FindNewlyPressedPadKey(a_context, _isStickWanted);
				if (_padKey >= 0)
				{
					ApplyPadBinding(a_context, _element, _padKey);
					m_waitingIndex = -1;
					m_isRowClickArmed = false;
				}
			}
		}
		else if (_isInputEnable)
		{
			//--------------------------------------------------------------
			// 待ち受け中 : 待ち受けに入った後に新しく押されたキーを割り当てる
			//
			// 「何も押されていなくなるまで待つ」形にすると、日本語キーボードの IME のキー
			// (英数・ひらがな・半角/全角)が押された扱いのまま戻らず、いつまでも受け付けない。
			// 押された瞬間だけを見れば、押しっぱなし・戻らないキーは関係なくなる
			//
			// TODO : 待ち受け中もゲーム側のアクションは動く(Esc でメニューが開くなど)。
			//        取り消しの操作と、同じキーが別のアクションに付いているときの扱いを決める
			//--------------------------------------------------------------
			const int _key = FindNewlyPressedKey();
			if (_key >= 0)
			{
				ApplyButtonBinding(a_context, m_buttonVec[static_cast<size_t>(m_waitingIndex)], _key);
				m_waitingIndex = -1;
				m_isRowClickArmed = false;
			}
		}

		// 表示 : 各行に今のキー名を書く
		for (size_t _i = 0; _i < m_buttonVec.size(); ++_i)
		{
			RefreshKeyText(a_context, m_buttonVec[_i], static_cast<int>(_i) == m_waitingIndex);
		}
	}

	//======================================================================================
	// UserData へ書く
	//======================================================================================
	void InputOptionPanel::ApplySensitivity(Engine::GameObject::ObjectContext& a_context, float a_sensitivity)
	{
		// 0.01 単位に丸める : 足し引きを繰り返したときの誤差を残さない
		const float _rounded = std::round(a_sensitivity / SENSITIVITY_STEP) * SENSITIVITY_STEP;
		m_moveSensitivity = std::clamp(_rounded, SENSITIVITY_MIN, SENSITIVITY_MAX);

		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		_pUserDataResource->pUserData->RefInputSettings().mouseSensitivity = m_moveSensitivity;
		ApplyLookSettings(a_context);
	}

	void InputOptionPanel::ApplyInvertLookY(Engine::GameObject::ObjectContext& a_context, bool a_isInvert)
	{
		m_opIsInvertLookY = a_isInvert;

		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		_pUserDataResource->pUserData->RefInputSettings().opIsInvertLookY = m_opIsInvertLookY;
		ApplyLookSettings(a_context);
	}

	void InputOptionPanel::ApplyLookSettings(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;
		if (!a_context.pServices || !a_context.pServices->pMainEngine) return;

		_pUserDataResource->pUserData->ApplyLookSettings(*a_context.pServices->pMainEngine);
	}

	bool InputOptionPanel::IsCurrentInvertLookY(Engine::GameObject::ObjectContext& a_context) const
	{
		if (m_opIsInvertLookY) return m_opIsInvertLookY.value();

		// 選ばれていなければ、入力が今使っている値(作り手の値)
		if (a_context.pServices && a_context.pServices->pInputManager)
		{
			return a_context.pServices->pInputManager->IsInvertLookY();
		}
		return false;
	}

	void InputOptionPanel::ApplyButtonBinding(Engine::GameObject::ObjectContext& a_context, ButtonElement& a_element, int a_key)
	{
		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		auto& _keyboard = _pUserDataResource->pUserData->RefInputSettings().keyboard;
		const auto _it = _keyboard.find(a_element.action);

		if (a_element.axisDirection == EAxisDirection::None)
		{
			// ボタン1つの行 : 軸(4方向のキー)で持っているアクションは、キー1つでは置き換えられない
			if (_it != _keyboard.end() && std::holds_alternative<Game::AxisInputData>(_it->second))
			{
				const std::string _actionName{ magic_enum::enum_name(a_element.action) };
				ENGINE_WARNING("[InputOptionPanel] %s は軸で割り当てられているため、キー1つでは変えられません", _actionName.c_str());
				return;
			}

			_keyboard[a_element.action] = Game::ButtonInputData{ a_key };
		}
		else
		{
			// 軸の向きの行 : その向きのキーだけを差し替え、残りの3方向は今のまま
			Game::AxisInputData _axis = {};
			if (_it != _keyboard.end())
			{
				const auto* _pAxis = std::get_if<Game::AxisInputData>(&_it->second);
				if (!_pAxis)
				{
					const std::string _actionName{ magic_enum::enum_name(a_element.action) };
					ENGINE_WARNING("[InputOptionPanel] %s はボタン1つで割り当てられているため、向きごとには変えられません", _actionName.c_str());
					return;
				}
				_axis = *_pAxis;
			}
			else
			{
				// まだ割り当てが無ければ既定から始める(他の3方向が空にならないように)
				const Game::InputSettings _default = Game::MakeDefaultInputSettings();
				const auto _defaultIt = _default.keyboard.find(a_element.action);
				if (_defaultIt != _default.keyboard.end())
				{
					if (const auto* _pDefaultAxis = std::get_if<Game::AxisInputData>(&_defaultIt->second)) _axis = *_pDefaultAxis;
				}
			}

			int* _pKey = RefAxisKey(_axis, a_element.axisDirection);
			if (!_pKey) return;
			*_pKey = a_key;

			_keyboard[a_element.action] = _axis;
		}

		a_element.currentActionKey = a_key;
		ApplyBindings(a_context);
	}

	// 入力デバイスへ流し直す。通さないと UserData の値だけ変わって、操作は前のキーのまま
	void InputOptionPanel::ApplyBindings(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pInputActionManager) return;

		_pUserDataResource->pInputActionManager->Apply();
	}

	//======================================================================================
	// コントローラーの表示
	//--------------------------------------------------------------------------------------
	// 見るのは InputDeviceManager の最後に触ったデバイス(UI のコントローラー用の飾りと同じ)。
	// 待ち受けの間は変えない : 入ったデバイスのボタンを待っているのに、表示だけ替わらないように
	//======================================================================================
	void InputOptionPanel::UpdateGamepadMode(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_waitingIndex >= 0) return;
		if (!a_context.pServices || !a_context.pServices->pInputManager) return;

		using Engine::Input::EInputDevice;
		const EInputDevice _device = a_context.pServices->pInputManager->GetInputDeviceManager().GetActiveDevice();
		m_isGamepadMode = (_device == EInputDevice::Gamepad);
	}

	bool InputOptionPanel::IsRowShown(const ButtonElement& a_element) const
	{
		if (!m_isGamepadMode) return true;

		// スティックは向きごとには変えられないので、軸の行は上(前)の1行で1本ぶんを持つ
		return a_element.axisDirection == EAxisDirection::None
			|| a_element.axisDirection == EAxisDirection::Up;
	}

	void InputOptionPanel::ApplyStickSensitivity(Engine::GameObject::ObjectContext& a_context, float a_sensitivity)
	{
		// マウスと同じく 0.01 単位に丸める
		const float _rounded = std::round(a_sensitivity / SENSITIVITY_STEP) * SENSITIVITY_STEP;
		m_stickSensitivity = std::clamp(_rounded, SENSITIVITY_MIN, SENSITIVITY_MAX);

		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		// スティックの速さは割り当てと一緒に軸へ積むので、割り当てごと流し直す
		_pUserDataResource->pUserData->RefInputSettings().stickSensitivity = m_stickSensitivity;
		ApplyBindings(a_context);
	}

	void InputOptionPanel::ApplyInvertStickY(Engine::GameObject::ObjectContext& a_context, bool a_isInvert)
	{
		m_isInvertStickY = a_isInvert;

		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		_pUserDataResource->pUserData->RefInputSettings().isInvertStickY = m_isInvertStickY;
		ApplyBindings(a_context);
	}

	//======================================================================================
	// コントローラーの割り当てを書く
	//--------------------------------------------------------------------------------------
	// 移動の行   : スティック1本ごと入れる。視点が同じスティックを使っていれば、視点はもう片方へ
	//              (同じスティックで移動と視点が両方動くと遊べないため)。
	// ボタンの行 : ボタン1つ。軸で持っているアクションはボタン1つでは置き換えない(キーボードと同じ)
	//======================================================================================
	void InputOptionPanel::ApplyPadBinding(Engine::GameObject::ObjectContext& a_context, ButtonElement& a_element, int a_padKey)
	{
		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		auto& _controller = _pUserDataResource->pUserData->RefInputSettings().controller;
		const auto _it = _controller.find(a_element.action);

		if (a_element.axisDirection == EAxisDirection::None)
		{
			if (_it != _controller.end() && std::holds_alternative<Game::AxisInputData>(_it->second))
			{
				const std::string _actionName{ magic_enum::enum_name(a_element.action) };
				ENGINE_WARNING("[InputOptionPanel] %s はスティックで割り当てられているため、ボタン1つでは変えられません", _actionName.c_str());
				return;
			}

			_controller[a_element.action] = Game::ButtonInputData{ a_padKey };
			a_element.currentPadKey = a_padKey;
		}
		else
		{
			if (_it != _controller.end() && !std::holds_alternative<Game::AxisInputData>(_it->second))
			{
				const std::string _actionName{ magic_enum::enum_name(a_element.action) };
				ENGINE_WARNING("[InputOptionPanel] %s はボタンで割り当てられているため、スティックでは変えられません", _actionName.c_str());
				return;
			}

			const bool _isLeft = (a_padKey == Engine::Input::GamepadKey::LThumbUp);
			const Game::AxisInputData _stick = MakeStickAxis(_isLeft);
			_controller[a_element.action] = _stick;

			// 視点が同じスティックなら、もう片方へ入れ替える
			const auto _lookIt = _controller.find(Game::EGameAction::Look);
			const auto* _pLook = (_lookIt != _controller.end()) ? std::get_if<Game::AxisInputData>(&_lookIt->second) : nullptr;
			if (_pLook && _pLook->up == _stick.up)
			{
				_controller[Game::EGameAction::Look] = MakeStickAxis(!_isLeft);
			}

			// 同じアクションの軸の行(向きごとの行)は全部同じスティックを指す
			for (ButtonElement& _element : m_buttonVec)
			{
				if (_element.action == a_element.action && _element.axisDirection != EAxisDirection::None)
				{
					_element.currentPadKey = _stick.up;
				}
			}
		}

		ApplyBindings(a_context);
	}

	void InputOptionPanel::CapturePadState(Engine::GameObject::ObjectContext& a_context)
	{
		m_padDownKeyVec = ReadPadKeys(a_context);
	}

	int InputOptionPanel::FindNewlyPressedPadKey(Engine::GameObject::ObjectContext& a_context, bool a_isStickWanted)
	{
		const std::vector<int> _downKeyVec = ReadPadKeys(a_context);

		int _found = -1;
		for (int _padKey : _downKeyVec)
		{
			if (IsStickKey(_padKey) != a_isStickWanted) continue;

			// 前のフレームから押されていたもの(押しっぱなし・倒しっぱなし)は拾わない
			if (std::find(m_padDownKeyVec.begin(), m_padDownKeyVec.end(), _padKey) != m_padDownKeyVec.end()) continue;

			_found = _padKey;
			break;
		}

		// 全部覚え直す : 途中で抜けると、残りが古いまま次のフレームに拾われる
		m_padDownKeyVec = _downKeyVec;
		return _found;
	}

	void InputOptionPanel::LockNavigation(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld || !a_context.pWorld->HasResource<InstanceResource::UINavigationResource>()) return;
		a_context.pWorld->RefResource<InstanceResource::UINavigationResource>().LockMove(a_context.frameIndex);
	}

	//======================================================================================
	// 今押されているボタン・倒されているスティック
	//--------------------------------------------------------------------------------------
	// スティックは倒した側の LThumbUp / RThumbUp で表す(割り当てはスティック1本ごとのため)。
	// トリガーはしきい値を超えていれば押したとする(InputButtonForXInput と同じ)
	//======================================================================================
	std::vector<int> InputOptionPanel::ReadPadKeys(Engine::GameObject::ObjectContext& a_context)
	{
		std::vector<int> _keyVec = {};
		if (!a_context.pServices || !a_context.pServices->pInputManager) return _keyVec;

		const XINPUT_GAMEPAD* _pPad =
			a_context.pServices->pInputManager->GetInputDeviceManager().GetGamepadState(GAMEPAD_USER_INDEX);
		if (!_pPad) return _keyVec;

		for (const PadButtonMap& _map : PAD_BUTTON_MAP_ARRAY)
		{
			if (_pPad->wButtons & _map.mask) _keyVec.push_back(_map.padKey);
		}

		namespace Pad = Engine::Input::GamepadKey;
		if (_pPad->bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)  _keyVec.push_back(Pad::LTrigger);
		if (_pPad->bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) _keyVec.push_back(Pad::RTrigger);

		if (IsStickTilted(_pPad->sThumbLX, _pPad->sThumbLY)) _keyVec.push_back(Pad::LThumbUp);
		if (IsStickTilted(_pPad->sThumbRX, _pPad->sThumbRY)) _keyVec.push_back(Pad::RThumbUp);

		return _keyVec;
	}

	bool InputOptionPanel::IsStickKey(int a_padKey)
	{
		namespace Pad = Engine::Input::GamepadKey;
		return a_padKey == Pad::LThumbUp || a_padKey == Pad::RThumbUp;
	}

	//======================================================================================
	// キー名の表示
	//--------------------------------------------------------------------------------------
	// 飾りの文字は保存される値だが、毎フレーム UserData の写しから書き直すので、
	// 保存されたまま残っても害はない
	//======================================================================================
	void InputOptionPanel::RefreshKeyText(Engine::GameObject::ObjectContext& a_context, const ButtonElement& a_element, bool a_isWaiting)
	{
		if (a_element.keyTextId == 0) return;

		UIBase* _pRow = FindUI(a_context, a_element.rowUI.guid);
		if (!_pRow) return;

		Decoration::Decoration* _pDecoration = _pRow->FindDecorationById(a_element.keyTextId);
		if (!_pDecoration) return;

		Decoration::TextData* _pText = _pDecoration->RefText();
		if (!_pText) return;

		std::string _name = "...";
		if (!a_isWaiting)
		{
			_name = m_isGamepadMode
				? std::string(Engine::Input::GamepadKey::GetName(a_element.currentPadKey))
				: MakeKeyName(a_element.currentActionKey);
		}
		if (_pText->text != _name) _pText->text = _name;
	}

	void InputOptionPanel::CaptureKeyState()
	{
		for (size_t _key = 0; _key < KEY_CODE_COUNT; ++_key)
		{
			m_keyDownBits[_key] = (GetAsyncKeyState(static_cast<int>(_key)) & 0x8000) != 0;
		}
	}

	int InputOptionPanel::FindNewlyPressedKey()
	{
		int _found = -1;

		// 全部のキーの状態を覚え直しながら、離れていたのに押されたものを探す。
		// 途中で抜けると、後ろのキーの状態が古いまま残って次のフレームに拾われてしまう
		for (int _key = 0x01; _key < static_cast<int>(KEY_CODE_COUNT); ++_key)
		{
			const bool _isDown = (GetAsyncKeyState(_key) & 0x8000) != 0;
			const bool _wasDown = m_keyDownBits[static_cast<size_t>(_key)];
			m_keyDownBits[static_cast<size_t>(_key)] = _isDown;

			if (_found >= 0) continue;
			if (!_isDown || _wasDown) continue;
			if (IsIgnoredKey(_key)) continue;

			_found = _key;
		}
		return _found;
	}

	bool InputOptionPanel::IsIgnoredKey(int a_key)
	{
		switch (a_key)
		{
		// 左右を区別しない修飾キー(左右つきの VK_LSHIFT などのほうで拾う)
		case VK_SHIFT:
		case VK_CONTROL:
		case VK_MENU:

		// IME の切り替え(かな・漢字・変換・無変換など)。操作に割り当てるものではない
		case VK_KANA:
		case VK_JUNJA:
		case VK_FINAL:
		case VK_KANJI:
		case VK_CONVERT:
		case VK_NONCONVERT:
		case VK_ACCEPT:
		case VK_MODECHANGE:
		case VK_PROCESSKEY:
		case VK_PACKET:

		// 日本語キーボードの 英数 / カタカナひらがな / 半角全角 など(0xF0〜0xF5)。
		// 押された扱いのまま戻らないことがある
		case 0xF0: case 0xF1: case 0xF2: case 0xF3: case 0xF4: case 0xF5:
			return true;

		default:
			// デバッグキー(Development までの F1〜F7)。割り当てると押すたびに両方が動く
			return Game::DebugKeys::IsReservedKey(a_key);
		}
	}

	std::string InputOptionPanel::MakeKeyName(int a_key)
	{
		if (a_key < 0) return "-";

		// マウスのボタンは OS から名前を引けない
		switch (a_key)
		{
		case VK_LBUTTON:  return "Mouse L";
		case VK_RBUTTON:  return "Mouse R";
		case VK_MBUTTON:  return "Mouse M";
		case VK_XBUTTON1: return "Mouse 4";
		case VK_XBUTTON2: return "Mouse 5";
		default: break;
		}

		// それ以外は OS に引かせる(キーボードの配列に合った名前が返る)
		UINT _scanCode = MapVirtualKeyW(static_cast<UINT>(a_key), MAPVK_VK_TO_VSC);

		// 矢印キーなどの拡張キーは、印を付けないとテンキー側の名前が返る
		switch (a_key)
		{
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
		case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
		case VK_INSERT: case VK_DELETE: case VK_RCONTROL: case VK_RMENU:
			_scanCode |= 0x100;
			break;
		default:
			break;
		}

		wchar_t _name[64] = {};
		if (GetKeyNameTextW(static_cast<LONG>(_scanCode << 16), _name, static_cast<int>(std::size(_name))) > 0)
		{
			return String::ToUTF8(_name);
		}
		return "Key " + std::to_string(a_key);
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void InputOptionPanel::ArchiveItems(Engine::Persistence::Archive& a_ar)
	{
		// キーは個別に持っていた頃と同じ(SensitivityGaugeGUID など)
		m_sensitivityNumeric.Archive(a_ar, "Sensitivity");
		a_ar.Field("SectionSpace", m_sectionSpace);

		size_t _count = m_buttonVec.size();
		if (a_ar.BeginArray("Buttons", _count))
		{
			m_buttonVec.resize(_count);
			for (size_t _i = 0; _i < _count; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;

				// 1行ごとに区切る : 行へフィールドを足しても、後ろの行の読み出しがずれない
				{
					Engine::Persistence::ArchiveSection _section(a_ar, "ButtonElement");

					ButtonElement& _element = m_buttonVec[_i];
					Game::ActionField(a_ar, "Action", _element.action);
					ArchiveItemUI(a_ar, "Row", _element.rowUI);
					a_ar.Field("KeyTextId", _element.keyTextId);

					// 後から足したもの : 追加は必ずこの区切りの末尾へ
					a_ar.Field("AxisDirection", _element.axisDirection);
				}

				a_ar.EndObject();
			}
			a_ar.EndArray();
		}

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		ArchiveItemUI(a_ar, "AxisHeader", m_axisHeader);
		ArchiveItemUI(a_ar, "ButtonHeader", m_buttonHeader);
		ArchiveSelector(a_ar, "InvertLookY", m_invertLookYSelector);
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void InputOptionPanel::DrawItemsInspector(Engine::GameObject::ObjectContext& a_context)
	{
		//------------------------------------------------------------------
		// 視点側
		//------------------------------------------------------------------
		if (Engine::EditorField::TreeScope _tree{ "Axis", true })
		{
			DrawItemUIInspector(a_context, "AxisHeader", m_axisHeader);
			Engine::EditorField::Tooltip("見出し(文字だけの UI)。未設定なら行を取らない");

			m_sensitivityNumeric.DrawInspector(a_context, "Sensitivity");
			DrawSelectorInspector(a_context, "InvertLookY", m_invertLookYSelector);
			Engine::EditorField::Value("Sensitivity", "%.2f", m_moveSensitivity);
		}

		Engine::EditorField::Field("SectionSpace", m_sectionSpace, 0.5f, 0.0f, 512.0f);
		Engine::EditorField::Tooltip("視点側とボタン側の間(px)");

		//------------------------------------------------------------------
		// ボタン側
		//------------------------------------------------------------------
		if (Engine::EditorField::TreeScope _tree{ "Buttons", true })
		{
			DrawItemUIInspector(a_context, "ButtonHeader", m_buttonHeader);
			Engine::EditorField::Tooltip("見出し(文字だけの UI)。未設定なら行を取らない");

			int _removeIndex = -1;

			for (size_t _i = 0; _i < m_buttonVec.size(); ++_i)
			{
				ButtonElement& _element = m_buttonVec[_i];
				Engine::EditorField::IDScope _id(static_cast<int>(_i));

				// 軸の行は向きも出す(移動が4行並ぶので)
				std::string _label{ magic_enum::enum_name(_element.action) };
				if (_element.axisDirection != EAxisDirection::None)
				{
					_label += " " + std::string(magic_enum::enum_name(_element.axisDirection));
				}
				Engine::EditorField::TreeScope _row{ _label.c_str() };
				if (!_row) continue;

				Engine::EditorField::Field("Action", _element.action);
				Engine::EditorField::Field("AxisDirection", _element.axisDirection);
				Engine::EditorField::Tooltip("移動のように軸(4方向のキー)で持つアクションは、どの向きのキーを変えるか。ボタン1つなら None");
				DrawItemUIInspector(a_context, "Row", _element.rowUI);

				// キー名を書き込む飾りは、行の UI の文字の飾りから選ぶ
				if (UIBase* _pRow = FindUI(a_context, _element.rowUI.guid))
				{
					_pRow->DrawDecorationPicker("KeyText", _element.keyTextId, Decoration::EDecorationType::Text);
					Engine::EditorField::Tooltip("今のキー名を書き込む文字の飾り");
				}

				Engine::EditorField::Value("Key", "%s", MakeKeyName(_element.currentActionKey).c_str());
				Engine::EditorField::Value("Pad", "%s", Engine::Input::GamepadKey::GetName(_element.currentPadKey));

				if (Engine::EditorField::DeleteSmallButton("Remove")) _removeIndex = static_cast<int>(_i);
			}

			if (_removeIndex >= 0)
			{
				m_buttonVec.erase(m_buttonVec.begin() + _removeIndex);
				m_waitingIndex = -1;
			}

			if (Engine::EditorField::CreateButton("Add"))
			{
				m_buttonVec.push_back(ButtonElement{});
			}
		}
	}
}
