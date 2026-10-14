#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// 撃った手持ちの武器から、WeaponStateComponent の弾を出すシステム(旧 GunProjectileSpawnSystem)
	class WeaponProjectileSpawnSystem : public App::ECS::APPISystem
	{
	public:
		void Init(App::ECS::APPWorld& a_world) override;
	};
}
