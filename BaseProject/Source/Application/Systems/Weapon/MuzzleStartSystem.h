#pragma once
#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	//==========================================================================================
	// MuzzleStartSystem
	//
	// 武器が出てきたとき(Start)に、銃口ノードの名前ハッシュからモデルのノード番号を引く。(旧 GunStateStartSystem)
	//
	// マズルフラッシュの再生枠は、以前はここで足していた。Start の最中に足した枠は
	// エフェクトの解決(PostDeserialize)を通らないので、今は銃口の必須コンポーネントとして最初から付け、
	// 中身は MuzzleEffectFixupSystem が書く
	//==========================================================================================
	class MuzzleStartSystem : public App::ECS::APPISystem
	{
	public:
		void Init(App::ECS::APPWorld& a_world) override;
	};
}
