#include "BallisticSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Effect/BallisticComponent.h"
#include "Application/Components/Effect/EffectAssetComponent.h"
#include "Application/Components/Effect/EffectPlayRequestComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Core/LifeTimeComponent.h"
#include "Application/Components/Physics/Collider.h"

#include "Engine/Physics/PhysicsWorld.h"

namespace App::System
{
	//==============================================================================
	// BallisticSystem
	//
	// BallisticComponent を持つもの(飛び散る岩の破片など)を放物線で飛ばし、
	// 地面に当たったら跳ね返して、勢いが尽きたら止める。
	//
	//   速度 += 重力(シーンの設定) × dt、位置 += 速度 × dt
	//   進む向きへ「移動量 + 半径」のレイを打ち、地面(上向きの面)に当たったら
	//     面に沿う速さ × (1 - friction)、面に向かう速さ × -restitution
	//   跳ね返る速さが restSpeed を下回ったら、その場で止める
	//
	// ・地面として見るのは StaticObject だけ。当たり判定のボディは持たないので、
	//   プレイヤーや他の破片には当たらない(見た目だけの小物のため)。
	// ・面から出ていく向きに動いているときは当たりを見ない。
	//   地面すれすれから上へ撒かれた瞬間に、足元の地面(の裏)を拾って止まらないようにするため。
	// ・止まったら、同じエンティティの軌跡のエフェクトを止める(isStopEffectOnRest)。
	// ・寿命(LifeTimeComponent)の残りが shrinkTime を切ったら縮めていく。
	//==============================================================================
	namespace
	{
		// これより寝ている面は地面として扱わない(壁に当たったら跳ね返すだけで止めない)
		constexpr float GROUND_NORMAL_Y = 0.3f;
	}

	void BallisticSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<Component::BallisticComponent, Component::LocalTransformComponent>(
			Engine::ECS::ESystemType::Update,
			"BallisticSystem",
			[](
				Engine::ECS::Chunk*               a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				Component::BallisticComponent*               a_ballisticArray,
				Component::LocalTransformComponent*          a_transArray
			)
			{
				if (a_count == 0) return;

				auto& _world = *a_ctx.pWorld;
				const auto& _physicsWorld = _world.GetResource<Engine::Physics::PhysicsWorld>();
				const float _dt = a_ctx.dt;
				const uint32_t _queryMask = static_cast<uint32_t>(Component::ECollisionLayer::StaticObject);
				// 重力はシーンの設定(ScenePhysics)が物理空間へ流し込んだもの
				const Math::Vector3 _gravity = _physicsWorld.GetGravity();

				// 寿命とエフェクトを持つかはチャンク(同じ組み合わせ)で揃っているので、先頭で1回だけ見る
				const Engine::ECS::Entity _first = a_pChunk->entityData[0];
				const bool _hasLifeTime = _world.HasComponent<Component::LifeTimeComponent>(_first);
				const bool _hasEffect   = _world.HasComponent<Component::EffectAssetComponent>(_first);

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::BallisticComponent& _ballistic = a_ballisticArray[_i];
					Component::LocalTransformComponent& _trans = a_transArray[_i];
					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					if (!_ballistic.hasBaseScale)
					{
						_ballistic.baseScale    = _trans.scale;
						_ballistic.hasBaseScale = true;
					}

					//------------------------------------------------------
					// 飛ぶ
					//------------------------------------------------------
					if (!_ballistic.isResting && _dt > 0.0f)
					{
						_ballistic.velocity += _gravity * _ballistic.gravityScale * _dt;

						const Math::Vector3 _prevPos = _trans.pos;
						const Math::Vector3 _move    = _ballistic.velocity * _dt;
						const float _moveLen = _move.Length();

						bool _isBounced = false;
						if (_moveLen > 1e-5f)
						{
							Math::Ray _ray;
							_ray.origin      = _prevPos;
							_ray.direction   = _move / _moveLen;
							_ray.maxDistance = _moveLen + _ballistic.groundOffset;

							Engine::Physics::RayHit _hit = {};
							if (_physicsWorld.CastRay(_ray, _queryMask, _self, _hit))
							{
								const Math::Vector3 _normal = _hit.normal;
								const float _intoSpeed = _ballistic.velocity.Dot(_normal);

								// 面に向かって進んでいるときだけ跳ね返す
								if (_intoSpeed < 0.0f)
								{
									const Math::Vector3 _normalVel  = _normal * _intoSpeed;
									const Math::Vector3 _tangentVel = _ballistic.velocity - _normalVel;

									_ballistic.velocity =
										_tangentVel * (1.0f - std::clamp(_ballistic.friction, 0.0f, 1.0f)) -
										_normalVel * std::clamp(_ballistic.restitution, 0.0f, 1.0f);

									_trans.pos = _hit.position + _normal * _ballistic.groundOffset;
									_isBounced = true;

									// 地面(上向きの面)で、もう跳ねる勢いが無ければ止める
									const float _bounceSpeed = -_intoSpeed * _ballistic.restitution;
									if (_normal.y >= GROUND_NORMAL_Y && _bounceSpeed < _ballistic.restSpeed)
									{
										_ballistic.velocity  = Math::Vector3(0.0f, 0.0f, 0.0f);
										_ballistic.isResting = true;
									}
								}
							}
						}

						if (!_isBounced)
						{
							_trans.pos = _prevPos + _move;
						}

						// 回る(止まったら回らない)
						if (!_ballistic.isResting && _ballistic.spinSpeedDeg != 0.0f)
						{
							const Math::Quaternion _spin = Math::Quaternion::CreateFromAxisAngle(
								_ballistic.spinAxis, DirectX::XMConvertToRadians(_ballistic.spinSpeedDeg) * _dt);
							_trans.quat = _spin * _trans.quat;
							_trans.quat.Normalize();
						}

						_trans.isDirty = true;
					}

					//------------------------------------------------------
					// 止まったら軌跡の砂埃を止める
					//------------------------------------------------------
					if (_ballistic.isResting && _ballistic.isStopEffectOnRest && _hasEffect)
					{
						if (auto* _pEffect = _world.RefData<Component::EffectPlayRequestComponent>(_self))
						{
							_pEffect->isPlay = false;
						}
					}

					//------------------------------------------------------
					// 寿命の最後は縮んで消える
					//------------------------------------------------------
					if (_hasLifeTime && _ballistic.shrinkTime > 0.0f)
					{
						if (const auto* _pLife = _world.RefData<Component::LifeTimeComponent>(_self))
						{
							if (_pLife->value >= 0.0f && _pLife->value < _ballistic.shrinkTime)
							{
								_trans.scale   = _ballistic.baseScale * (_pLife->value / _ballistic.shrinkTime);
								_trans.isDirty = true;
							}
						}
					}
				}
			}
		)
		// 物理空間へレイを撃つ
		.ReadsResource<Engine::Physics::PhysicsWorld>()
		// 順序 : 姿勢(LocalTransform)とエフェクト(EffectAsset)の書き手同士の並び
		.After({ "LookAroundSystem", "MuzzleFlashSystem" })
		// 絞り込みに使わない読み書き : 寿命と軌跡のエフェクトは持っているときだけ RefData で触る
		.Reads<Component::LifeTimeComponent>()
		.Writes<Component::EffectPlayRequestComponent>();
	}
}
