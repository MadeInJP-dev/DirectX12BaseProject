#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	//==========================================================================================
	// WeaponFireModeSystem
	//
	// 引き金(WeaponTriggerComponent)を受けて、このフレームに撃つかを撃ち方ごとに決め、
	// WeaponFireComponent::isFired へ書く。弾は出さない。(旧 GunTriggerSystem)
	//   AutoFireSystem     : 押している間、間隔ごとに撃つ
	//   BurstFireSystem    : 引くと決まった数を撃ち切る
	//   SemiAutoFireSystem : 引いた瞬間に1発
	//
	// ・熱(WeaponOverHeatComponent)・弾倉(WeaponReloadComponent)を持っていれば、
	//   撃てない間は反応せず、撃った瞬間に熱を溜めて弾を減らす。上限に届いたらバーストも打ち切る。
	// ・書くのは自分の武器の値だけなのでチャンクを分けてワーカーで回す。
	// ・銃口の位置と向きは WeaponMuzzleSystem、弾の生成は WeaponProjectileSpawnSystem。
	//==========================================================================================
	class WeaponFireModeSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
