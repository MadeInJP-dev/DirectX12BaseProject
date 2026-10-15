#include "GroundDustEmitterSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Effect/GroundDustEmitterComponent.h"
#include "Application/Components/Movement/ActualVelocityComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Utility/EffectSpawnHelper.h"

#include "Engine/Physics/PhysicsWorld.h"

namespace App::System
{
	//==============================================================================
	// GroundDustEmitterSystem
	//
	// 機体から真下へレイを打ち、地面が近くて速く動いていれば地表へ砂埃を炊く。
	// ワームの体が炊く砂埃(BoidGroundEffectSystem)の「地面の上」と同じ作りで、
	// そこへ速さの効きを足したもの。
	//
	//   高さ : 真下へ maxHeight だけ打ち、当たれば地表へ炊く。
	//          近いほど大きく(nearScale)、maxHeight ぎりぎりで farScale
	//   速さ : 水平の実速度が minSpeed から fullSpeed へ上がるほど、
	//          間隔を詰め(slowInterval → fastInterval)、大きく(slowScale → 1)する
	//   向き : 地面の法線を、進む向きの逆へ kickBack だけ倒す。
	//          エフェクトは向きの側の半球へ粒を飛ばすので、後ろへ巻き上がって見える
	//
	// ・レイを打つのは残り時間(timer)が切れたときだけ。遅いときは打たない。
	// ・機体の原点は足元なので、始点は rayStartUp だけ上げる(接地判定 RayCollisionSystem と同じ)。
	//   高さはその分を引いて足元から測る。
	// ・地面として見るのは StaticObject だけ(BoidGroundEffectSystem と同じ)。
	// ・位置は前フレームのワールド行列(Update 帯ではまだ今フレームの行列が組まれていない)。
	//   レイの始点が 1 フレーム遅れるだけなので、出る位置には効かない。
	// ・エフェクトは遅延生成(ReserveSpawnEffectAt)。出し切ったら自分から消える。
	//==============================================================================
	void GroundDustEmitterSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<Component::GroundDustEmitterComponent, const Component::WorldMatrixComponent, const Component::ActualVelocityComponent>(
			Engine::ECS::ESystemType::Update,
			"GroundDustEmitterSystem",
			[](
				Engine::ECS::Chunk*                         a_pChunk,
				uint32_t                                    a_count,
				const Engine::ECS::SystemContext&           a_ctx,
				Component::ActiveTag*                       /*a_tags*/,
				Component::GroundDustEmitterComponent*      a_emitterArray,
				const Component::WorldMatrixComponent*      a_worldMatArray,
				const Component::ActualVelocityComponent*   a_velocityArray
			)
			{
				const auto& _physicsWorld = a_ctx.pWorld->GetResource<Engine::Physics::PhysicsWorld>();
				const uint32_t _queryMask = static_cast<uint32_t>(Component::ECollisionLayer::StaticObject);

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::GroundDustEmitterComponent& _emitter = a_emitterArray[_i];
					if (_emitter.effectGUID == Core::DEFAULT_GUID) continue;

					//------------------------------------------------------
					// 速さ : 水平成分だけ見る(落ちているだけでは炊かない)
					//------------------------------------------------------
					const Math::Vector3 _velocity = Math::Vector3(a_velocityArray[_i].value);
					const Math::Vector3 _flatVel = { _velocity.x, 0.0f, _velocity.z };
					const float _speed = _flatVel.Length();

					// 遅いうちは数えない。速くなった瞬間にすぐ炊けるよう、残り時間も 0 に戻しておく
					if (_speed < _emitter.minSpeed)
					{
						_emitter.timer = 0.0f;
						continue;
					}

					_emitter.timer -= a_ctx.dt;
					if (_emitter.timer > 0.0f) continue;

					// minSpeed → fullSpeed を 0..1 へ。fullSpeed が minSpeed 以下なら常に全開
					const float _speedRange = _emitter.fullSpeed - _emitter.minSpeed;
					const float _speedT = (_speedRange > 1e-4f)
						? std::clamp((_speed - _emitter.minSpeed) / _speedRange, 0.0f, 1.0f)
						: 1.0f;

					// 次の番。毎回ばらして、一定の拍で炊いているように見せない
					const float _interval = std::max(
						_emitter.slowInterval + (_emitter.fastInterval - _emitter.slowInterval) * _speedT,
						0.01f);
					_emitter.timer = _interval * Math::Random::Float(0.75f, 1.25f);

					//------------------------------------------------------
					// 高さ : 真下の地面が近ければ、近いほど大きく炊く
					//------------------------------------------------------
					if (_emitter.maxHeight <= 0.0f) continue;

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					const float _startUp = std::max(_emitter.rayStartUp, 0.0f);

					Math::Ray _downRay;
					_downRay.origin      = Math::Matrix(a_worldMatArray[_i].worldMat).Translation();
					_downRay.origin.y   += _startUp;
					_downRay.direction   = { 0.0f, -1.0f, 0.0f };
					_downRay.maxDistance = _emitter.maxHeight + _startUp;

					Engine::Physics::RayHit _downHit = {};
					if (!_physicsWorld.CastRay(_downRay, _queryMask, _self, _downHit)) continue;

					const float _height = std::max(_downHit.distance - _startUp, 0.0f);
					const float _heightT = std::clamp(_height / _emitter.maxHeight, 0.0f, 1.0f);
					const float _heightScale = _emitter.nearScale + (_emitter.farScale - _emitter.nearScale) * _heightT;
					const float _speedScale = _emitter.slowScale + (1.0f - _emitter.slowScale) * _speedT;
					const float _scale = _heightScale * _speedScale;
					if (_scale <= 0.0f) continue;

					//------------------------------------------------------
					// 向き : 法線を進む向きの逆へ倒して、後ろへ巻き上げる
					//------------------------------------------------------
					Math::Vector3 _emitDir = _downHit.normal + (-_flatVel / _speed) * std::max(_emitter.kickBack, 0.0f);
					if (_emitDir.LengthSquared() <= 1e-8f) _emitDir = _downHit.normal;

					App::Utility::ReserveSpawnEffectAt(*a_ctx.pWorld, _emitter.effectGUID, _downHit.position, true, _emitDir, _scale);
				}
			}
		)
		// 物理空間へレイを撃つ
		.ReadsResource<Engine::Physics::PhysicsWorld>();
	}
}
