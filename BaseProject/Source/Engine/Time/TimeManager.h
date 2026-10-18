#pragma once

namespace Engine::Time
{
	class FPSController;
	class DeltaTime;

	class TimeManager
	{
	public:

		TimeManager();
		~TimeManager();

		/// <summary>
		/// 初期化
		/// </summary>
		void Init();

		/// <summary>
		/// 解放
		/// </summary>
		void Release();

		/// <summary>
		/// フレームの初めに計測
		/// </summary>
		void BeginFrame();

		/// <summary>
		/// フレーム終わりに測定と、必要であればスリープ処理
		/// </summary>
		/// <param name="a_fpsRate">FPSレート設定</param>
		void EndFrame(EFPSRate a_fpsRate);

		/// <summary>
		/// デルタタイム取得 : 時間の倍率を掛けた後の値
		/// </summary>
		float GetDeltaTime() const { return m_scaledDeltaTime; }

		/// <summary>
		/// 倍率を掛ける前のデルタタイム
		/// </summary>
		/// <remarks>
		/// ゲームを止めていても動いてほしいもの(エディターの操作など)はこちらを使う
		/// </remarks>
		float GetUnscaledDeltaTime() const;

		/// <summary>
		/// 時間の倍率 : ゲームの進み方へ掛ける(1 で等倍、0 で止める)
		/// </summary>
		/// <remarks>
		/// 反映は次の BeginFrame から。負の値は 0 として扱う
		/// </remarks>
		void SetTimeScale(float a_scale) { m_timeScale = (std::max)(a_scale, 0.0f); }
		float GetTimeScale() const { return m_timeScale; }

		/// <summary>
		/// 1フレームだけ等倍で進める(コマ送り)
		/// </summary>
		/// <remarks>
		/// 倍率に関係なく、次の BeginFrame の1フレームだけ倍率を掛けないデルタタイムになる
		/// </remarks>
		void ReserveFrameStep() { m_isFrameStepReserved = true; }

		/// <summary>
		/// 現在のFPSを取得
		/// </summary>
		UINT GetNowFPS() const;

	private:

		std::unique_ptr<FPSController> m_upFPSController = nullptr;
		std::unique_ptr<DeltaTime> m_upDeltaTime = nullptr;

		// 時間の倍率と、それを掛けたこのフレームのデルタタイム
		float m_timeScale = 1.0f;
		float m_scaledDeltaTime = 0.0f;

		// 次の BeginFrame で1フレームだけ等倍で進めるか
		bool m_isFrameStepReserved = false;
	};
}