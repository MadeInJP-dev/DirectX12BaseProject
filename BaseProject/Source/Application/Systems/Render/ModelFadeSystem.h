#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	// メインカメラ(TPS)の依頼から、モデルのフェード係数(ModelComponent::fade)を寄せるシステム。
	// 依頼を作るのは TPSSystem(手前の障害物・カメラに近い自機)。依頼の無いモデルは 1 へ戻す。
	// 実際に画素を捨てるのは GBuffer / ZPre のピクセルシェーダー(アルファディザー)
	class ModelFadeSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
