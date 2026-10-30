#include "ExplosionDamageSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/ECS/Component/CollisionEvent.h"
#include "Engine/Physics/PhysicsWorld.h"

#include "Application/Components/Combat/ExplosionDamageComponent.h"
#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Components/Weapon/Projectile/ProjectileComponent.h"
#include "Application/InstanceResource/HitEventResource.h"

namespace App::System
{
	//==============================================================================
	// ExplosionDamageSystem
	//
	// ExplosionDamageComponent を持つもの(ミサイルなど)が何かに当たった瞬間に、
	// 当たった位置を中心とした球に入っている体力持ちへ、爆風のヒットを1件ずつ積む。
	// 体力を減らすのは HealthSystem、当たった演出(OnHit)は EffectEventSystem で、
	// 弾が直接当たったときと同じ入口(HitEventResource)を通す。
	//
	// ・直撃のヒットは HitDetectSystem が積まない。直撃の相手も爆風の球に入るので、
	//   ここで1回だけ食らう(直撃と爆風で二重に削らない)。
	// ・巻き込む相手は弾の当たりに行くレイヤーと同じ。撃った本人と自分は外す。
	// ・自分を消す・死亡エフェクトを出すのは今までどおり ExplodeOnHitSystem。
	// ・PostUpdate 帯。当たりは Physics 帯の HitDetectSystem が書き、
	//   体力を減らす HealthSystem はこの後に読む。
	//==============================================================================
	void ExplosionDamageSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<const Engine::ECS::CollisionEvent, Component::ExplosionDamageComponent, const Component::ColliderComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"ExplosionDamageSystem",
			[]
			(
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				const Engine::ECS::CollisionEvent* a_eventArray,
				Component::ExplosionDamageComponent* a_explosionArray,
				const Component::ColliderComponent* a_collArray
				)
			{
				if (!a_ctx.pWorld->HasResource<InstanceResource::HitEventResource>()) return;
				InstanceResource::HitEventResource& _hitEvents = a_ctx.pWorld->RefResource<InstanceResource::HitEventResource>();
				const auto& _physicsWorld = a_ctx.pWorld->GetResource<Engine::Physics::PhysicsWorld>();

				// 巻き込んだ相手(使い回す)
				std::vector<Engine::Physics::ShapeHit> _hitVec;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Engine::ECS::CollisionEvent& _event = a_eventArray[_i];
					Component::ExplosionDamageComponent& _explosion = a_explosionArray[_i];

					// 未ヒット、または爆発済み
					if (_event.other == Engine::ECS::Limits::INVALID_ENTITY) continue;
					if (_explosion.isExploded) continue;
					_explosion.isExploded = true;

					if (!(_explosion.radius > 0.0f) || !(_explosion.damage > 0.0f)) continue;

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					// 撃った本体(弾でなければ居ない)
					Engine::ECS::Entity _shooter = Engine::ECS::Limits::INVALID_ENTITY;
					if (a_ctx.pWorld->HasComponent<Component::ProjectileComponent>(_self))
					{
						if (const auto* _pProjectile = a_ctx.pWorld->RefData<Component::ProjectileComponent>(_self))
						{
							_shooter = _pProjectile->shooterEntity;
						}
					}

					const Math::Vector3 _center = Math::Vector3(_event.hitPos);
					const uint32_t _layerMask = static_cast<uint32_t>(a_collArray[_i].collideLayer);

					_hitVec.clear();
					if (!_physicsWorld.OverlapSphereAll(_center, _explosion.radius, _layerMask, _self, _shooter, _hitVec)) continue;

					for (const Engine::Physics::ShapeHit& _hit : _hitVec)
					{
						if (_hit.entity == Engine::ECS::Limits::INVALID_ENTITY) continue;

						// 削れるものだけ(地形や弾は巻き込んでも何も起きない)
						if (!a_ctx.pWorld->HasComponent<Component::HealthComponent>(_hit.entity)) continue;

						// 受けた側から見た方向 = 爆心から相手へ。重なっていて決まらなければ面の法線
						Math::Vector3 _dir = Math::Vector3(_hit.position) - _center;
						if (_dir.LengthSquared() > 1e-8f) _dir.Normalize();
						else                               _dir = -Math::Vector3(_hit.normal);

						InstanceResource::HitEvent _hitEvent = {};
						_hitEvent.attacker = _self;
						_hitEvent.shooter  = _shooter;
						_hitEvent.victim   = _hit.entity;
						_hitEvent.hitPos   = _hit.position;
						_hitEvent.hitDir   = _dir;
						_hitEvent.damage   = _explosion.damage;
						_hitEvent.type     = InstanceResource::EHitEventType::Explosion;

						_hitEvents.Push(_hitEvent);
					}
				}
			}
		)
		// 絞り込みに使わない読み : 撃った本体(ProjectileComponent)
		.Reads<Component::ProjectileComponent>()
		// 物理空間へ重なりクエリを撃つ
		.ReadsResource<Engine::Physics::PhysicsWorld>()
		.WritesResource<InstanceResource::HitEventResource>();
	}
}
