#pragma once
#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	//==========================================================================================
	// MuzzleStartSystem
	//
	// 武器が出てきたとき(Start)に、銃口(MuzzleComponent)の準備をする。(旧 GunStateStartSystem)
	//   ・銃口ノードの名前ハッシュから、モデルのノード番号を引く
	//   ・マズルフラッシュの再生枠(EffectAssetComponent)を武器自身へ持たせる
	//==========================================================================================
	class MuzzleStartSystem : public App::ECS::APPISystem
	{
	public:
		void Init(App::ECS::APPWorld& a_world) override;
	};
}
