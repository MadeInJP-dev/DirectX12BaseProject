#include "PlatoonFollowSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Boid/PlatoonLeaderComponent.h"
#include "Application/Components/Boid/PlatoonFormationComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Movement/MovementParamsComponent.h"
#include "Application/Components/Movement/ActualVelocityComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"
#include "Application/Components/Movement/LookAngleComponent.h"

namespace App::System
{
	//==============================================================================
	// PlatoonFollowSystem
	//
	// 小隊長を一つ前の相手(preLeader)の**後ろ**へ追従させる。
	// 書くのは目標速度(DesiredVelocityComponent)だけで、加減速と座標の積分は
	// MovementIntegrationSystem(Physics)に任せる。
	//
	//   目標地点 = 前の相手の位置 - 前の相手の前方 × distance
	//   目標速度 = 前の相手の実速度 + 目標地点への差 × followGain
	//
	// ・前の相手の「前方」は LookAngleComponent(SwarmLookSystem が進んでいる向きへ寄せる)。
	//   角度を持っていない相手は体の向き(quat の +Z)で代用する。
	// ・前の相手の実速度を上乗せするのは、ずれが出てから追いかけ始めると
	//   間隔が開いたまま詰まらないため。止まっている相手なら位置のずれだけで動く。
	// ・目標地点の手前でも奥でも同じ式で寄る(追い越したら下がる)。
	// ・速さは MovementParamsComponent.moveSpeed で頭打ちにする。前の相手より遅いと離されていく。
	// ・上下も同じ式で追う(地中や空中を潜って追いかけるため)。上下は加減速が掛からず
	//   目標速度がそのまま乗る点に注意(MovementIntegrationSystem の仕様)。
	//
	// ・前の相手の位置と実速度は RefData で引く。どちらも前フレームの Physics の結果なので、
	//   列の中で誰から先に処理しても同じ値を見る(並び順に依存しない)。
	// ・自分の座標も RefData で引く(読みは TaskAccess で宣言してある)。
	//   LocalTransform を読んで Velocity を書くので、その逆をする LockOnRotationSystem とは
	//   読み書きが往復する。あちらが After(PlatoonFollowSystem) で向きを決めている。
	// ・前の相手が居なくなったら目標速度を 0 にして止める。
	//
	// ・円陣の置き場所へ行けと命じられていれば(PlatoonFormationComponent の isActive)、
	//   前の相手は追わずに置き場所へ寄る(目標速度 = 置き場所への差 × followGain)。
	//==============================================================================
	namespace
	{
		//--------------------------------------------------------------------------
		// そのエンティティが向いている方向
		//
		// 視点角(LookAngleComponent)を持っているならそこから作る。
		// 持っていない相手は体の向き(LocalTransform.quat の +Z)で代用する
		//--------------------------------------------------------------------------
		Math::Vector3 GetForward(Engine::ECS::World& a_world, Engine::ECS::Entity a_entity)
		{
			if (a_world.HasComponent<Component::LookAngleComponent>(a_entity))
			{
				if (const auto* _pLook = a_world.RefData<Component::LookAngleComponent>(a_entity))
				{
					return MakeLookForward(*_pLook);
				}
			}

			if (a_world.HasComponent<Component::LocalTransformComponent>(a_entity))
			{
				if (const auto* _pTrs = a_world.RefData<Component::LocalTransformComponent>(a_entity))
				{
					return Math::Vector3::Transform(Math::Vector3::Forward(), _pTrs->quat);
				}
			}

			return Math::Vector3::Forward();
		}
	}

	void PlatoonFollowSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<const Component::PlatoonLeaderComponent, const Component::MovementParamsComponent, Component::DesiredVelocityComponent>(
			Engine::ECS::ESystemType::Update,
			"PlatoonFollowSystem",
			[](
				Engine::ECS::Chunk*      a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::PlatoonLeaderComponent*     a_platoonArray,
				const Component::MovementParamsComponent*          a_movementArray,
				Component::DesiredVelocityComponent*                a_velArray
			)
			{
				auto& _world = *a_ctx.pWorld;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::PlatoonLeaderComponent&  _platoon = a_platoonArray[_i];
					const Component::MovementParamsComponent&       _move    = a_movementArray[_i];
					Component::DesiredVelocityComponent&             _vel     = a_velArray[_i];

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];
					if (!_world.HasComponent<Component::LocalTransformComponent>(_self)) continue;

					//----------------------------------------------------------
					// 円陣の置き場所へ行けと命じられていれば、そこへ寄る
					//----------------------------------------------------------
					if (_world.HasComponent<Component::PlatoonFormationComponent>(_self))
					{
						const auto* _pFormation = _world.RefData<Component::PlatoonFormationComponent>(_self);
						if (_pFormation && _pFormation->isActive)
						{
							const Math::Vector3 _pos = _world.RefData<Component::LocalTransformComponent>(_self)->pos;
							Math::Vector3 _target = (_pFormation->slotPos - _pos) * _platoon.followGain;

							if (_move.moveSpeed > 0.0f)
							{
								const float _speedSq = _target.LengthSquared();
								if (_speedSq > _move.moveSpeed * _move.moveSpeed)
								{
									_target *= _move.moveSpeed / std::sqrt(_speedSq);
								}
							}

							_vel.value = _target;
							continue;
						}
					}

					//----------------------------------------------------------
					// 前の相手が居なければ止まる
					//----------------------------------------------------------
					const Engine::ECS::Entity _pre = _platoon.preLeader;
					if (_pre == Engine::ECS::Limits::INVALID_ENTITY ||
						!_world.IsAliveEntity(_pre) ||
						!_world.HasComponent<Component::LocalTransformComponent>(_pre))
					{
						_vel.value = Math::Vector3(0.0f, 0.0f, 0.0f);
						continue;
					}

					const Math::Vector3 _selfPos = _world.RefData<Component::LocalTransformComponent>(_self)->pos;
					const Math::Vector3 _prePos  = _world.RefData<Component::LocalTransformComponent>(_pre)->pos;

					// 前の相手の実速度。実速度を持たなければ目標速度で代用する
					Math::Vector3 _preVel = {};
					if (const auto* _pActual = _world.RefData<Component::ActualVelocityComponent>(_pre))
					{
						_preVel = _pActual->value;
					}
					else if (_world.HasComponent<Component::DesiredVelocityComponent>(_pre))
					{
						_preVel = _world.RefData<Component::DesiredVelocityComponent>(_pre)->value;
					}

					const Math::Vector3 _toPre = _prePos - _selfPos;
					const float _distSq = _toPre.LengthSquared();
					const float _followDistance = _platoon.distance;
					const float _followDistanceSq = _followDistance * _followDistance;

					// すでに十分近いのなら停止
					if (_distSq <= _followDistanceSq)
					{
						_vel.value = Math::Vector3::Zero();
						continue;
					}

					const float _dist = std::sqrt(_distSq);

					const Math::Vector3 _direction = _toPre / _dist;

					//----------------------------------------------------------
					// 前の相手の後ろを目標地点にする
					//----------------------------------------------------------
					// 前方が分かるので「相手の真後ろ」を狙える。相手が曲がれば目標地点も
					// 一緒に回り込むので、列は相手の軌跡をなぞって付いていく
					//----------------------------------------------------------
					const Math::Vector3 _preForward = GetForward(_world, _pre);
					const Math::Vector3 _goalPos = _prePos - _preForward * _followDistance;

					// 前の相手の速度をベースに追従
					Math::Vector3 _target = _preVel;

					// 目標地点との位置誤差を補正
					_target += (_goalPos - _selfPos) * _platoon.followGain;

					// 速さの頭打ち
					if (_move.moveSpeed > 0.0f)
					{
						const float _speedSq = _target.LengthSquared();
						if (_speedSq > _move.moveSpeed * _move.moveSpeed)
						{
							_target *= _move.moveSpeed / std::sqrt(_speedSq);
						}
					}

					_vel.value = _target;
				}
			}
		)
		// 順序 : 目標速度(Velocity)の書き手同士の並び
		.After("CharacterMovementSystem")
		// 絞り込みに使わない読み : 前の相手の向き・実速度(目標速度)と、自分と前の相手の位置、円陣の命令。
		// LocalTransform の読みは LockOnRotationSystem と読み書きが往復するので、
		// あちらが After(PlatoonFollowSystem) で向きを決めている
		.Reads<Component::LookAngleComponent, Component::LocalTransformComponent, Component::ActualVelocityComponent, Component::DesiredVelocityComponent, Component::PlatoonFormationComponent>();
	}
}
