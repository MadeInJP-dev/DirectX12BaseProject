#include "InputAxisBase.h"

Core::Math::Vector2 Engine::Input::InputAxisBase::GetState() const
{
	Math::Vector2 _retAxis = m_axis * m_valueRate;

	_retAxis.x = std::clamp(_retAxis.x, -m_limitValue, m_limitValue);
	_retAxis.y = std::clamp(_retAxis.y, -m_limitValue, m_limitValue);

	// 1秒あたりの量 → このフレームの量
	if (m_isPerSecond) _retAxis *= m_deltaTime;

	return _retAxis;
}
