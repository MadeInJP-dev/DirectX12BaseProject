#include "TimeManager.h"

#include "FPSController/FPSController.h"
#include "DeltaTime/DeltaTime.h"
namespace Engine::Time
{
	TimeManager::TimeManager()
	{}

	TimeManager::~TimeManager()
	{}

	void TimeManager::Init()
	{
		timeBeginPeriod(1);		// Windowsのタイマー精度を1msに設定

		m_upFPSController = std::make_unique<FPSController>();

		m_upDeltaTime = std::make_unique<DeltaTime>();
	}

	void TimeManager::Release()
	{
		timeEndPeriod(1);		// Windowsのタイマー精度を元に戻す
	}

	void TimeManager::BeginFrame()
	{
		m_upFPSController->BeginFrame();
		m_upDeltaTime->CalcDeltaTime();

		// 倍率を掛ける。コマ送りを頼まれていれば、この1フレームだけ等倍で進める
		const float _unscaled = m_upDeltaTime->Get();
		if (m_isFrameStepReserved)
		{
			m_isFrameStepReserved = false;
			m_scaledDeltaTime = _unscaled;
		}
		else
		{
			m_scaledDeltaTime = _unscaled * m_timeScale;
		}
	}

	void TimeManager::EndFrame(EFPSRate a_fpsRate)
	{
		m_upFPSController->EndFrame(a_fpsRate);
	}

	float TimeManager::GetUnscaledDeltaTime() const
	{
		return m_upDeltaTime->Get();
	}

	UINT TimeManager::GetNowFPS() const
	{
		return m_upFPSController->GetNowFPS();
	}
}