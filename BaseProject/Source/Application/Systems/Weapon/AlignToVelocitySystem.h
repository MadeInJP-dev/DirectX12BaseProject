#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// AlignToVelocityComponent を持つもの(細長い弾・ミサイル)の向きを、進む向きへ合わせるシステム。
	class AlignToVelocitySystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
