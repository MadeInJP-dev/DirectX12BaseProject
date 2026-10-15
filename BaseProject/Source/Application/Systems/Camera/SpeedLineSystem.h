#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// カメラの速さ(TPSCameraStateComponent::currentSpeed01)から、
	// カメラに付けたスピードラインのエフェクトへ出す量(params[0])を書くシステム
	class SpeedLineSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
