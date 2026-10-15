#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// 地面の近くを速く動く機体(GroundDustEmitterComponent)から真下へレイを打ち、
	// 地表へ砂埃を炊くシステム。ワームの体が炊く砂埃(BoidGroundEffectSystem)と同じ作り
	class GroundDustEmitterSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
