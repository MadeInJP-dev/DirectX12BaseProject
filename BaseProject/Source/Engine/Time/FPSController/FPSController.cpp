#include "FPSController.h"

#include "Engine/JobSystem/Profile/ThreadProfiler.h"

namespace Engine::Time
{
	void FPSController::BeginFrame()
	{
		m_frameStart = std::chrono::steady_clock::now();
	}

	void FPSController::EndFrame(EFPSRate a_fpsRate)
	{
		// 制限なし・垂直同期(待つのは Present)以外のときだけFPSを制御する
		if (a_fpsRate != EFPSRate::Unlimited && a_fpsRate != EFPSRate::VSync)
		{
			const auto _target = std::chrono::duration<double>(1.0 / static_cast<double>(static_cast<UINT>(a_fpsRate)));

			// ターゲット時間までスリープ : 止まっている時間として数える
			Thread::ThreadStateScope _idleScope(Thread::EThreadState::Idle);
			while (true)
			{
				auto _elapsed = std::chrono::steady_clock::now() - m_frameStart;
				if (_elapsed >= _target) break;
				if (_target - _elapsed > std::chrono::milliseconds(2))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				else
				{
					_mm_pause();
				}
			}
		}

		// 現在のFPSを測定
		FPSMonitor();
	}

	void FPSController::FPSMonitor()
	{
		constexpr auto REFRESH = std::chrono::milliseconds(500);

		m_frameCount++;

		auto _now = std::chrono::steady_clock::now();
		auto _elapsed = _now - m_countFrameStart;

		if (_elapsed >= REFRESH)
		{
			double _seconds = std::chrono::duration<double>(_elapsed).count();

			m_nowFPS = static_cast<UINT>(m_frameCount / _seconds);

			m_countFrameStart = _now;
			m_frameCount = 0;
		}
	}

}