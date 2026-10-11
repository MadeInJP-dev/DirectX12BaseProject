#pragma once


namespace Engine::Time
{
	class FPSController
	{
	public:
		/// <summary>
		/// フレームの初めに呼び出し : 記録開始
		/// </summary>
		void BeginFrame();

		/// <summary>
		/// フレームの最後に呼び出し : スリープ処理
		/// </summary>
		/// <param name="a_fpsRate">FPSレート設定 : 毎フレーム受け取るので、途中で変えてもすぐ効く</param>
		void EndFrame(EFPSRate a_fpsRate);

		/// <summary>
		/// 現在のフレームレートを取得
		/// </summary>
		UINT GetNowFPS() const { return m_nowFPS; }

	private:

		void FPSMonitor();

		// FPS制御
		UINT m_nowFPS = 0;		// 現在のFPS値

		UINT m_frameCount = 0;		// フレームレート計測用
		std::chrono::steady_clock::time_point m_frameStart;
		std::chrono::steady_clock::time_point m_countFrameStart;
	};
}