#include "DeathMotionSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Application/ECS/Event/EffectEvent/EffectEvent.h"

#include "Application/Components/Core/PhaseTag/PostDeserializeTag.h"

#include "Application/Components/Character/DeathMotionComponent.h"
#include "Application/Components/Character/DeathSmokeTag.h"
#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Render/ModelComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Components/Effect/EffectPlayRequestComponent.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Utility/EffectSpawnHelper.h"

#include "Engine/Physics/PhysicsWorld.h"

namespace App::System
{
	//==============================================================================
	// DeathMotionSystem
	//
	// DeathMotionComponent を持つキャラクターが死んだら(HealthComponent.isDead)、
	//
	//   Alive   → 死んだ瞬間に爆発を積み、煙を子として出して Falling へ
	//   Falling → 黒くしながら回して落とす。進む向きへレイを打ち、地面についたら爆発(地面用があればそちら)を積んで Landed へ
	//   Landed  → 着地した位置と向きに留め、destroyTime たったら解放予約(isDestroyFade なら縮めながら)
	//
	// ・爆発は OneShotEffectEventResource へ積むだけ。出すのは OneShotEffectSystem。
	// ・煙は落下中ずっと付いて来てほしいので、単発エフェクトではなく子のエフェクトとして出す
	//   (DeathSmokeTag)。止めるのは煙の側(DeathSmokeSystem)が親の段を見て行う。
	// ・落とすのは Physics 帯の移動の後。死んでいるあいだ移動入力は DeathStateSystem が消すので、
	//   移動系が動かすのは水平の惰性だけになり、その上から落下を足す形になる。
	// ・重力はシーンの設定(ScenePhysics)。PhysicsWorld::GetGravity で引く。
	//   重力が 0 のシーンでは地面につかないので、maxFallTime でその場に着地した扱いになる。
	// ・位置は LocalTransform をワールドとして扱う(親を持たないキャラクター前提)。
	// ・HealthComponent の releaseDelay では消えない(DeathReleaseSystem が除外している)。
	//==============================================================================
	namespace
	{
		// 重力の逆向きとの内積がこれ以上の面を地面として扱う(これより寝ている面は壁)
		constexpr float GROUND_NORMAL_DOT = 0.3f;

		// rotStrength = 1 のときの回転の速さ(度/秒)
		constexpr float BASE_SPIN_DEG = 180.0f;

		// 回転の速さを個体ごとにばらす幅(±)
		constexpr float SPIN_JITTER = 0.25f;

		// 向きがそろわない回転軸(単位ベクトル)
		Math::Vector3 RandomAxis()
		{
			Math::Vector3 _axis(
				Math::Random::Float(-1.0f, 1.0f),
				Math::Random::Float(-1.0f, 1.0f),
				Math::Random::Float(-1.0f, 1.0f));
			if (_axis.LengthSquared() < 1e-6f) _axis = Math::Vector3(1.0f, 0.0f, 0.0f);
			_axis.Normalize();
			return _axis;
		}

		// 爆発を積む(出すのは OneShotEffectSystem)
		void PushExplosion(Engine::ECS::World& a_world, const Core::GUID& a_guid, float a_scale, const Math::Vector3& a_pos)
		{
			if (a_guid == Core::DEFAULT_GUID) return;

			ECS::OneShotEffectEvent _event = {};
			_event.effectGUID = a_guid;
			_event.worldMat = Math::Matrix::CreateTranslation(a_pos);
			_event.scale = a_scale;

			a_world.RefResource<ECS::OneShotEffectEventResource>().Push(_event);
		}

		// 黒くする : 0 で元の色、1 で真っ黒(アルファと発光の色はそのまま)
		void ApplyBlacken(Component::ModelComponent& a_model, const Component::DeathMotionComponent& a_motion, float a_rate)
		{
			const float _keep = 1.0f - std::clamp(a_rate, 0.0f, 1.0f);

			a_model.colorScale.r = a_motion.baseColor.r * _keep;
			a_model.colorScale.g = a_motion.baseColor.g * _keep;
			a_model.colorScale.b = a_motion.baseColor.b * _keep;
			a_model.emissiveIntensity = a_motion.baseEmissiveIntensity * _keep;
		}

		// 死んだ瞬間 : 爆発して落ち始める
		void BeginFalling(
			Engine::ECS::World& a_world,
			Engine::ECS::Entity a_self,
			Component::DeathMotionComponent& a_motion,
			const Component::LocalTransformComponent& a_trans,
			const Component::ModelComponent& a_model)
		{
			a_motion.phase = Component::EDeathMotionPhase::Falling;
			a_motion.timer = 0.0f;
			a_motion.velocity = Math::Vector3(0.0f, 0.0f, 0.0f);
			a_motion.rotation = a_trans.quat;

			// 回転は軸をランダムに、速さは個体ごとに少しばらす
			a_motion.spinAxis = RandomAxis();
			a_motion.spinSpeedDeg = BASE_SPIN_DEG * a_motion.rotStrength
				* Math::Random::Float(1.0f - SPIN_JITTER, 1.0f + SPIN_JITTER);

			// 黒くする・縮める前の見た目を覚えておく
			a_motion.baseColor = a_model.colorScale;
			a_motion.baseEmissiveIntensity = a_model.emissiveIntensity;
			a_motion.baseScale = a_trans.scale;

			PushExplosion(a_world, a_motion.explosionGUID, a_motion.explosionScale, a_trans.pos);

			// 煙は子として出す(位置だけ付いて行く)。止めるのは DeathSmokeSystem
			if (a_motion.smokeGUID != Core::DEFAULT_GUID)
			{
				App::Utility::ReserveSpawnChildEffect(
					a_world, a_motion.smokeGUID, a_self, a_motion.smokeScale,
					{ a_world.GetCompTypeID<Component::DeathSmokeTag>() });
			}
		}

		// 地面についた : もう一度爆発して止まる。止まる位置は今の本体の位置
		void Land(
			Engine::ECS::World& a_world,
			Component::DeathMotionComponent& a_motion,
			Component::ModelComponent& a_model,
			const Component::LocalTransformComponent& a_trans,
			const Math::Vector3& a_explosionPos)
		{
			a_motion.phase = Component::EDeathMotionPhase::Landed;
			a_motion.timer = 0.0f;
			a_motion.velocity = Math::Vector3(0.0f, 0.0f, 0.0f);
			a_motion.landedPos = a_trans.pos;

			// 黒くなりきる前に落ちきっても、着地した時点で真っ黒にする
			ApplyBlacken(a_model, a_motion, 1.0f);

			// 地面用の爆発があればそれ、無ければ空中と同じ爆発
			if (a_motion.landExplosionGUID != Core::DEFAULT_GUID)
			{
				PushExplosion(a_world, a_motion.landExplosionGUID, a_motion.landExplosionScale, a_explosionPos);
			}
			else
			{
				PushExplosion(a_world, a_motion.explosionGUID, a_motion.explosionScale, a_explosionPos);
			}
		}
	}

	void DeathMotionSystem::Init(App::ECS::APPWorld& a_world)
	{
		//--------------------------------------------------------------------------
		// [PostDeserialize] エフェクトのハンドルを取り、段を生きているところへ戻す
		//
		// プレハブから写した値は持ち主ではないので、必ず取り直す。
		// 死んだ瞬間に読み込みが走って最初の爆発が遅れないよう、ここで読み切って先読みもしておく
		//--------------------------------------------------------------------------
		a_world.PostDeserializeTask<Component::DeathMotionComponent>(
			Engine::ECS::ESystemType::PostDeserialize,
			"DeathMotionFixupSystem",
			[](
				Engine::ECS::Chunk*               /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::PostDeserializeTag*               /*a_tag*/,
				Component::DeathMotionComponent*             a_motionArray
			)
			{
				auto* _pResourceManager = a_ctx.pServices->pResourceManager;
				if (!_pResourceManager) return;

				// 未設定ならハンドルを空に、設定されていれば取り直して先読みする
				auto _acquire = [&](const Core::GUID& a_guid, Engine::Handle<Engine::Resource::EffectAsset>& a_handle)
				{
					if (a_guid == Core::DEFAULT_GUID)
					{
						a_handle = {};
						return;
					}
					_pResourceManager->AcquireImmediate(a_handle, a_guid);
					App::Utility::WarmupEffectParticles(*a_ctx.pServices, a_handle);
				};

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::DeathMotionComponent& _motion = a_motionArray[_i];

					_motion.phase = Component::EDeathMotionPhase::Alive;
					_motion.timer = 0.0f;
					_motion.velocity = Math::Vector3(0.0f, 0.0f, 0.0f);

					_acquire(_motion.explosionGUID, _motion.explosionHandle);
					_acquire(_motion.landExplosionGUID, _motion.landExplosionHandle);
					_acquire(_motion.smokeGUID, _motion.smokeHandle);
				}
			}
		);

		//--------------------------------------------------------------------------
		// [Physics] 死んだら落とし、地面についたら止めて、時間で消す
		//
		// 爆発の積み込み・煙の生成・解放予約をするのでメインスレッドで回す
		//--------------------------------------------------------------------------
		a_world.ActiveTask<const Component::HealthComponent, Component::DeathMotionComponent, Component::LocalTransformComponent, Component::ModelComponent>(
			Engine::ECS::ESystemType::Physics,
			"DeathMotionSystem",
			[](
				Engine::ECS::Chunk*               a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::HealthComponent*            a_healthArray,
				Component::DeathMotionComponent*             a_motionArray,
				Component::LocalTransformComponent*          a_transArray,
				Component::ModelComponent*                   a_modelArray
			)
			{
				auto& _world = *a_ctx.pWorld;
				if (!_world.HasResource<ECS::OneShotEffectEventResource>()) return;

				const auto& _physicsWorld = _world.GetResource<Engine::Physics::PhysicsWorld>();
				const uint32_t _queryMask = static_cast<uint32_t>(Component::ECollisionLayer::StaticObject);
				const float _dt = a_ctx.dt;

				// 重力はシーンの設定(ScenePhysics)が物理空間へ流し込んだもの。
				// 地面かどうかは、重力の逆向き(上)に面が向いているかで見る
				const Math::Vector3 _gravity = _physicsWorld.GetGravity();
				Math::Vector3 _upDir = -_gravity;
				if (_upDir.LengthSquared() > 1e-8f) _upDir.Normalize();

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::DeathMotionComponent&    _motion = a_motionArray[_i];
					Component::LocalTransformComponent& _trans  = a_transArray[_i];
					Component::ModelComponent&          _model  = a_modelArray[_i];
					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					switch (_motion.phase)
					{
					//------------------------------------------------------
					// 生きている : 死んだら落ち始める
					//------------------------------------------------------
					case Component::EDeathMotionPhase::Alive:
					{
						if (!a_healthArray[_i].isDead) break;

						BeginFalling(_world, _self, _motion, _trans, _model);
						if (_motion.blackenTime <= 0.0f) ApplyBlacken(_model, _motion, 1.0f);
						break;
					}

					//------------------------------------------------------
					// 落ちている : 黒くしながら回して落とす
					//------------------------------------------------------
					case Component::EDeathMotionPhase::Falling:
					{
						_motion.timer += _dt;

						const float _blackenRate = (_motion.blackenTime > 0.0f) ? (_motion.timer / _motion.blackenTime) : 1.0f;
						ApplyBlacken(_model, _motion, _blackenRate);

						// 回る : 自分で持っている向きを回して書き込む(向きを書き戻す側に上書きされない)
						if (_motion.spinSpeedDeg != 0.0f)
						{
							const Math::Quaternion _spin = Math::Quaternion::CreateFromAxisAngle(
								_motion.spinAxis, DirectX::XMConvertToRadians(_motion.spinSpeedDeg) * _dt);
							_motion.rotation = _spin * _motion.rotation;
							_motion.rotation.Normalize();
						}
						_trans.quat = _motion.rotation;
						_trans.isDirty = true;

						// 落ちる : 進む向きへ「進む量 + 接地面までの距離」のレイを打つ
						_motion.velocity += _gravity * _motion.gravityScale * _dt;
						const Math::Vector3 _move = _motion.velocity * _dt;
						const float _moveLen = _move.Length();

						bool _isBlocked = false;
						if (_moveLen > 1e-5f)
						{
							Math::Ray _ray;
							_ray.origin      = _trans.pos;
							_ray.direction   = _move / _moveLen;
							_ray.maxDistance = _moveLen + _motion.groundOffset;

							Engine::Physics::RayHit _hit = {};
							if (_physicsWorld.CastRay(_ray, _queryMask, _self, _hit) && _motion.velocity.Dot(_hit.normal) < 0.0f)
							{
								// 重力に向かい合う面なら地面。そこで止める
								if (_hit.normal.Dot(_upDir) >= GROUND_NORMAL_DOT)
								{
									_trans.pos = _hit.position + _hit.normal * _motion.groundOffset;
									_trans.isDirty = true;
									Land(_world, _motion, _model, _trans, _hit.position);
									break;
								}

								// 壁は抜けないよう、面に向かう速さだけ捨ててこのフレームは動かない
								_motion.velocity -= _hit.normal * _motion.velocity.Dot(_hit.normal);
								_isBlocked = true;
							}
						}

						if (!_isBlocked)
						{
							_trans.pos += _move;
							_trans.isDirty = true;
						}

						// 地面が見つからないまま時間が過ぎたら、その場で着地した扱いにする
						if (_motion.maxFallTime > 0.0f && _motion.timer >= _motion.maxFallTime)
						{
							Land(_world, _motion, _model, _trans, _trans.pos);
						}
						break;
					}

					//------------------------------------------------------
					// 地面についた : 時間で消える
					//------------------------------------------------------
					case Component::EDeathMotionPhase::Landed:
					{
						// 着地した位置と向きに留める(惰性や向きを書き戻す側に動かされない)
						_trans.pos = _motion.landedPos;
						_trans.quat = _motion.rotation;
						_trans.isDirty = true;

						// マイナスは消えない
						if (_motion.destroyTime < 0.0f) break;

						_motion.timer += _dt;

						// だんだん縮めて消す
						if (_motion.isDestroyFade && _motion.destroyTime > 0.0f)
						{
							const float _rate = std::clamp(1.0f - _motion.timer / _motion.destroyTime, 0.0f, 1.0f);
							_trans.scale = _motion.baseScale * _rate;
							_trans.isDirty = true;
						}

						// 0 なら着地した次のフレームで消える。
						// 解放予約したものは次の BeginFrame で ActiveTag が外れるので、二度は当たらない
						if (_motion.timer >= _motion.destroyTime)
						{
							_world.ReserveReleaseEntity(_self);
						}
						break;
					}

					default:
						break;
					}
				}
			}
		)
		// 物理空間へレイを撃つ
		.ReadsResource<Engine::Physics::PhysicsWorld>()
		// 爆発を積む(出すのは OneShotEffectSystem)
		.WritesResource<ECS::OneShotEffectEventResource>()
		// 順序 : 姿勢(LocalTransform)の書き手同士。移動と押し出しの後に落とし、闘技場の外へは境界面で押し戻す
		.After({ "MovementIntegrationSystem", "PositionIntegrationSystem", "RayCollisionSystem", "CapsuleCollisionSystem", "SphereCollisionSystem" })
		.Before("BoundaryClampSystem");

		//--------------------------------------------------------------------------
		// [Physics] 落下中の煙 : 親が落ちているあいだだけ出す
		//
		// 煙は遅延生成なので、出した側(DeathMotionSystem)は煙の ID を知らない。
		// そこで煙の側から親の段を見て、再生の要求を書く。
		// 親が居なくなっていたら(煙が出る前に親が消えた)自分も消える
		//--------------------------------------------------------------------------
		a_world.ActiveTask<const Component::DeathSmokeTag, const Component::HierarchyComponent, Component::EffectPlayRequestComponent>(
			Engine::ECS::ESystemType::Physics,
			"DeathSmokeSystem",
			[](
				Engine::ECS::Chunk*               a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::DeathSmokeTag*              /*a_tagArray*/,
				const Component::HierarchyComponent*         a_hierarchyArray,
				Component::EffectPlayRequestComponent*       a_requestArray
			)
			{
				auto& _world = *a_ctx.pWorld;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Engine::ECS::Entity _parent = a_hierarchyArray[_i].parentID;

					if (_parent == Engine::ECS::Limits::INVALID_ENTITY || !_world.IsAliveEntity(_parent))
					{
						a_requestArray[_i].isPlay = false;
						_world.ReserveReleaseEntity(a_pChunk->entityData[_i]);
						continue;
					}

					const auto* _pMotion = _world.HasComponent<Component::DeathMotionComponent>(_parent)
						? _world.RefData<Component::DeathMotionComponent>(_parent)
						: nullptr;

					a_requestArray[_i].isPlay = _pMotion && _pMotion->phase == Component::EDeathMotionPhase::Falling;
				}
			}
		)
		// 順序 : 親の段は DeathMotionSystem が書くので、その後で読む
		.After("DeathMotionSystem")
		// 絞り込みに使わない読み : 親の死亡モーション
		.Reads<Component::DeathMotionComponent>();
	}
}
