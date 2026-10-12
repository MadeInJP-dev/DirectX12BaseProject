#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// 関節をゆっくり揺らすシステム(JointSwayComponent)。
	// 向きの中心(restQuat)のまわりを、軸まわりに sin で行き来させて LocalTransform の回転へ書く。
	// 行列は後ろの CalcMatrixSystem(PostUpdate)が組み直す。
	class JointSwaySystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
