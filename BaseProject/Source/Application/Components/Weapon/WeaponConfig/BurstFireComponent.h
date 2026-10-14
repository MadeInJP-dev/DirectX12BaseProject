#pragma once

#include "Application/Components/Weapon/WeaponTriggerComponent.h"
#include "Application/Components/Weapon/WeaponFireComponent.h"
#include "MuzzleComponent.h"

namespace App::Component
{
	//================================================================================
	// バースト : 引き金を引くと burstNum 発を burstTime 間隔で撃ち、
	//            撃ち切ってから waitTime あけて次のバーストへ
	//
	// 一度始まったら引き金を離しても撃ち切る。
	// リロード・オーバーヒートどちらの場合も、バースト数に達する前に上限が来たら途中で打ち切る。
	// 判定は BurstFireSystem。
	//================================================================================
	struct BurstFireComponent
	{
		uint32_t burstNum = 3;			// 1バーストで撃つ数
		float burstTime = 0.1f;			// バースト内の間隔(秒)
		float waitTime = 1.0f;			// バーストを撃ち切ってから次のバーストまでの間隔(秒)

		// ---- ランタイム(保存しない) ----
		struct Runtime
		{
			float timeSinceShot = 0.0f;	// 前回撃ってからの経過時間(秒)
			uint32_t remain = 0;		// 今のバーストで残っている発数
		};
		Runtime runtimeData = {};
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::BurstFireComponent>
{
	using Requires = Engine::ECS::RequireComponents<
		App::Component::WeaponTriggerComponent, App::Component::WeaponFireComponent, App::Component::MuzzleComponent>;

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::BurstFireComponent& _comp = Engine::EditorField::RefValue<App::Component::BurstFireComponent>(a_pData);
		a_ar.Field("burstNum", _comp.burstNum);
		a_ar.Field("burstTime", _comp.burstTime);
		a_ar.Field("waitTime", _comp.waitTime);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::BurstFireComponent& _comp = Engine::EditorField::RefValue<App::Component::BurstFireComponent>(a_context.pData);

		int _burstNum = static_cast<int>(_comp.burstNum);
		if (Engine::EditorField::Field("Burst Num", _burstNum, 1.0f, 1, 100))
		{
			_comp.burstNum = static_cast<uint32_t>((std::max)(_burstNum, 1));
		}
		Engine::EditorField::Field("Burst Time", _comp.burstTime, 0.005f, 0.0f, 10.0f, "%.3f s");
		Engine::EditorField::Tooltip("バースト内の1発ごとの間隔");
		Engine::EditorField::Field("Wait Time", _comp.waitTime, 0.05f, 0.0f, 60.0f, "%.2f s");
		Engine::EditorField::Tooltip("撃ち切ってから次のバーストを撃てるまで");

		Engine::EditorField::HelpText("TimeSinceShot : %.2f s  Remain : %u", _comp.runtimeData.timeSinceShot, _comp.runtimeData.remain);
	}
};
