#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// 当たった弾(CollisionEvent)のうち ExplosionDamageComponent を持つものが、
	// 当たった位置を中心に範囲ダメージをヒットとして積むシステム。
	class ExplosionDamageSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
