#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// 死亡モーション(DeathMotionComponent)へ移るボイドを、群れから外すシステム
	class BoidDeathLeaveSystem : public App::ECS::APPISystem
	{
	public:
		void Init(App::ECS::APPWorld& a_world) override;
	};
}
