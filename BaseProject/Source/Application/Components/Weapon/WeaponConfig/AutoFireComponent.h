#pragma once

#include "Application/Components/Weapon/WeaponTriggerComponent.h"
#include "Application/Components/Weapon/WeaponFireComponent.h"
#include "MuzzleComponent.h"

namespace App::Component
{
	//================================================================================
	// 連射 : 引き金を引いている間、fireRate の間隔で撃ち続ける
	//
	// 撃ち方は Auto / Burst / SemiAuto のどれか1つを持たせる(2つ以上持たせない)。
	// 判定は AutoFireSystem。熱(WeaponOverHeatComponent)・弾倉(WeaponReloadComponent)を
	// 持っていれば、撃てない間は引き金に反応しない。
	//================================================================================
	struct AutoFireComponent
	{
		float fireRate = 10.0f;			// 1秒あたりの発射数

		// ---- ランタイム(保存しない) ----
		struct Runtime
		{
			// 前回撃ってからの経過時間(秒)。撃っていない間も溜まり続けるので、
			// 久しぶりに引き金を引いたら即撃てる
			float timeSinceShot = 0.0f;
		};
		Runtime runtimeData = {};
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::AutoFireComponent>
{
	// 引き金の受け口・発射の結果・銃口は撃ち方があれば必ず要るので自動で付ける
	using Requires = Engine::ECS::RequireComponents<
		App::Component::WeaponTriggerComponent, App::Component::WeaponFireComponent, App::Component::MuzzleComponent>;

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::AutoFireComponent& _comp = Engine::EditorField::RefValue<App::Component::AutoFireComponent>(a_pData);
		a_ar.Field("fireRate", _comp.fireRate);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::AutoFireComponent& _comp = Engine::EditorField::RefValue<App::Component::AutoFireComponent>(a_context.pData);

		if (Engine::EditorField::Field("Fire Rate", _comp.fireRate, 0.1f, 0.01f, 1000.0f, "%.2f /s"))
		{
			if (_comp.fireRate < 0.01f) _comp.fireRate = 0.01f;
		}

		Engine::EditorField::HelpText("TimeSinceShot : %.2f s", _comp.runtimeData.timeSinceShot);
	}
};
