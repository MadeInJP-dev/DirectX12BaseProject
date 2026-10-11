#pragma once

namespace Engine::Window
{


	struct WindowDesc
	{
		UINT width = 0;						// ウィンドウの横幅
		UINT height = 0;					// ウィンドウの縦幅
		std::wstring titleName = L"Game";				// タイトルバーの名前
		std::wstring className = L"GameWindow";			// クラス名
		EWindowMode windowMode = EWindowMode::Windowed;	// ウィンドウモード
	};

	class NativeWindow
	{
	public:

		// ウィンドウの作成
		bool Create(const WindowDesc& a_desc);

		// 解放処理
		void Release();

		// メッセージ処理
		bool ProcessMessage();

		// タイトルの変更
		void ChangeTitle(const std::string& a_title);

		// ウィンドウモードの変更
		void ChangeWindowMode(EWindowMode a_nextWindowMode);

		/// <summary>
		/// HWNDから実際のクライアント領域サイズを取り直す
		/// ウィンドウプロシージャ(WM_SIZE)からも呼ばれる
		/// </summary>
		void RefreshClientSize();

		/// <summary>
		/// 溜まっている生のマウス移動量を取り出す(取り出したら0に戻る)
		/// </summary>
		/// <param name="a_outX">横の移動量(マウスのカウント。右が正)</param>
		/// <param name="a_outY">縦の移動量(マウスのカウント。下が正)</param>
		/// <remarks>
		/// WM_INPUT で届いた移動量をそのまま足し合わせたもの。
		/// カーソル座標の差分と違い、Windowsのポインター速度・ポインターの精度を高める
		/// (加速)・画面端でのクランプ・ピクセル単位への丸めを一切通っていない。
		/// 視点操作はこちらを使うこと。
		///
		/// 1フレームに複数回届くので、読むまで足し込み続ける。
		/// 読み捨てないと溜まり続けるため、毎フレーム必ず呼ぶこと。
		/// </remarks>
		void ConsumeRawMouseDelta(int& a_outX, int& a_outY);

		/// <summary>
		/// WM_INPUT を処理して移動量を足し込む(ウィンドウプロシージャから呼ばれる)
		/// </summary>
		void OnRawInput(LPARAM a_lParam);

		/// <summary>
		/// 溜まっているホイールの回転量を取り出す(取り出したら0に戻る)
		/// </summary>
		/// <param name="a_outDelta">回転量(WM_MOUSEWHEEL の値。奥へ回すと正、1ノッチが WHEEL_DELTA)</param>
		/// <remarks>読み捨てないと溜まり続けるため、毎フレーム必ず呼ぶこと</remarks>
		void ConsumeWheelDelta(int& a_outDelta);

		/// <summary>
		/// WM_MOUSEWHEEL を処理して回転量を足し込む(ウィンドウプロシージャから呼ばれる)
		/// </summary>
		void OnMouseWheel(int a_delta);

		// アクセサ
		const HWND& GetWindowHandle() const { return m_hWnd; }
		const UINT& GetClientWidth() const { return m_clientWidth; }
		const UINT& GetClientHeight() const { return m_clientHeight; }

		/// <summary>
		/// クライアント領域でOSのカーソルを消すか
		/// </summary>
		/// <remarks>
		/// 自前のカーソル画像を描いている間だけ true にする(切り替えるのはアプリ側)。
		/// ウィンドウプロシージャが WM_SETCURSOR でこれを見る。
		/// 絵を出せていないうちから消すとカーソルが1つも無い状態になるので、
		/// 「消す」と「描く」は必ず対で切り替えること。
		/// </remarks>
		void SetCursorHidden(bool a_isHidden) { m_isCursorHidden = a_isHidden; }
		bool IsCursorHidden() const { return m_isCursorHidden; }

		// メモリ使用率取得
		// バイト単位での取得
		double GetMemoryUsage() const;

		//----------------------------------------------------------------------------------
		// メッセージの横取り
		//
		// 既定の処理より先にメッセージを渡す先。処理したら true を返す(以降は何もしない)。
		// エディター(ImGui)が入力を受け取るために登録する。
		// ウィンドウ側は ImGui を知らないので、つなぐのはエディターの役
		//----------------------------------------------------------------------------------
		using MessageHook = bool(*)(HWND a_hWnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam);
		void SetMessageHook(MessageHook a_hook) { m_messageHook = a_hook; }

		bool CallMessageHook(HWND a_hWnd, UINT a_message, WPARAM a_wParam, LPARAM a_lParam) const
		{
			return m_messageHook && m_messageHook(a_hWnd, a_message, a_wParam, a_lParam);
		}

		//----------------------------------------------------------------------------------
		// フォーカスの出入り
		//
		// 選択された(true)・外れた(false)ときに呼ぶ先。入力を止める・戻すのに使う。
		// ウィンドウ側は入力を知らないので、つなぐのは持ち主(MainEngine)の役。
		// 作成の途中からも届くので Create より前に登録すること
		//----------------------------------------------------------------------------------
		using FocusCallback = std::function<void(bool a_isActive)>;
		void SetFocusCallback(FocusCallback a_callback) { m_focusCallback = std::move(a_callback); }

		void CallFocusCallback(bool a_isActive) const
		{
			if (m_focusCallback) m_focusCallback(a_isActive);
		}

	private:

		/// <summary>
		/// クライアント領域が要求サイズになるようウィンドウ全体のサイズを補正する
		/// 枠の太さはDPIによって変わるため、作成後の実測値から差分を求めて合わせる。
		/// モニターの作業領域に収まらない場合は収まるところまで縮める
		/// </summary>
		void FitClientSize(UINT a_width, UINT a_height);

		/// <summary>
		/// 生のマウス入力(WM_INPUT)を受け取れるように登録する
		/// </summary>
		void RegisterRawMouse();

	private:
		// Windows用
		// ウィンドウプロシージャは CreateWindowEx の途中からも呼ばれるため、
		// ハンドルは必ずnullptrで初期化しておく
		HWND		m_hWnd = nullptr;	// ウィンドウハンドル
		HINSTANCE	m_hInst = nullptr;	// ウィンドウインスタンス
		std::wstring m_className;	// ウィンドウクラスネーム

		// ウィンドウモード時のサイズと位置を記憶しておく
		// フルスクリーンから戻した際に元のサイズと位置に戻すため
		RECT m_windowedRect = { 0,0,0,0 };

		// スタイル保持
		DWORD m_windowedStyle = WS_OVERLAPPEDWINDOW;

		// クライアント領域でOSのカーソルを消すか
		bool m_isCursorHidden = false;

		// メッセージの横取り先
		MessageHook m_messageHook = nullptr;

		// フォーカスの出入りの通知先
		FocusCallback m_focusCallback = nullptr;

		// ウィンドウ設定
		UINT m_clientWidth = 0;
		UINT m_clientHeight = 0;

		// ウィンドウモード
		EWindowMode m_windowMode = EWindowMode::Windowed;

		// 現在のウィンドウ設定
		WINDOWPLACEMENT m_windowPlacement = {};

		//-------------------------------------------------------------------
		// 生のマウス移動量(WM_INPUT)
		//-------------------------------------------------------------------
		// 読み出されるまで足し込み続ける。1フレームに複数回メッセージが来るため、
		// 「最後の1件」ではなく合計を持つ必要がある。
		int  m_rawMouseDeltaX = 0;
		int  m_rawMouseDeltaY = 0;

		// 絶対座標で報告してくる機器(リモートデスクトップ・ペンタブ等)用。
		// 相対値が入っていないので、前回位置との差から自前で作る
		bool m_hasPrevRawAbsolutePos = false;
		LONG m_prevRawAbsoluteX = 0;
		LONG m_prevRawAbsoluteY = 0;

		//-------------------------------------------------------------------
		// ホイール(WM_MOUSEWHEEL)
		//-------------------------------------------------------------------
		// 移動量と同じく、読み出されるまで足し込み続ける
		int  m_wheelDelta = 0;
	};
}