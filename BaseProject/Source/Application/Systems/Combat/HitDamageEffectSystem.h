#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// 被弾(体力が減った)を見て画面効果の強さを動かし、DamageEffectPass へ送るシステム
	class HitDamageEffectSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
