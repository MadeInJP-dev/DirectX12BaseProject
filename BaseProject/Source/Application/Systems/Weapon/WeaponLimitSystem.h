#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	//==========================================================================================
	// WeaponLimitSystem
	//
	// 武器が「撃てなくなる条件」の時間の進行を受け持つ。
	//   WeaponOverHeatSystem       : 熱を冷まし、復帰しきい値まで下がったらオーバーヒートを解く(Job)
	//   WeaponReloadSystem         : リロードを進め、弾倉が空ならリロードに入る(Job)
	//   WeaponOverHeatEffectSystem : オーバーヒートした瞬間のエフェクトを武器の位置へ出す
	//
	// 熱を溜める・弾を減らすのは撃った瞬間の撃ち方のシステム(WeaponFireModeSystem)。
	// こちらはその前に走り、今フレーム撃てるかを決める材料を整えておく。
	//==========================================================================================
	class WeaponLimitSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
