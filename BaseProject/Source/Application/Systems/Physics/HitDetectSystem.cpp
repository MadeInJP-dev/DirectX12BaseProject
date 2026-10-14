#include "HitDetectSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/ECS/Component/CollisionEvent.h"

#include "Engine/MainEngine.h"
#include "Engine/Physics/PhysicsWorld.h"

#include "Application/Components/Physics/SphereCollider.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Weapon/Projectile/ProjectileComponent.h"
#include "Application/InstanceResource/HitEventResource.h"

namespace App::System
{
	void HitDetectSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<const Component::SphereColliderComponent, Engine::ECS::CollisionEvent, const Component::LocalTransformComponent, const Component::ColliderComponent>(
			Engine::ECS::ESystemType::Physics,
			"HitDetectSystem",
			[]
			(
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				const Component::SphereColliderComponent* a_sphereArray,
				Engine::ECS::CollisionEvent* a_eventArray,
				const Component::LocalTransformComponent* a_transArray,
				const Component::ColliderComponent* a_collArray
				)
			{
				ENGINE_PROFILE_SCOPE("Physics_HitDetect");
				const auto& _physicsWorld = a_ctx.pWorld->GetResource<Engine::Physics::PhysicsWorld>();

				// ワールド側のヒット履歴(反応系が横から読む)
				InstanceResource::HitEventResource* _pHitEvents = nullptr;
				if (a_ctx.pWorld->HasResource<InstanceResource::HitEventResource>())
				{
					_pHitEvents = &a_ctx.pWorld->RefResource<InstanceResource::HitEventResource>();
				}

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::SphereColliderComponent& _sphere = a_sphereArray[_i];
					const Component::LocalTransformComponent& _trans = a_transArray[_i];

					// 当たりに行くレイヤー。
					// 弾は発射のたびに ProjectileSpawn が撃った側で入れ替えているので、
					// 自分側の弾のレイヤーはここに入っていない(自分の弾同士がすり抜ける)
					const uint32_t _layerMask = static_cast<uint32_t>(a_collArray[_i].collideLayer);

					Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					// 投射物なら、与えるダメージと発射元を拾っておく。
					// 銃口は撃った本人の体の中にあるので、除外しないと発射した瞬間に
					// 自分へ当たって消える。ProjectileComponent は弾しか持たないので、
					// クエリには含めず持っている時だけ引く。
					Engine::ECS::Entity _shooter = Engine::ECS::Limits::INVALID_ENTITY;
					float               _damage  = 0.0f;
					Component::ProjectileComponent* _pProjectile = nullptr;
					if (a_ctx.pWorld->HasComponent<Component::ProjectileComponent>(_self))
					{
						_pProjectile = a_ctx.pWorld->RefData<Component::ProjectileComponent>(_self);
						if (_pProjectile)
						{
							_shooter = _pProjectile->shooterEntity;
							_damage  = _pProjectile->damage;
						}
					}

					const Math::Vector3 _nowPos = Math::Vector3(_trans.pos) + Math::Vector3(_sphere.offset);

					//==========================================================
					// 判定
					//----------------------------------------------------------
					// 弾のように速いものは「今いる場所の球」だけで見てはいけない。
					// 1フレームの移動量が球の直径を超えると、その間はどこも判定されず
					// 相手を飛び越してしまう(弾速100m/s・60fpsなら1フレーム約1.7m進むのに
					// 判定球は直径0.3m。間の約1.4mが素通りになる)。
					// フレームレートが落ちるほど移動量が伸びるので、重い場面ほどよく抜ける。
					//
					// そこで前フレームの位置から今の位置まで球を掃き、進む向きでいちばん手前の相手を取る。
					// 動いていない/移動量が小さいものは今の球で、いちばん深く重なった相手を取る。
					//==========================================================
					Engine::Physics::ShapeHit _hit = {};
					bool _isHit = false;

					bool _isSwept = false;
					if (_pProjectile && _pProjectile->hasPrevPos)
					{
						const Math::Vector3 _prevPos = Math::Vector3(_pProjectile->prevPos);
						const Math::Vector3 _move    = _nowPos - _prevPos;

						// 球の半径ぶんも動いていないなら、球のままで取りこぼさない
						if (_move.LengthSquared() > (_sphere.radius * _sphere.radius))
						{
							_isHit   = _physicsWorld.SweepSphere(_prevPos, _nowPos, _sphere.radius,
								_layerMask, _self, _shooter, _hit);
							_isSwept = true;
						}
					}

					if (!_isSwept)
					{
						// 自分の球で重なりクエリ(自分自身と発射元は除外)
						_isHit = _physicsWorld.OverlapSphere(_nowPos, _sphere.radius,
							_layerMask, _self, _shooter, _hit);
					}

					// 次フレームの判定の起点にする。当たったかどうかに関わらず更新する
					if (_pProjectile)
					{
						_pProjectile->prevPos    = _nowPos;
						_pProjectile->hasPrevPos = true;
					}

					if (!_isHit) continue;

					// 自分側に記録(弾が hitPos で反応/消滅するため)。
					// hitDir は相手の表面の法線(こちらを向く)
					a_eventArray[_i].other  = _hit.entity;
					a_eventArray[_i].hitPos = _hit.position;
					a_eventArray[_i].hitDir = _hit.normal;

					// 当たった相手側にも直接書き込む(相手が CollisionEvent を持っていれば)。
					// 値の書き換えのみ＝構造変化なしなので反復中でも安全。
					if (_hit.entity != Engine::ECS::Limits::INVALID_ENTITY &&
						a_ctx.pWorld->HasComponent<Engine::ECS::CollisionEvent>(_hit.entity))
					{
						auto* _ev = a_ctx.pWorld->RefData<Engine::ECS::CollisionEvent>(_hit.entity);
						if (_ev)
						{
							_ev->other  = _self;
							_ev->hitPos = _hit.position;
							// 相手から見た方向は逆
							_ev->hitDir = -_hit.normal;
						}
					}

					// ワールドのヒットイベントにも1件積む。
					// CollisionEvent は1エンティティ1件しか持てず、消えた弾の分も残らないので、
					// エフェクト生成やのけぞりなどの反応系はこちらを読む。
					if (_pHitEvents && _hit.entity != Engine::ECS::Limits::INVALID_ENTITY)
					{
						InstanceResource::HitEvent _event = {};
						_event.attacker = _self;
						// 弾を撃った本体。ヒットマーカーのように「自分の弾が当たったか」を
						// 見たい側は、弾(attacker)ではなくこちらで判定する
						_event.shooter  = _shooter;
						_event.victim   = _hit.entity;
						_event.hitPos   = _hit.position;
						// 受けた側の体力を削る量(HealthSystem が読む)。弾以外は 0 のまま
						_event.damage   = _damage;
						// 受けた側から見た方向にそろえる(のけぞりの向きに使う)
						_event.hitDir   = -_hit.normal;
						// このタスクを通るのは球コライダー＋CollisionEvent を持つ弾だけなので Bullet 固定。
						// 近接など別経路が増えたら、産む側で種別を指定すること。
						_event.type     = InstanceResource::EHitEventType::Bullet;

						_pHitEvents->Push(_event);
					}
				}
			}
		)
		// 物理空間へ重なり・掃引クエリを撃つ
		.ReadsResource<Engine::Physics::PhysicsWorld>()
		// 絞り込みに使わない書き込み : 弾の前フレーム位置と、当たった相手の CollisionEvent・ヒットの記録
		.Writes<Component::ProjectileComponent, Engine::ECS::CollisionEvent>()
		.WritesResource<InstanceResource::HitEventResource>();
	}
}
