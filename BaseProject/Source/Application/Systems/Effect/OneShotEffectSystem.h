#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// 単発エフェクトのイベント(OneShotEffectEventResource)を、まとめてエンティティとして出すシステム
	class OneShotEffectSystem : public App::ECS::APPISystem
	{
	public:
		void Init(App::ECS::APPWorld& a_world) override;
	};
}
