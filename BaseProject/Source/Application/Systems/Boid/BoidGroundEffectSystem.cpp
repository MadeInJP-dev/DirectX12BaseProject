#include "BoidGroundEffectSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Boid/WarmGroundEffectComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/InstanceResource/WormGroundEffectResource.h"
#include "Application/Utility/EffectSpawnHelper.h"

#include "Engine/Physics/PhysicsWorld.h"

namespace App::System
{
	//==============================================================================
	// BoidGroundEffectSystem
	//
	// ワームの体(小隊長)から上下にレイを打ち、地表へ砂埃を炊く。
	// どのエフェクトをどう出すかは WormGroundEffectResource(SwarmBossController が書く)。
	//
	//   地面の中 … 真上へ打ったレイが上向きの面の裏に当たった = 地表の下。
	//              深さに関係なく、当たった地表へ underScale で炊く
	//   地面の上 … 真下へ maxHeight だけ打ち、当たれば地表へ炊く。
	//              近いほど大きく(nearScale)、maxHeight ぎりぎりで farScale
	//
	// ・小隊長は体の芯に等間隔で並んでいるので、真上・真下に同じ大きさで炊くと玉が連なって見える。
	//   レイを打つ位置を体の太さの中へ水平にばらし(scatterRadius)、大きさと間隔もばらして、
	//   隣どうしの砂埃が重なり合った帯に見えるようにする。
	// ・レイを打つのは残り時間(WarmGroundEffectComponent.timer)が切れた小隊長だけ。
	//   間隔 0.5 秒なら小隊長 100 体で 1 フレームに 3〜4 体ほど(レイは最大でその2倍)。
	// ・1 フレームに出す数が上限に達したら、そのフレームは打たずに時間だけ戻す(炊き損ねは捨てる)。
	//   待たせると、先に並んだチャンクの小隊長ばかりが毎回炊くことになるため。
	// ・地面として見るのは StaticObject だけ(SerchGroundSystem と同じ)。
	// ・エフェクトは遅延生成(ReserveSpawnEffectAt)。出し切ったら自分から消える。
	//==============================================================================
	namespace
	{
		// 半径 a_radius の円の中の点(水平。円の中に均等に散らす)
		Math::Vector3 RandomInDisc(float a_radius)
		{
			if (a_radius <= 0.0f) return Math::Vector3(0.0f, 0.0f, 0.0f);

			const float _angle = Math::Random::Float(0.0f, DirectX::XM_2PI);
			const float _dist = a_radius * std::sqrt(Math::Random::Float(0.0f, 1.0f));
			return Math::Vector3(std::cos(_angle) * _dist, 0.0f, std::sin(_angle) * _dist);
		}
	}

	void BoidGroundEffectSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<Component::WarmGroundEffectComponent, const Component::LocalTransformComponent>(
			Engine::ECS::ESystemType::Update,
			"BoidGroundEffectSystem",
			[](
				Engine::ECS::Chunk*               a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				Component::WarmGroundEffectComponent*        a_effectArray,
				const Component::LocalTransformComponent*    a_transArray
			)
			{
				if (!a_ctx.pWorld->HasResource<InstanceResource::WormGroundEffectResource>()) return;
				auto& _res = a_ctx.pWorld->RefResource<InstanceResource::WormGroundEffectResource>();
				if (!_res.isActive || _res.effectGUID == Core::DEFAULT_GUID) return;

				ENGINE_PROFILE_SCOPE("Physics_BoidGroundEffectRay");
				const auto& _physicsWorld = a_ctx.pWorld->GetResource<Engine::Physics::PhysicsWorld>();

				const float _dt = a_ctx.dt;
				const uint32_t _queryMask = static_cast<uint32_t>(Component::ECollisionLayer::StaticObject);
				const float _interval = std::max(_res.interval, 0.01f);

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::WarmGroundEffectComponent& _effect = a_effectArray[_i];

					_effect.timer -= _dt;
					if (_effect.timer > 0.0f) continue;

					// 次の番。毎回ばらして、隣の小隊長と同じ拍子で炊かないようにする。
					// ばらしすぎると、前の砂埃が薄れても次が出ない間が空いて体の所々が途切れて見えるので ±30% に留める
					_effect.timer = _interval * Math::Random::Float(0.7f, 1.3f);

					// 今フレームはもう出せない。レイも打たない
					if (_res.spawnedThisFrame >= _res.maxSpawnPerFrame) continue;

					// 打つ位置は体の太さの中へ水平にばらす(芯の一列に並ばないように)
					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];
					const Math::Vector3 _pos = a_transArray[_i].pos + RandomInDisc(_res.scatterRadius);

					Math::Vector3 _spawnPos = {};
					Math::Vector3 _emitDir  = {};
					float _scale = 0.0f;
					bool _isSpawn = false;

					//------------------------------------------------------
					// 地面の中 : 真上の地表へ常に炊く
					//------------------------------------------------------
					Math::Ray _upRay;
					_upRay.origin      = _pos;
					_upRay.direction   = { 0.0f, 1.0f, 0.0f };
					_upRay.maxDistance = _res.maxDepth;

					Engine::Physics::RayHit _upHit = {};
					if (_physicsWorld.CastRay(_upRay, _queryMask, _self, _upHit) && _upHit.normal.y > 0.0f)
					{
						_spawnPos = _upHit.position;
						_emitDir  = _upHit.normal;
						_scale    = _res.underScale;
						_isSpawn  = true;
					}
					//------------------------------------------------------
					// 地面の上 : 真下の地面が近ければ、近いほど大きく炊く
					//------------------------------------------------------
					else if (_res.maxHeight > 0.0f)
					{
						Math::Ray _downRay;
						_downRay.origin      = _pos;
						_downRay.direction   = { 0.0f, -1.0f, 0.0f };
						_downRay.maxDistance = _res.maxHeight;

						Engine::Physics::RayHit _downHit = {};
						if (_physicsWorld.CastRay(_downRay, _queryMask, _self, _downHit))
						{
							const float _t = std::clamp(_downHit.distance / _res.maxHeight, 0.0f, 1.0f);

							_spawnPos = _downHit.position;
							_emitDir  = _downHit.normal;
							_scale    = _res.nearScale + (_res.farScale - _res.nearScale) * _t;
							_isSpawn  = true;
						}
					}

					if (!_isSpawn || _scale <= 0.0f) continue;

					// 大きさもばらす(同じ大きさの玉が並ばないように)
					const float _jitter = std::clamp(_res.scaleJitter, 0.0f, 0.95f);
					_scale *= Math::Random::Float(1.0f - _jitter, 1.0f + _jitter);

					if (App::Utility::ReserveSpawnEffectAt(*a_ctx.pWorld, _res.effectGUID, _spawnPos, true, _emitDir, _scale))
					{
						++_res.spawnedThisFrame;
					}
				}
			}
		)
		// 物理空間へレイを撃つ
		.ReadsResource<Engine::Physics::PhysicsWorld>()
		.WritesResource<InstanceResource::WormGroundEffectResource>();
	}
}
