#include "NativeWindow.h"

// プロセスのメモリ使用量の取得はここだけで使う。
// プリコンパイル済みヘッダーへ置くと全翻訳単位に広がるため
#include <psapi.h>

// 古いSDK対策 (Windows8.1+ のヘッダにしか定義がない)
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef _DPI_AWARENESS_CONTEXTS_
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

namespace Engine::Window
{
	namespace
	{
		/// <summary>
		/// マニフェストを使わずにDPI対応を有効化する
		///
		/// 必ずウィンドウ作成より前に呼ぶこと。
		/// ウィンドウのDPI意識レベルは生成時のスレッド設定で決まるため、
		/// 後から有効化しても既存のウィンドウには反映されず、
		/// OSによる拡大(DPI仮想化)が掛かったままになる。
		/// そうなるとGetClientRectが返す値と実際のピクセル数が食い違い、
		/// ImGuiのレイアウトが画面外へはみ出す。
		/// </summary>
		void EnableDpiAwareness()
		{
			using PFN_SetProcessDpiAwarenessContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

			// Windows10 1703以降ならモニター単位で追従できる
			// (user32はGUIアプリなら必ずロード済みなのでモジュールを引くだけ)
			if (HMODULE _user32 = GetModuleHandleW(L"user32.dll"))
			{
				auto _setContextFunc = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
					GetProcAddress(_user32, "SetProcessDpiAwarenessContext")
				);
				if (_setContextFunc && _setContextFunc(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
				{
					return;
				}
			}

			// 取れなければシステムDPI基準で妥協する
			SetProcessDPIAware();
		}

		/// <summary>
		/// 各種メッセージを処理する関数
		/// </summary>
		/// <param name="a_hWnd">ウィンドウハンドル</param>
		/// <param name="a_message">メッセージID</param>
		/// <param name="a_wParam">メッセージの追加情報</param>
		/// <param name="a_lParam">メッセージの追加情報</param>
		/// <returns>処理結果</returns>
		LRESULT CALLBACK WndProc(HWND a_hWnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam)
		{
			// CreateWindowExで渡した this をウィンドウ側へ保存する
			// (WM_NCCREATEはCreateWindowExの中で最初に届くので、以降のメッセージで使える)
			if (a_message == WM_NCCREATE)
			{
				auto* _pCreateStruct = reinterpret_cast<CREATESTRUCT*>(a_lParam);
				SetWindowLongPtr(a_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(_pCreateStruct->lpCreateParams));
			}
			auto* _pWindow = reinterpret_cast<Engine::Window::NativeWindow*>(GetWindowLongPtr(a_hWnd, GWLP_USERDATA));

			//==============================================================================
			// OSのカーソルを消す
			//------------------------------------------------------------------------------
			// クライアント領域の上だけ消して、代わりに自前の画像をカーソル位置へ描く
			// (描くのはアプリ側。ゲームモードの間だけ)。枠やリサイズの境目はOSに任せるので
			// ヒットテストが HTCLIENT のときだけ止める。
			//
			// 横取り先(エディターの ImGui)より前に置くこと。あちらも WM_SETCURSOR を拾って
			// カーソルの形を設定するので、後ろに回すと上書きされて消えなくなる。
			//
			// 消してよいかどうかはアプリ側のカーソルが SetCursorHidden で決める(画像を出せているフレームだけ true)。
			// 出せないうちから消すと、カーソルが1つも無い状態になってしまう。
			//==============================================================================
			if (a_message == WM_SETCURSOR &&
				_pWindow && _pWindow->IsCursorHidden() &&
				LOWORD(a_lParam) == HTCLIENT)
			{
				SetCursor(nullptr);
				return TRUE;
			}

			// エディターが先に受け取る(ImGui の入力)
			if (_pWindow && _pWindow->CallMessageHook(a_hWnd, a_message, a_wParam, a_lParam))
				return true;

			// ウィンドウズからのメッセージを処理
			switch (a_message)
			{
			case WM_SIZE:					// クライアント領域のサイズが変わった
				// 最小化はクライアント領域が0になるだけなので無視する
				if (a_wParam != SIZE_MINIMIZED && _pWindow)
				{
					_pWindow->RefreshClientSize();
				}
				break;
			case WM_DPICHANGED:				// モニター間の移動や表示スケール変更でDPIが変わった
			{
				// OSが提案してくるサイズへ合わせる
				// (無視すると新しいDPIの枠と中身の大きさがずれる)
				const RECT* _pSuggestedRect = reinterpret_cast<const RECT*>(a_lParam);
				SetWindowPos(
					a_hWnd,
					nullptr,
					_pSuggestedRect->left,
					_pSuggestedRect->top,
					_pSuggestedRect->right - _pSuggestedRect->left,
					_pSuggestedRect->bottom - _pSuggestedRect->top,
					SWP_NOZORDER | SWP_NOACTIVATE
				);
				break;
			}
			case WM_INPUT:					// 生のマウス移動量が届いた
				if (_pWindow)
				{
					_pWindow->OnRawInput(a_lParam);
				}
				break;
			case WM_MOUSEWHEEL:				// ホイールが回された
				if (_pWindow)
				{
					_pWindow->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(a_wParam));
				}
				break;
			case WM_DESTROY:				// OSに対して終了を伝える
				PostQuitMessage(0);
				break;
			case WM_SETFOCUS:				// ウィンドウが選択された際
			{
				if (_pWindow) _pWindow->CallFocusCallback(true);
				break;
			}
			case WM_KILLFOCUS:				// ウィンドウの選択が外された際
			{
				if (_pWindow) _pWindow->CallFocusCallback(false);
				break;
			}
			default:
				break;
			}

			// メッセージの基本的な処理
			return DefWindowProc(a_hWnd, a_message, a_wParam, a_lParam);
		}
	}
}

//==================================================================================
// 
// ウィンドウの作成
//
//==================================================================================
namespace Engine::Window
{
	bool NativeWindow::Create(const  WindowDesc& a_desc)
	{
		// DPI対応の有効化
		// ウィンドウを作る前に済ませないとDPI仮想化が掛かり、
		// 論理サイズと実ピクセル数がずれてImGuiのサイズが合わなくなる
		EnableDpiAwareness();

		// 実行ファイルのインスタンスハンドル取得
		m_hInst = GetModuleHandle(nullptr);
		if (!m_hInst)
		{
			ENGINE_ERRLOG(false, "インスタンスハンドルの取得に失敗");
			return false;
		}

		// ウィンドウの仕様書作成
		WNDCLASSEX _wc = {};
		_wc.cbSize = sizeof(WNDCLASSEX);									// 構造体サイズ
		_wc.style = CS_OWNDC;												// 描画スタイル
		_wc.lpfnWndProc = WndProc;									// ウィンドウ関数
		_wc.hIcon = LoadIcon(m_hInst, IDI_APPLICATION);						// ウィンドウのアイコン（Alt+Tabなど）
		// 標準カーソルは hInstance に nullptr を渡すこと(自前のモジュールを指すと取得できない)。
		// 自前カーソルを切ったときはこれが出る
		_wc.hCursor = LoadCursor(nullptr, IDC_ARROW);						// ウィンドウ内のデフォルトカーソル
		_wc.hInstance = m_hInst;											// インスタンスハンドル
		_wc.hbrBackground = nullptr;										// 初期の背景塗りつぶし色
		_wc.lpszMenuName = nullptr;											// ウィンドウクラスのメニューリソースID
		_wc.lpszClassName = a_desc.className.c_str();						// ウィンドウのクラス名（ユニーク性必須）
		_wc.hIconSm = LoadIcon(m_hInst, IDI_APPLICATION);					// タスクバーとかのアイコン
		// ウィンドウクラスの登録（同じクラス名は使わないこと）
		if (!RegisterClassEx(&_wc))
		{
			ENGINE_ERRLOG(false, "ウィンドウクラスの登録失敗");
			return false;
		}

		// モードに応じたスタイルの設定
		DWORD _style = 0;
		DWORD _exStyle = 0;
		int _x = CW_USEDEFAULT;
		int _y = CW_USEDEFAULT;
		UINT _width = a_desc.width;
		UINT _height = a_desc.height;

		switch (a_desc.windowMode)
		{
			case EWindowMode::Windowed:
				_style = WS_OVERLAPPEDWINDOW;
				break;
			case EWindowMode::FullScreen:
			case EWindowMode::Borederless:
				// 枠なしスタイル
				_style = WS_POPUP | WS_VISIBLE;
				_x = 0;
				_y = 0;
				// モニターの解像度を取得してウィンドウサイズにする
				_width = GetSystemMetrics(SM_CXSCREEN);
				_height = GetSystemMetrics(SM_CYSCREEN);
				break;
		}

		// ウィンドウサイズの調整（クライアント領域の設定 = 描画できる中身の部分）
		RECT _rect = { 0,0,static_cast<LONG>(_width),static_cast<LONG>(_height) };
		if (a_desc.windowMode == EWindowMode::Windowed)
		{
			// ウィンドウモードの時のみ枠の計算をする（フルスクリーンとボーダレスは枠なしスタイルなので必要ない）
			AdjustWindowRect(&_rect, _style, FALSE);
		}

		// ウィンドウの生成
		m_hWnd = CreateWindowEx(
			_exStyle,					// 拡張スタイル
			a_desc.className.c_str(),	// ウィンドウのクラス名（登録したのと同じもの）
			a_desc.titleName.c_str(),	// タイトルバーの名前
			_style,						// ウィンドウのスタイル
			_x,							// ウィンドウの X座標
			_y,							// ウィンドウの Y座標
			_rect.right - _rect.left,	// 幅
			_rect.bottom - _rect.top,	// 高さ
			nullptr,					// 親ウィンドウハンドル（トップレベルのため無し）
			nullptr,					// メニューハンドル（無し）
			m_hInst,					// 登録したときと同じハンドルを渡す
			this						// 作成パラメタ : ウィンドウプロシージャから自身を触るため
		);
		if (!m_hWnd) {
			ENGINE_ERRLOG(false, "CreateWindowEx failed (GetLastError = %lu)", GetLastError());
			return false;
		}

		// ウィンドウを表示
		ShowWindow(m_hWnd, SW_SHOWNORMAL);

		// ウィンドウの更新
		UpdateWindow(m_hWnd);

		// ウィンドウにフォーカスする
		SetFocus(m_hWnd);					// キー入力受付開始命令も含む

		// メンバ変数の保存
		m_className = a_desc.className;
		m_windowMode = a_desc.windowMode;

		// クライアント領域を要求サイズへ合わせる
		// 枠の太さはDPIによって変わるため、AdjustWindowRectの結果だけでは合わない
		if (a_desc.windowMode == EWindowMode::Windowed)
		{
			FitClientSize(a_desc.width, a_desc.height);
		}

		// 実際のクライアント領域サイズを保持する
		// 要求値をそのまま持つと、枠やモニター解像度で丸められた実サイズと
		// 食い違ってImGuiやスワップチェインのサイズが合わなくなる
		RefreshClientSize();

		// 現在のウィンドウ設定を保持
		m_windowPlacement.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(m_hWnd, &m_windowPlacement);

		// 視点操作用に生のマウス入力を受け取れるようにする
		RegisterRawMouse();

		return true;
	}

	//==================================================================================
	// 生のマウス入力(WM_INPUT)
	//----------------------------------------------------------------------------------
	// 視点操作にカーソル座標の差分を使ってはいけない。カーソル座標は
	//   ・ポインターの速度(コントロールパネルのスライダー)で拡縮され
	//   ・「ポインターの精度を高める」(加速)で移動の速さに応じて倍率が変わり
	//   ・ピクセル単位へ丸められ(端数は切り捨てられて消える)
	//   ・デスクトップの端でクランプされる
	// という加工を全部通った後の値で、素早く振ったときほど元の動きから離れる。
	//
	// WM_INPUT はデバイスが報告したカウントがそのまま届くので、上のどれも通らない。
	// 1フレームに複数回届くため、読み出すまで足し込んでおく。
	//==================================================================================
	void NativeWindow::RegisterRawMouse()
	{
		if (!m_hWnd) return;

		RAWINPUTDEVICE _device = {};
		_device.usUsagePage = 0x01;		// Generic Desktop Controls
		_device.usUsage     = 0x02;		// Mouse
		_device.dwFlags     = 0;		// フォーカスがある間だけ受け取る(RIDEV_INPUTSINK は付けない)
		_device.hwndTarget  = m_hWnd;

		if (!RegisterRawInputDevices(&_device, 1, sizeof(_device)))
		{
			// 受け取れなくても致命傷ではない。
			// InputManager 側がカーソル座標の差分へ自動で切り替える
			ENGINE_WARNING("[Window] 生のマウス入力を登録できませんでした。カーソル座標から視点移動を作ります");
		}
	}

	void NativeWindow::OnRawInput(LPARAM a_lParam)
	{
		RAWINPUT _raw = {};
		UINT _size = sizeof(_raw);

		const UINT _result = GetRawInputData(
			reinterpret_cast<HRAWINPUT>(a_lParam),
			RID_INPUT,
			&_raw,
			&_size,
			sizeof(RAWINPUTHEADER));

		// 取得できなかった場合は (UINT)-1 が返る
		if (_result == static_cast<UINT>(-1)) return;
		if (_raw.header.dwType != RIM_TYPEMOUSE) return;

		const RAWMOUSE& _mouse = _raw.data.mouse;

		if ((_mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
		{
			// 絶対座標で報告してくる機器(リモートデスクトップ・ペンタブ等)。
			// 相対値が入っていないので前回位置との差から作る。
			// 最初の1件は基準が無いので移動量を出さない
			if (m_hasPrevRawAbsolutePos)
			{
				m_rawMouseDeltaX += static_cast<int>(_mouse.lLastX - m_prevRawAbsoluteX);
				m_rawMouseDeltaY += static_cast<int>(_mouse.lLastY - m_prevRawAbsoluteY);
			}

			m_prevRawAbsoluteX      = _mouse.lLastX;
			m_prevRawAbsoluteY      = _mouse.lLastY;
			m_hasPrevRawAbsolutePos = true;
			return;
		}

		// 通常のマウスは相対値がそのまま入っている
		m_hasPrevRawAbsolutePos = false;

		m_rawMouseDeltaX += static_cast<int>(_mouse.lLastX);
		m_rawMouseDeltaY += static_cast<int>(_mouse.lLastY);
	}

	void NativeWindow::ConsumeRawMouseDelta(int& a_outX, int& a_outY)
	{
		a_outX = m_rawMouseDeltaX;
		a_outY = m_rawMouseDeltaY;

		m_rawMouseDeltaX = 0;
		m_rawMouseDeltaY = 0;
	}

	//==================================================================================
	// ホイール(WM_MOUSEWHEEL)
	//----------------------------------------------------------------------------------
	// 移動量と同じく1フレームに複数回届くので、読み出すまで足し込んでおく。
	// 高精度のホイールは WHEEL_DELTA(120) より細かい値で何度も届くため、
	// 1件ずつノッチへ丸めず生の値のまま足す
	//==================================================================================
	void NativeWindow::OnMouseWheel(int a_delta)
	{
		m_wheelDelta += a_delta;
	}

	void NativeWindow::ConsumeWheelDelta(int& a_outDelta)
	{
		a_outDelta = m_wheelDelta;
		m_wheelDelta = 0;
	}

	void NativeWindow::Release()
	{
		// ウィンドウの解放
		UnregisterClass(m_className.c_str(), m_hInst);
	}

	void NativeWindow::RefreshClientSize()
	{
		if (!m_hWnd) return;

		RECT _clientRect = {};
		if (!GetClientRect(m_hWnd, &_clientRect)) return;

		const UINT _width = static_cast<UINT>(_clientRect.right - _clientRect.left);
		const UINT _height = static_cast<UINT>(_clientRect.bottom - _clientRect.top);

		// 最小化中は0が返るので保持しない
		if (_width == 0 || _height == 0) return;

		m_clientWidth = _width;
		m_clientHeight = _height;
	}

	void NativeWindow::FitClientSize(UINT a_width, UINT a_height)
	{
		if (!m_hWnd) return;

		// 枠の太さを実測する（ウィンドウ全体 - クライアント領域）
		RECT _clientRect = {};
		RECT _windowRect = {};
		if (!GetClientRect(m_hWnd, &_clientRect)) return;
		if (!GetWindowRect(m_hWnd, &_windowRect)) return;

		const LONG _frameWidth = (_windowRect.right - _windowRect.left) - (_clientRect.right - _clientRect.left);
		const LONG _frameHeight = (_windowRect.bottom - _windowRect.top) - (_clientRect.bottom - _clientRect.top);

		LONG _targetWidth = static_cast<LONG>(a_width);
		LONG _targetHeight = static_cast<LONG>(a_height);
		LONG _posX = _windowRect.left;
		LONG _posY = _windowRect.top;

		// モニターの作業領域(タスクバーを除いた範囲)に収まるところまで縮めて寄せる
		// はみ出したままだと画面外のパネルが操作できなくなる
		MONITORINFO _monitorInfo = { sizeof(MONITORINFO) };
		if (GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &_monitorInfo))
		{
			const RECT& _work = _monitorInfo.rcWork;

			_targetWidth = std::min(_targetWidth, (_work.right - _work.left) - _frameWidth);
			_targetHeight = std::min(_targetHeight, (_work.bottom - _work.top) - _frameHeight);
			if (_targetWidth <= 0 || _targetHeight <= 0) return;

			// 右下がはみ出す分だけ左上へ寄せる
			_posX = std::min(_posX, _work.right - (_targetWidth + _frameWidth));
			_posY = std::min(_posY, _work.bottom - (_targetHeight + _frameHeight));
			_posX = std::max(_posX, _work.left);
			_posY = std::max(_posY, _work.top);
		}
		if (_targetWidth <= 0 || _targetHeight <= 0) return;

		SetWindowPos(
			m_hWnd,
			nullptr,
			_posX,
			_posY,
			_targetWidth + _frameWidth,
			_targetHeight + _frameHeight,
			SWP_NOZORDER
		);
	}

	bool NativeWindow::ProcessMessage()
	{
		// アプリごとのメッセージキューにアクセスしてメッセージを処理
		MSG _msg = {};
		while (PeekMessage(&_msg, nullptr, 0, 0, PM_REMOVE))
		{
			// 終了メッセージが来た
			if (_msg.message == WM_QUIT)
			{
				return false;
			}

			// メッセージ処理
			TranslateMessage(&_msg);
			DispatchMessage(&_msg);
		}

		return true;
	}

	void NativeWindow::ChangeTitle(const std::string& a_title)
	{
		SetWindowTextA(m_hWnd, a_title.c_str());
	}
	void NativeWindow::ChangeWindowMode(EWindowMode a_nextWindowMode)
	{
		if (m_windowMode == a_nextWindowMode) return;
		m_windowMode = a_nextWindowMode;

		switch (a_nextWindowMode)
		{
		case Engine::EWindowMode::Windowed:
			// ウィンドウスタイル変更
			SetWindowLongPtr(
				m_hWnd,
				GWL_STYLE,
				WS_OVERLAPPEDWINDOW
			);

			// 保存していた位置を復元
			SetWindowPlacement(
				m_hWnd,
				&m_windowPlacement
			);

			// ウィンドウの位置調整
			SetWindowPos(
				m_hWnd,
				nullptr,
				0,
				0,
				0,
				0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED
			);

			break;
		case Engine::EWindowMode::FullScreen:
		case Engine::EWindowMode::Borederless:
			// 現在のウィンドウ設定を保持
			m_windowPlacement.length = sizeof(WINDOWPLACEMENT);
			GetWindowPlacement(m_hWnd, &m_windowPlacement);

			// ウィンドを広げる
			SetWindowLongPtr(
				m_hWnd,
				GWL_STYLE,
				WS_POPUP | WS_VISIBLE
			);

			// ウィンドウの位置を左上に移動
			SetWindowPos(
				m_hWnd,
				HWND_TOP,
				0,
				0,
				GetSystemMetrics(SM_CXSCREEN),
				GetSystemMetrics(SM_CYSCREEN),
				SWP_FRAMECHANGED
			);
			break;
		default:
			break;
		}

		// スタイル変更後のクライアント領域を取り直す
		// (WM_SIZE でも更新されるが、切替直後から正しい値を返せるようにしておく)
		RefreshClientSize();
	}
	double NativeWindow::GetMemoryUsage() const
	{
		PROCESS_MEMORY_COUNTERS_EX _pmc;
		if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&_pmc), sizeof(_pmc)))
		{
			// WorkingSetSizeがタスクマネージャーのメモリに一番近い数値
			// 物理メモリ使用量
			SIZE_T _physMemUsed = _pmc.WorkingSetSize;

			return static_cast<double>(_physMemUsed);
		}

		// 取れなかった
		return 0.0;
	}
}