#include "SwarmLookSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Movement/LookAngleComponent.h"
#include "Application/Components/Boid/BoidMembershipComponent.h"
#include "Application/Components/Boid/BoidSteeringParamsComponent.h"
#include "Application/Components/Boid/BoidLeaderComponent.h"
#include "Application/Components/Boid/PlatoonLeaderComponent.h"
#include "Application/Components/Boid/PlatoonFormationComponent.h"
#include "Application/Components/Movement/ActualVelocityComponent.h"
#include "Application/Components/Boid/SwarmMissileComponent.h"
#include "Application/Components/Boid/SwarmBurstComponent.h"

namespace App::System
{
	//==============================================================================
	// SwarmLookSystem
	//
	// 群れのボス(リーダー / 小隊長 / ボイド)の「どちらを向いているか」を進める。
	// 持ち物は既存の LookAngleComponent で、体の向き(quat)にするのは RotationSystem。
	//
	//   リーダー・小隊長 … 実際に進んでいる向き(ActualVelocityComponent)へ寄せる。
	//                      小隊長は円陣で向けと命じられていれば(PlatoonFormationComponent の isLookAt)その向きへ
	//   ボイド           … 所属している小隊長(BoidMembershipComponent.platoonID)の向きへ寄せる
	//   ミサイル         … 切り離されたボイド。飛んでいる向き(SwarmMissileComponent.dir)をそのまま向く
	//   爆散             … 死亡で飛び散ったボイド。飛んでいる向き(SwarmBurstComponent.velocity)をそのまま向く
	//
	// ・寄せる速さは各コンポーネントの turnSpeedDeg(度/秒)。Yaw と Pitch に同じ値を使う。
	// ・止まっている間(速度がほぼ 0)は向きを変えない。0 ベクトルから角度を作ると
	//   どこを向くか決まらず、その場でくるくる回ってしまうため。
	// ・小隊長はこの向きを「前の相手の後ろ」を求めるのに使う(PlatoonFollowSystem)。
	//   つまり列の進路はリーダーの向きから順に伝わっていく。
	// ・相手の角度は RefData で引く。クエリに入れると、同じ LookAngle を読む側と書く側で
	//   依存が輪になるため。前フレームの値を見ることがあるが、1フレームの遅れで済む。
	//==============================================================================
	namespace
	{
		//--------------------------------------------------------------------------
		// -180〜180 度に畳む
		//--------------------------------------------------------------------------
		float WrapDeg180(float a_deg)
		{
			a_deg = std::fmod(a_deg + 180.0f, 360.0f);
			if (a_deg < 0.0f) a_deg += 360.0f;
			return a_deg - 180.0f;
		}

		//--------------------------------------------------------------------------
		// 角度を目標へ最大 a_maxDelta だけ近づける(近い方の回り方で回る)
		//--------------------------------------------------------------------------
		float MoveTowardDeg(float a_current, float a_target, float a_maxDelta)
		{
			const float _diff = WrapDeg180(a_target - a_current);

			// 速さの指定が無いなら一気に合わせる
			if (a_maxDelta <= 0.0f) return WrapDeg180(a_target);

			if (std::fabs(_diff) <= a_maxDelta) return WrapDeg180(a_target);

			return WrapDeg180(a_current + std::copysign(a_maxDelta, _diff));
		}

		//--------------------------------------------------------------------------
		// 進んでいる向きへ視点角を寄せる
		//--------------------------------------------------------------------------
		void TurnToVelocity(
			Component::LookAngleComponent& a_look,
			const Math::Vector3& a_velocity,
			float a_turnSpeedDeg,
			float a_dt)
		{
			float _yaw = 0.0f;
			float _pitch = 0.0f;
			if (!Component::MakeLookAngleFromDir(a_velocity, _yaw, _pitch)) return;	// 止まっている間はそのまま

			const float _step = a_turnSpeedDeg * a_dt;

			a_look.Yaw = MoveTowardDeg(a_look.Yaw, _yaw, _step);
			a_look.Pitch = MoveTowardDeg(a_look.Pitch, _pitch, _step);
			a_look.Pitch = std::clamp(a_look.Pitch, -a_look.maxPitch, a_look.maxPitch);
		}
	}

	void SwarmLookSystem::Init(App::ECS::APPWorld& a_world)
	{
		//--------------------------------------------------------------------------
		// リーダー : 進んでいる向きへ
		//--------------------------------------------------------------------------
		// 自分のチャンクの値だけを書く(ほかのエンティティは RefData で読むだけ)ので、ワーカーで回す
		a_world.ActiveJobTask<const Component::BoidLeaderComponent, const Component::ActualVelocityComponent, Component::LookAngleComponent>(
			Engine::ECS::ESystemType::Update,
			"SwarmLookSystem_Leader",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::BoidLeaderComponent*        a_leaderArray,
				const Component::ActualVelocityComponent*    a_actualArray,
				Component::LookAngleComponent*               a_lookArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					TurnToVelocity(
						a_lookArray[_i], a_actualArray[_i].value,
						a_leaderArray[_i].turnSpeedDeg, a_ctx.dt);
				}
			}
		);

		//--------------------------------------------------------------------------
		// 小隊長 : 進んでいる向きへ(円陣で向けと命じられていればその向きへ)
		//--------------------------------------------------------------------------
		// 自分のチャンクの値だけを書く(ほかのエンティティは RefData で読むだけ)ので、ワーカーで回す
		a_world.ActiveJobTask<const Component::PlatoonLeaderComponent, const Component::ActualVelocityComponent, Component::LookAngleComponent>(
			Engine::ECS::ESystemType::Update,
			"SwarmLookSystem_Platoon",
			[](
				Engine::ECS::Chunk*               a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::PlatoonLeaderComponent*     a_platoonArray,
				const Component::ActualVelocityComponent*    a_actualArray,
				Component::LookAngleComponent*               a_lookArray
			)
			{
				auto& _world = *a_ctx.pWorld;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Math::Vector3 _dir = a_actualArray[_i].value;

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];
					if (_world.HasComponent<Component::PlatoonFormationComponent>(_self))
					{
						const auto* _pFormation = _world.RefData<Component::PlatoonFormationComponent>(_self);
						if (_pFormation && _pFormation->isActive && _pFormation->isLookAt)
						{
							_dir = _pFormation->lookDir;
						}
					}

					TurnToVelocity(a_lookArray[_i], _dir, a_platoonArray[_i].turnSpeedDeg, a_ctx.dt);
				}
			}
		)
		// 順序 : 向き(LookAngle)はリーダー → 小隊長 → ボイドの順に決める
		.After("SwarmLookSystem_Leader")
		// 絞り込みに使わない読み : 円陣の命令(自分のもの)
		.Reads<Component::PlatoonFormationComponent>();

		//--------------------------------------------------------------------------
		// ボイド : 所属している小隊長の向きへ
		//--------------------------------------------------------------------------
		a_world.ActiveJobTask<const Component::BoidMembershipComponent, const Component::BoidSteeringParamsComponent, Component::LookAngleComponent>(
			Engine::ECS::ESystemType::Update,
			"SwarmLookSystem_Boid",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::BoidMembershipComponent*    a_memberArray,
				const Component::BoidSteeringParamsComponent* a_paramsArray,
				Component::LookAngleComponent*               a_lookArray
			)
			{
				auto& _world = *a_ctx.pWorld;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::LookAngleComponent&  _look = a_lookArray[_i];

					// 所属が無い / 小隊長が居なくなったなら今の向きのまま
					const Engine::ECS::Entity _platoon = a_memberArray[_i].platoonID;
					if (_platoon == Engine::ECS::Limits::INVALID_ENTITY) continue;
					if (!_world.IsAliveEntity(_platoon)) continue;
					if (!_world.HasComponent<Component::LookAngleComponent>(_platoon)) continue;

					const Component::LookAngleComponent* _pTargetLook = _world.RefData<Component::LookAngleComponent>(_platoon);
					if (!_pTargetLook) continue;

					const float _step = a_paramsArray[_i].turnSpeedDeg * a_ctx.dt;

					_look.Yaw   = MoveTowardDeg(_look.Yaw, _pTargetLook->Yaw, _step);
					_look.Pitch = MoveTowardDeg(_look.Pitch, _pTargetLook->Pitch, _step);
					_look.Pitch = std::clamp(_look.Pitch, -_look.maxPitch, _look.maxPitch);
				}
			}
		)
		// 順序 : ボイドは小隊長の今フレームの向きへ寄せる
		.After("SwarmLookSystem_Platoon")
		// 絞り込みに使わない読み : 小隊長の向き(別のエンティティ)
		.Reads<Component::LookAngleComponent>();

		//--------------------------------------------------------------------------
		// ミサイル(切り離されたボイド) : 飛んでいる向きへ
		//
		// 向きは SwarmMissileSystem が曲がれる角度の上限つきで回しているので、寄せずにそのまま向く
		//--------------------------------------------------------------------------
		a_world.ActiveJobTask<const Component::SwarmMissileComponent, Component::LookAngleComponent>(
			Engine::ECS::ESystemType::Update,
			"SwarmLookSystem_Missile",
			[](
				Engine::ECS::Chunk*               /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::SwarmMissileComponent*      a_missileArray,
				Component::LookAngleComponent*               a_lookArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::LookAngleComponent& _look = a_lookArray[_i];

					float _yaw = 0.0f;
					float _pitch = 0.0f;
					if (!Component::MakeLookAngleFromDir(a_missileArray[_i].dir, _yaw, _pitch)) continue;

					_look.Yaw   = _yaw;
					_look.Pitch = std::clamp(_pitch, -_look.maxPitch, _look.maxPitch);
				}
			}
		)
		// 順序 : 向き(LookAngle)の書き手同士(対象のアーキタイプは重ならない)
		.After("SwarmLookSystem_Boid");

		//--------------------------------------------------------------------------
		// 爆散(死亡で飛び散ったボイド) : 飛んでいる向きへ
		//--------------------------------------------------------------------------
		a_world.ActiveJobTask<const Component::SwarmBurstComponent, Component::LookAngleComponent>(
			Engine::ECS::ESystemType::Update,
			"SwarmLookSystem_Burst",
			[](
				Engine::ECS::Chunk*               /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::SwarmBurstComponent*        a_burstArray,
				Component::LookAngleComponent*               a_lookArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::LookAngleComponent& _look = a_lookArray[_i];

					float _yaw = 0.0f;
					float _pitch = 0.0f;
					if (!Component::MakeLookAngleFromDir(a_burstArray[_i].velocity, _yaw, _pitch)) continue;

					_look.Yaw   = _yaw;
					_look.Pitch = std::clamp(_pitch, -_look.maxPitch, _look.maxPitch);
				}
			}
		)
		// 順序 : 向き(LookAngle)の書き手同士(対象のアーキタイプは重ならない)
		.After("SwarmLookSystem_Missile");
	}
}
