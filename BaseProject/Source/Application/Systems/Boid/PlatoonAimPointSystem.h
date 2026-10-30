#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// ボイドボスの小隊長の狙われる点(AimPointComponent)を、率いる群れの中心に近いボイドへ合わせる
	class PlatoonAimPointSystem : public App::ECS::APPISystem
	{
	public:
		void Init(App::ECS::APPWorld& a_world) override;
	};
}
