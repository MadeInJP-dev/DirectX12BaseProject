#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// RadialBlurShakeResource の依頼(ボスの咆哮など)から、
	// カメラのラジアルブラーへ震え(引きずり量の揺れと中心のずれ)を足すシステム。
	// 速度ぶん(RadialBlurSpeedSystem)とは別に持ち、送るときに CamSetShaderSystem が足し合わせる。
	class RadialBlurShakeSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
