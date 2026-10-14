#pragma once
namespace App::Component
{
	/// <summary>
	/// 投射物が特定の対象を追跡する場合に付与される
	/// </summary>
	struct HomingComponent
	{
		// 追う相手。発射した瞬間に WeaponProjectileSpawnSystem / MissileFireSystem が埋める(保存しない)
		Engine::ECS::Entity targetEntity = Engine::ECS::Limits::INVALID_ENTITY;
		float turnSpeed = 0.0f;				// 旋回スピード : ラジアン/秒。0 以下なら誘導しない
		float searchRange = 0.0f;			// ターゲットを探す最大距離。0 以下なら距離制限なし
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::HomingComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::HomingComponent& _comp = Engine::EditorField::RefValue<App::Component::HomingComponent>(a_pData);
		a_ar.Field("turnSpeed",_comp.turnSpeed);
		a_ar.Field("searchRange",_comp.searchRange);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::HomingComponent& _comp = Engine::EditorField::RefValue<App::Component::HomingComponent>(a_context.pData);
		Engine::EditorField::Field("turnSpeed", _comp.turnSpeed, 0.1f);
		Engine::EditorField::Field("searchRange", _comp.searchRange, 0.1f);

		// 追跡中の相手(表示のみ。発射時に埋まる)
		if (_comp.targetEntity == Engine::ECS::Limits::INVALID_ENTITY)
		{
			Engine::EditorField::HelpText("Target : None");
		}
		else
		{
			Engine::EditorField::HelpText("Target : %llu", static_cast<unsigned long long>(_comp.targetEntity));
		}
	}
};
