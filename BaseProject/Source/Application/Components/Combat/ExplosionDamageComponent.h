#pragma once

#include "Engine/EditorField/EditorField.h"

namespace App::Component
{
	//================================================================================
	// 爆発したときの範囲ダメージ(ミサイルなど)
	//
	// 当たった瞬間(CollisionEvent)に、当たった位置を中心とした半径 radius の球に
	// 入っている体力持ちすべてへ damage を与える。与えるのは ExplosionDamageSystem。
	//
	// ・これを持つ弾は、当たった相手へ直接ダメージを与えない(HitDetectSystem がヒットを積まない)。
	//   直撃でも爆風に巻き込まれるのと同じ扱いになり、地面に当たっても近くの相手は食らう。
	// ・威力はここが持つ。撃った武器の威力(MissileState / WeaponState)はこの弾には効かない。
	// ・巻き込む相手は弾の当たりに行くレイヤー(ColliderComponent::collideLayer)と同じ。
	//   発射のたびに撃った側で入れ替わるので、味方の爆風で自分側は削れない。撃った本人も外す。
	//================================================================================
	struct ExplosionDamageComponent
	{
		float radius = 3.0f;		// 爆風の半径(m)
		float damage = 30.0f;		// 範囲に入った相手へ与えるダメージ

		// 爆発したか(ランタイム。保存しない)。消えずに残る弾でも1回しか爆発させない
		bool isExploded = false;
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::ExplosionDamageComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::ExplosionDamageComponent& _comp = Engine::EditorField::RefValue<App::Component::ExplosionDamageComponent>(a_pData);
		a_ar.Field("radius", _comp.radius);
		a_ar.Field("damage", _comp.damage);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::ExplosionDamageComponent& _comp = Engine::EditorField::RefValue<App::Component::ExplosionDamageComponent>(a_context.pData);

		Engine::EditorField::Field("Radius", _comp.radius, 0.05f, 0.0f, 100.0f, "%.2f m");
		Engine::EditorField::Field("Damage", _comp.damage, 0.5f, 0.0f);
		Engine::EditorField::Tooltip("爆風に入った体力持ちへ与える。直撃のダメージは無い");
	}
};
