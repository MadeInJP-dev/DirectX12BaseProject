#include "InputManager.h"

#include "../Internal/InputContext.h"

#include "../InputCollector/InputCollector.h"
#include "../InputDeviceManager.h"
#include "../InputDevice/Button/InputButtonBase.h"
#include "../InputDevice/Button/InputButtonForWindowsChord/InputButtonForWindowsChord.h"
#include "../InputDevice/Axis/InputAxisBase.h"

#include "../../MainEngine.h"
#include "../../Window/NativeWindow.h"
#include "InputManager.h"
#include "InputManager.h"

namespace Engine::Input
{
	namespace
	{
		// エディタのテキスト入力欄にフォーカスがあるか(=文字入力中か)。
		// この間はゲーム側の入力を無効化し、プレイヤー操作やシーン遷移が
		// 誤って走らないようにする。
		// エディタが無い(無効時など)ときは何もブロックしない
		bool IsUICapturingInput()
		{
			return EditorField::IsTextInputActive();
		}

		// ゲーム入力を受け付けてよい状態か。
		//
		// プレイモード以外(エディター操作中)は、キーもマウスもエディターのものなので
		// ゲーム側へは一切渡さない。ここで止めておけば、エディターで WASD を押しても
		// プレイヤーが動かないし、シーンビューでカーソルを振り回しても視点が回らない。
		//
		// 「入力が残らない」ためにも効く。取得側が常に無入力を返すので、
		// 入力フェーズのシステムが毎フレーム MoveIntent などへ 0 を書き込み、
		// 切り替え前の入力が押しっぱなしのまま固まらない。
		bool IsPlayMode()
		{
			// デバッグプレイはエディターを出したまま遊ぶモードなので、入力はゲーム側へ渡す。
			// (画の出し方は Editor 寄り、操作は Game 寄り)
			const auto _mode = MainEngine::Instance().GetMode();
			return (_mode == EAppMode::Game) || (_mode == EAppMode::DebugPlay);
		}

		// 溜まっている生のマウス移動量を捨てる
		//
		// 生の入力は読み出すまで足し込まれ続ける。視点操作をしていない間
		// (エディター操作中など)に捨てておかないと、操作を再開した最初の
		// フレームでその間の移動が一気に効いて視点が飛ぶ。
		void DiscardRawMouseDelta()
		{
			auto* _pWind = MainEngine::Instance().RefNativeWindow();
			if (!_pWind) return;

			int _rawX = 0;
			int _rawY = 0;
			_pWind->ConsumeRawMouseDelta(_rawX, _rawY);
		}

		// 溜まっているホイールの回転量を捨てる
		//
		// 移動量と同じく読み出すまで足し込まれ続けるので、
		// 切り替えの前に回したぶんを向こう側へ持ち越さないようにする
		void DiscardWheelDelta()
		{
			auto* _pWind = MainEngine::Instance().RefNativeWindow();
			if (!_pWind) return;

			int _wheel = 0;
			_pWind->ConsumeWheelDelta(_wheel);
		}
	}

	void InputManager::Init()
	{
		m_upInputDeviceManager = std::make_unique<InputDeviceManager>();
		m_upInputDeviceManager->Init();

		// 保存されている設定を反映する
		// この時点ではまだウィンドウが生成されていないため、
		// 中心座標はここでは求めず、固定を行うフレームごとに実測する
		SetCursorCentered(m_config.isCursorLockedToCenter);

		// システム用の入力を用意する
		RegisterSystemDevice();
	}

	//======================================================================================
	// システム用の入力
	//--------------------------------------------------------------------------------------
	// ゲームの操作(アプリ側が GameManager で登録するもの)とは別の束にしてある。
	//
	//   ・エディターとゲームの行き来はエンジン側の機能なので、アプリの登録漏れで
	//     戻れなくなることがないよう、ここで必ず登録しておく
	//   ・アプリ側が同じ名前("Keyboard")でデバイスを登録し直しても消えない
	//   ・プレイモードでなくても拾う必要がある(取得は IsSystemPress を使う)
	//
	// モード切り替えは Ctrl+P。単独のキーだと、エディターで名前を打っているときや
	// ゲーム操作と取り合いになるため、修飾キー付きにしてある。
	//======================================================================================
	void InputManager::RegisterSystemDevice()
	{
		auto _upSystem = std::make_unique<InputCollector>();

		_upSystem->AddButton(
			SYSTEM_ACTION_TOGGLE_APPMODE,
			std::make_unique<InputButtonForWindowsChord>('P', std::initializer_list<int>{ VK_CONTROL }));

		// 切り替えの瞬間に走る入力リセットで、この束だけは捨てない
		// (捨てると押しっぱなしが押した瞬間へ戻り、押している間ずっと切り替わる)
		_upSystem->SetKeepOnReset(true);

		AddDevice(SYSTEM_DEVICE_NAME, std::move(_upSystem));
	}

	void InputManager::Update()
	{
		if (!m_isActive) return;

		// 接続の確認と、最後に触ったデバイスの判定
		// 生のマウス移動量・ホイールはこの後で取り出されて0に戻るので、その前に見る
		m_upInputDeviceManager->Update();

		// マウスの固定化
		// エディタ操作中は固定しない。
		// デバッグプレイ中は固定する : 視点の移動量は固定していないと作られないので、
		// ここを外すとマウスで振り向けなくなる
		const auto _mode = MainEngine::Instance().GetMode();
		const bool _isPlaying = (_mode == EAppMode::Game) || (_mode == EAppMode::DebugPlay);
		m_isCursorLockActive = (m_isCursorLockedToCenter && _isPlaying);

		if (m_isCursorLockActive)
		{
			SetCursorLock();
		}
		else
		{
			// 固定していない間の移動量は持ち越さない。
			// 次に固定を開始したフレームは基準を取り直す
			m_deltaX = 0.0f;
			m_deltaY = 0.0f;
			m_needCursorLockReset = true;

			// 生の入力は読むまで足し込まれ続けるので、毎フレーム捨てておく
			DiscardRawMouseDelta();
		}

		// ホイール : 届いたぶんをこのフレームの回転量にする(読まないと溜まり続ける)
		m_wheelDelta = 0.0f;
		if (auto* _pWind = MainEngine::Instance().RefNativeWindow())
		{
			int _rawWheel = 0;
			_pWind->ConsumeWheelDelta(_rawWheel);
			m_wheelDelta = static_cast<float>(_rawWheel) / static_cast<float>(WHEEL_DELTA);
		}

		// 登録された入力でバスの更新を行う
		// (UIキャプチャ中でもデバイス自体は更新し、状態遷移の整合を保つ。
		//  実際に「入力なし」として扱うのは取得側で判定する)
		InputContext _context = {};
		_context.isMouseLockToCenter = m_isCursorLockActive;
		_context.deltaX = m_deltaX;
		_context.deltaY = m_deltaY;
		_context.deltaTime = MainEngine::Instance().GetUnscaledDeltaTime();
		_context.pDeviceManager = m_upInputDeviceManager.get();
		for (auto& _device : m_upInputDeviceMap)
		{
			_device.second->Update(_context);
		}
	}

	//======================================================================================
	// 溜まっている入力状態を捨てる
	//--------------------------------------------------------------------------------------
	// モードの切り替え時に呼ぶ。押しっぱなしのまま切り替えると、戻ってきたフレームが
	// 「押した瞬間(Press)」を挟まずに Hold から始まってしまう。
	// マウスは移動量と固定の基準も捨てる。切り替えの前後でカーソルが飛んでいるので、
	// そのまま差分を取ると1フレーム目に大きな視点移動が入ってしまう。
	//======================================================================================
	void InputManager::ResetInput()
	{
		m_deltaX = 0.0f;
		m_deltaY = 0.0f;
		m_needCursorLockReset = true;
		m_lockAnchorPos = {};

		// 切り替え前に溜まっていた生の移動量も捨てる
		DiscardRawMouseDelta();

		// ホイールも同じ
		m_wheelDelta = 0.0f;
		DiscardWheelDelta();

		for (auto& _device : m_upInputDeviceMap)
		{
			if (!_device.second) continue;

			// 切り替えに使ったキー自体は捨てない。
			// 捨てると押しっぱなしが「押した瞬間」に戻り、押している間ずっと
			// 切り替わり続けてしまう
			if (_device.second->IsKeepOnReset()) continue;

			_device.second->ResetInput();
		}
	}

	void InputManager::SetCursorCentered(bool a_enable)
	{
		m_isCursorLockedToCenter = a_enable;
		ENGINE_LOG("マウス座標固定化 : %s", BOOL_STR(a_enable));
	}

	//======================================================================================
	// カーソルをクライアント領域の中心へ戻し、そのフレームの移動量を確定させる
	//--------------------------------------------------------------------------------------
	// 移動量は WM_INPUT で届いた「デバイスが報告したカウント」を使う。
	// カーソル座標の差分は使わない。あちらは
	//   ・ポインターの速度スライダーで拡縮される(既定の半分なら動きも半分になる)
	//   ・「ポインターの精度を高める」で速さに応じて倍率が変わる(素早く振ると非線形になる)
	//   ・ピクセル単位に丸められ、端数が捨てられる
	//   ・デスクトップの端でクランプされ、大きく振ったぶんが頭打ちになる
	// という加工を通った後の値で、素早く振ったときほど実際の動きから離れてしまう。
	//
	// カーソルを中央へ戻すのは今までどおり続ける。移動量の取得には使わないが、
	// 画面外へ出てウィンドウの外をクリックしてしまうのを防ぐ役目がある。
	//======================================================================================
	void InputManager::SetCursorLock()
	{
		if (!m_isActive) return;
		auto* _pWind = MainEngine::Instance().RefNativeWindow();

		// ---- 移動量 : 生の入力から取る ----
		bool _hasRawDelta = false;
		if (_pWind)
		{
			int _rawX = 0;
			int _rawY = 0;
			_pWind->ConsumeRawMouseDelta(_rawX, _rawY);

			// 感度はここでしか掛からない(Windows側の設定を通っていないため)
			const auto& _inputConfig = m_config;

			// 作り手の感度 × プレイヤーの感度
			m_deltaX = static_cast<float>(_rawX) * _inputConfig.lookSensitivityX * m_userLookSensitivity;
			m_deltaY = static_cast<float>(_rawY) * _inputConfig.lookSensitivityY * m_userLookSensitivity;

			// 上下の反転はここで掛ける。
			// 後段(InputAxisForWindowsMouse)は「下が正」で届いた値を軸の向きへ
			// 直すために必ず符号を反転するので、あちらでやると設定と実際が食い違う
			if (IsInvertLookY()) m_deltaY = -m_deltaY;

			_hasRawDelta = true;
		}

		// ---- カーソルの固定 ----
		POINT _center = {};
		if (!GetClientCenterPos(_center))
		{
			// ウィンドウが取れない・最小化中などは移動量なし
			m_deltaX = 0.0f;
			m_deltaY = 0.0f;
			m_needCursorLockReset = true;
			return;
		}

		POINT _nowPos = {};
		const bool _hasCursorPos = (GetCursorPos(&_nowPos) != FALSE);

		// 生の入力が使えないとき(登録に失敗した等)だけ、従来どおり
		// カーソル座標の差分で代用する
		if (!_hasRawDelta)
		{
			if (!_hasCursorPos)
			{
				m_deltaX = 0.0f;
				m_deltaY = 0.0f;
				m_needCursorLockReset = true;
				return;
			}

			if (m_needCursorLockReset)
			{
				// 固定を開始したフレームは基準が無いので移動量を出さない
				m_deltaX = 0.0f;
				m_deltaY = 0.0f;
			}
			else
			{
				// 前フレームに「実際にカーソルを置いた位置」からの差分
				m_deltaX = static_cast<float>(_nowPos.x - m_lockAnchorPos.x);
				m_deltaY = static_cast<float>(_nowPos.y - m_lockAnchorPos.y);
			}
		}

		m_needCursorLockReset = false;

		// マウスを中央へ固定
		SetCursorPos(_center.x, _center.y);

		// 実際に移動できた座標を次フレームの基準にする。
		// 画面端でのクランプやDPIの丸めで要求どおりに移動できない場合があり、
		// 要求値を基準にすると毎フレーム同じ差分が残り続けて視点が震える
		// (生の入力を使っている間は参照されないが、代用へ落ちたときのために更新しておく)
		if (!GetCursorPos(&m_lockAnchorPos))
		{
			m_lockAnchorPos = _center;
		}
	}

	// ゲーム入力を受け付けてよい状態か(判定は上のヘルパーと同じ)
	bool InputManager::IsGameInputEnable() const
	{
		if (!m_isActive) return false;
		return IsPlayMode() && !IsUICapturingInput();
	}

	// カーソルのクライアント領域内の座標を取得する
	bool InputManager::GetCursorClientPos(Math::Vector2& a_outPos) const
	{
		auto* _pWind = MainEngine::Instance().GetNativeWindow();
		if (!_pWind) return false;

		const HWND _hWnd = _pWind->GetWindowHandle();
		if (!_hWnd) return false;

		POINT _pos = {};
		if (!GetCursorPos(&_pos)) return false;
		if (!ScreenToClient(_hWnd, &_pos)) return false;

		a_outPos = { static_cast<float>(_pos.x), static_cast<float>(_pos.y) };
		return true;
	}

	// クライアント領域の中心をスクリーン座標で取得する
	bool InputManager::GetClientCenterPos(POINT& a_outPos) const
	{
		auto* _pWind = MainEngine::Instance().GetNativeWindow();
		if (!_pWind) return false;

		const HWND _hWnd = _pWind->GetWindowHandle();
		if (!_hWnd) return false;

		// ウィンドウの移動・リサイズ・フルスクリーン切替に追従するため毎回取り直す
		RECT _clientRect = {};
		if (!GetClientRect(_hWnd, &_clientRect)) return false;

		const LONG _width = _clientRect.right - _clientRect.left;
		const LONG _height = _clientRect.bottom - _clientRect.top;

		// 最小化中はクライアント領域が0になる
		if (_width <= 0 || _height <= 0) return false;

		// クライアント座標の中心を求めてからスクリーン座標へ変換する
		// (呼び出し側のメンバをそのまま渡すと、変換が累積して中心がずれていく)
		a_outPos.x = _clientRect.left + _width / 2;
		a_outPos.y = _clientRect.top + _height / 2;

		return (ClientToScreen(_hWnd, &a_outPos) != FALSE);
	}

	// 任意のアプリケーションボタンの入力状態を取得
	InputButtonBase::EState InputManager::GetButtonState(ActionKey a_action) const
	{
		if (!m_isActive)			return InputButtonBase::EState::Free;		// 入力検知を開始しているかどうか
		if (!IsPlayMode())			return InputButtonBase::EState::Free;		// プレイモード以外はゲーム入力を渡さない
		if (IsUICapturingInput())	return InputButtonBase::EState::Free;		// エディタ操作中(テキスト入力など)はゲーム入力を無効化

		InputButtonBase::EState _buttonState = InputButtonBase::EState::Free;
		for (auto& _device : m_upInputDeviceMap)
		{
			// 有効な時のみ入力に影響を与える
			if (_device.second->GetActiveState() == InputCollector::EActiveState::Enable)
			{
				_buttonState |= _device.second->GetButtonState(a_action);
			}
		}
		return _buttonState;
	}

	// 任意のアプリケーションボタンが押されていない状態か判定
	bool InputManager::IsFree(ActionKey a_action) const
	{
		if (!m_isActive) return false;
		return (GetButtonState(a_action) == InputButtonBase::EState::Free);
	}
	bool InputManager::IsPress(ActionKey a_action) const
	{
		if (!m_isActive) return false;
		return Core::HasFlag(GetButtonState(a_action), InputButtonBase::EState::Press);
	}
	bool InputManager::IsHold(ActionKey a_action) const
	{
		if (!m_isActive) return false;
		return Core::HasFlag(GetButtonState(a_action), InputButtonBase::EState::Hold);
	}
	bool InputManager::IsRelease(ActionKey a_action) const
	{
		if (!m_isActive) return false;
		return Core::HasFlag(GetButtonState(a_action), InputButtonBase::EState::Release);
	}

	//======================================================================================
	// システム用の入力状態
	//--------------------------------------------------------------------------------------
	// プレイモードかどうかを見ない。エディターに居るときに押すもの
	// (モードの切り替え)を拾うためのもの。
	// 反対に「エディター操作に反応してほしくないもの」はこちらを使わないこと。
	//======================================================================================
	InputButtonBase::EState InputManager::GetSystemButtonState(ActionKey a_action) const
	{
		if (!m_isActive) return InputButtonBase::EState::Free;

		InputButtonBase::EState _buttonState = InputButtonBase::EState::Free;
		for (auto& _device : m_upInputDeviceMap)
		{
			if (!_device.second) continue;

			// 有効な時のみ入力に影響を与える
			if (_device.second->GetActiveState() == InputCollector::EActiveState::Enable)
			{
				_buttonState |= _device.second->GetButtonState(a_action);
			}
		}
		return _buttonState;
	}

	bool InputManager::IsSystemPress(ActionKey a_action) const
	{
		return Core::HasFlag(GetSystemButtonState(a_action), InputButtonBase::EState::Press);
	}

	bool InputManager::IsSystemHold(ActionKey a_action) const
	{
		return Core::HasFlag(GetSystemButtonState(a_action), InputButtonBase::EState::Hold);
	}

	bool InputManager::IsInvertLookY() const
	{
		if (m_opUserInvertLookY) return m_opUserInvertLookY.value();
		return m_config.isInvertLookY;
	}

	// ホイールの回転量 : ボタンと同じく、ゲーム入力を受け付けない間は 0
	float InputManager::GetWheelDelta() const
	{
		if (!IsGameInputEnable()) return 0.0f;
		return m_wheelDelta;
	}

	// 任意の軸の入力状態を取得
	// 指定した入力デバイスの任意の軸の入力状d態を2次元ベクトルで取得する
	Math::Vector2 InputManager::GetAxisState(ActionKey a_action) const
	{
		if (!m_isActive) return Math::Vector2(0.0f, 0.0f);
		// プレイモード以外はゲーム入力を渡さない
		if (!IsPlayMode()) return Math::Vector2(0.0f, 0.0f);

		// エディタ操作中(テキスト入力など)はゲーム入力を無効化
		if (IsUICapturingInput()) return Math::Vector2(0.0f, 0.0f);

		float _leftValue = 0.0f;
		float _rightValue = 0.0f;
		float _topValue = 0.0f;
		float _bottomValue = 0.0f;

		for (auto& _collector : m_upInputDeviceMap)
		{
			// 有効な時のみ入力に影響を与える
			if (_collector.second->GetActiveState() == InputCollector::EActiveState::Enable)
			{
				Math::Vector2 _nowDeviceAxis = {};
				_nowDeviceAxis = _collector.second->GetAxisState(a_action);

				// 入力がなければスキップ
				if (_nowDeviceAxis.LengthSquared() == 0.0f) continue;

				// 左右の入力をX軸数値で判定
				if (_nowDeviceAxis.x < 0.0f)
				{
					// 左なら最小値を保持
					_leftValue = std::min(_nowDeviceAxis.x, _leftValue);
				}
				else
				{
					// 右なら最大値を保持
					_rightValue = std::max(_nowDeviceAxis.x,_rightValue);
				}

				// 上下の入力をY軸数値で判定
				if (_nowDeviceAxis.y < 0.0f)
				{
					// 下なら最小値を保持
					_bottomValue = std::min(_nowDeviceAxis.y ,_bottomValue);
				}
				else
				{
					// 上なら最大値を保持
					_topValue = std::max(_nowDeviceAxis.y,_topValue);
				}
			}
		}

		// 最終的に左右と上下の入力値をそれぞれ合成したものを出力
		return Math::Vector2(_leftValue + _rightValue,_topValue + _bottomValue);
	}

	// 入力コレクター(入力デバイス)の追加
	void InputManager::AddDevice(std::string_view a_name, InputCollector* a_pInputDevice)
	{
		std::unique_ptr<InputCollector> _upNewDevice(a_pInputDevice);
		AddDevice(a_name,std::move(_upNewDevice));
	}
	void InputManager::AddDevice(std::string_view a_name, std::unique_ptr<InputCollector> a_upInputDevice)
	{
		m_upInputDeviceMap[a_name.data()] = std::move(a_upInputDevice);
		return;
	}

	const std::unique_ptr<InputCollector>& InputManager::GetDevice(std::string_view a_name) const
	{
		auto _device = m_upInputDeviceMap.find(a_name.data());

		if (_device == m_upInputDeviceMap.end())
		{
			ENGINE_ERRLOG(false, "未登録のデバイスです");
		}
		return _device->second;
	}
	std::unique_ptr<InputCollector>& InputManager::RefDevice(std::string_view a_name)
	{
		auto _device = m_upInputDeviceMap.find(a_name.data());

		if (_device == m_upInputDeviceMap.end())
		{
			ENGINE_ERRLOG(false, "未登録のデバイスです");
		}
		return _device->second;
	}

	void InputManager::Release()
	{
		m_upInputDeviceMap.clear();
	}
	InputManager::InputManager()
	{}
	InputManager::~InputManager()
	{
		Release();
	}
}


