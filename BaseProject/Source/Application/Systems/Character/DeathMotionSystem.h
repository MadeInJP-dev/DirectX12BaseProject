#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// 死亡モーション(DeathMotionComponent)を動かすシステム。
	// 死んだら爆発して、黒くなりながら回って落ち、地面で爆発して消える
	class DeathMotionSystem : public App::ECS::APPISystem
	{
	public:
		void Init(App::ECS::APPWorld& a_world) override;
	};
}
