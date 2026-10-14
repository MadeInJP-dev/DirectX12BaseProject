#pragma once

#include "Application/Components/Weapon/WeaponTriggerComponent.h"
#include "Application/Components/Weapon/WeaponFireComponent.h"
#include "MuzzleComponent.h"

namespace App::Component
{
	//================================================================================
	// 単発 : 引き金を引いた瞬間に1発。押しっぱなしでは次を撃たない
	//
	// 撃ってから waitTime たつまでは、引き直しても撃たない。
	// 判定は SemiAutoFireSystem。
	//================================================================================
	struct SemiAutoFireComponent
	{
		float waitTime = 0.3f;			// 撃ってから次に撃てるまでの時間(秒)

		// ---- ランタイム(保存しない) ----
		struct Runtime
		{
			float timeSinceShot = 0.0f;	// 前回撃ってからの経過時間(秒)
			bool wasPulled = false;		// 前のフレームに引き金を引いていたか(引いた瞬間の検出用)
		};
		Runtime runtimeData = {};
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::SemiAutoFireComponent>
{
	using Requires = Engine::ECS::RequireComponents<
		App::Component::WeaponTriggerComponent, App::Component::WeaponFireComponent, App::Component::MuzzleComponent>;

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::SemiAutoFireComponent& _comp = Engine::EditorField::RefValue<App::Component::SemiAutoFireComponent>(a_pData);
		a_ar.Field("waitTime", _comp.waitTime);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::SemiAutoFireComponent& _comp = Engine::EditorField::RefValue<App::Component::SemiAutoFireComponent>(a_context.pData);

		Engine::EditorField::Field("Wait Time", _comp.waitTime, 0.01f, 0.0f, 60.0f, "%.2f s");
		Engine::EditorField::HelpText("TimeSinceShot : %.2f s", _comp.runtimeData.timeSinceShot);
	}
};
