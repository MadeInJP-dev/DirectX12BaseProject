#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// クリップで組んだポーズの上から、決まった構えを足す(StancePoseComponent)
	class StancePoseSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
