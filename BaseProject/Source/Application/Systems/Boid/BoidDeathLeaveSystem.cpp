#include "BoidDeathLeaveSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Boid/BoidSteeringParamsComponent.h"
#include "Application/Components/Boid/BoidWaveStateComponent.h"
#include "Application/Components/Boid/BoidContactDamageComponent.h"
#include "Application/Components/Camera/FollowTargetComponent.h"
#include "Application/Components/Character/DeathMotionComponent.h"
#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"
#include "Application/Components/Render/EmissiveOverrideComponent.h"

namespace App::System
{
	//==============================================================================
	// BoidDeathLeaveSystem
	//
	// 死亡モーション(DeathMotionComponent)を持つボイドが死んだら、群れの部品を外して群れから離す。
	// 落ちていく死体が仲間の操舵(分離・整列・結合)に混ざったり、
	// 小隊長を追いかけ続けたりしないようにするため。
	//
	// ・外すのは、自爆ミサイル化(SwarmMissileSystem)や爆散(SwarmBossController)で
	//   群れから切り離すときと同じ組 :
	//     BoidSteeringParams(操舵) / FollowTarget(追従先) / BoidWaveState(発光のウェーブ) / BoidContactDamage(体当たり)
	//   所属(BoidMembership)と目標地点(BoidTarget)は操舵の必須として付いたまま残るが、
	//   操舵が無ければ誰も使わない(向きを寄せる SwarmLookSystem も操舵を持つものだけを見る)。
	// ・外すだけで初期化からのやり直しにはならない(体力や死亡モーションの段は戻らない)。
	// ・同時に、操舵が最後に書いた目標速度を 0 にする(移動の積分が減速させて止める)。
	//   残したままだと、外した後もその速さで飛び続ける。
	// ・発光の差し替え(EmissiveOverride)は止める。ウェーブの最後の値が毎フレーム書き戻され、
	//   死亡モーションの「黒くする」が効かなくなるため。
	// ・群れから外すと、このタスクの対象からも外れるので1回だけ走る。
	//   構成の変更は予約なので、実際に外れるのは次の BeginFrame。
	//==============================================================================
	void BoidDeathLeaveSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<const Component::HealthComponent, const Component::DeathMotionComponent, const Component::BoidSteeringParamsComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"BoidDeathLeaveSystem",
			[](
				Engine::ECS::Chunk*               a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::HealthComponent*            a_healthArray,
				const Component::DeathMotionComponent*       /*a_motionArray*/,
				const Component::BoidSteeringParamsComponent* /*a_steeringArray*/
			)
			{
				auto& _world = *a_ctx.pWorld;

				const Engine::ECS::ComponentTypeID _removeIDs[] =
				{
					_world.GetCompTypeID<Component::BoidSteeringParamsComponent>(),
					_world.GetCompTypeID<Component::FollowTargetComponent>(),
					_world.GetCompTypeID<Component::BoidWaveStateComponent>(),
					_world.GetCompTypeID<Component::BoidContactDamageComponent>(),
				};

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					if (!a_healthArray[_i].isDead) continue;

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					// 惰性で飛び続けないよう、目標速度を下ろす
					if (_world.HasComponent<Component::DesiredVelocityComponent>(_self))
					{
						_world.RefData<Component::DesiredVelocityComponent>(_self)->value = Math::Vector3(0.0f, 0.0f, 0.0f);
					}

					// 発光は最後に写した値のまま残す(死亡モーションがそこから黒くする)
					if (_world.HasComponent<Component::EmissiveOverrideComponent>(_self))
					{
						_world.RefData<Component::EmissiveOverrideComponent>(_self)->isOverride = false;
					}

					// 群れの部品をまとめて外す。
					// 1つずつ ReserveRemoveComponent すると、どれも今のシグネチャから作るので
					// 後の予約が前に外したものを戻してしまう
					Engine::ECS::ChangeEntityCmd _cmd = {};
					_cmd.entity = _self;
					_cmd.toSig  = _world.GetSignature(_self);
					for (const Engine::ECS::ComponentTypeID _id : _removeIDs)
					{
						if (_id != Engine::ECS::Limits::INVALID_COMPONENTTYPEID) _cmd.toSig.reset(_id);
					}

					_world.ReserveChangeSignature(std::move(_cmd));
				}
			}
		)
		// 順序 : 死亡状態にする HealthSystem の後で見る(同じフレームのうちに外す予約を入れる)
		.After("HealthSystem")
		// 絞り込みに使わない書き込み : 目標速度と発光の差し替えを RefData で下ろす
		.Writes<Component::DesiredVelocityComponent, Component::EmissiveOverrideComponent>();
	}
}
