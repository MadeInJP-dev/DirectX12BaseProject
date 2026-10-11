#pragma once
#include "Engine/ECS/World/World.h"

namespace App::Component
{
	//==========================================================================================
	// 追従先(カメラのプレイヤー、ボイドの小隊長など)
	//
	// エンティティIDだけで持ち、保存しない。追従先は出した側が出したときに入れる
	// (プレイヤーは PlayerSpawner、ボイドは SwarmBossController)。
	// プレイヤーは GUID を持たないので、GUID で保存して引き直す形にはできない。
	//==========================================================================================
	struct FollowTargetComponent
	{
		Engine::ECS::Entity target = Engine::ECS::Limits::INVALID_ENTITY;
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::FollowTargetComponent>
{
	// 追従先は実行中に入れるものなので保存しない
	static void Archive(Engine::Persistence::Archive& /*a_ar*/, void* /*a_pData*/) {}

	static void Edit(CompEditContext& a_context) {
		App::Component::FollowTargetComponent& _comp = Engine::EditorField::RefValue<App::Component::FollowTargetComponent>(a_context.pData);

		Engine::EditorField::Field("TargetEntity", _comp.target);
		Engine::EditorField::Tooltip("実行中に出した側が入れる(保存しない)");
	}
};