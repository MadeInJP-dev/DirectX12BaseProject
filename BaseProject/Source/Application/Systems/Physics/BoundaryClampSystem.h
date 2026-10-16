#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// プレイヤーを行ける範囲(BoundaryResource)の内側へ押し戻す。
	// 当たり判定は使わず、範囲の比較だけで挟む
	class BoundaryClampSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
