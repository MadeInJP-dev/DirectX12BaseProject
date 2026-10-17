#include "FollowLeaderSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Boid/BoidTargetComponent.h"
#include "Application/Components/Boid/PlatoonSpinComponent.h"
#include "Application/Components/Camera/FollowTargetComponent.h"
#include "Application/Components/Movement/LookAngleComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"

namespace App::System
{
	//==============================================================================
	// FollowLeaderSystem
	//
	// ボイドの目標地点(BoidTargetComponent)を、追従先(小隊長)の位置にする。
	//
	// ・書くのは自分の目標地点だけで、追従先の位置は読むだけなので、チャンクを分けてワーカーで回す。
	// ・PreUpdate 帯で回す。これを読む BoidSteeringSystem が PreUpdate にいるため(理由はあちらを参照)。
	//   小隊長の位置は前フレームの Physics の積分結果で、Update 帯では誰も動かさないので、
	//   Update に置いていたときと同じ値を読む。
	//
	// ・追従先が「胴回りを回れ」と命じられていれば(PlatoonSpinComponent の radius > 0)、
	//   目標地点を追従先の進む向きを軸にした輪の上へずらす。
	//     輪の上の角度 = 追従先の angle + ボイドごとの位相
	//     輪の半径     = 追従先の radius × ボイドごとの倍率(0.6〜1)
	//   位相と倍率はエンティティIDから決める(覚えておく値を増やさずに、毎フレーム同じ値になる)。
	//   angle が進むと全員の目標が輪の上を回り、ボイドが小隊長を起点に胴回りを回る。
	//==============================================================================
	namespace
	{
		//--------------------------------------------------------------------------
		// エンティティIDから 0〜1 の値を作る(a_salt で別の値にする)
		//--------------------------------------------------------------------------
		float Hash01(Engine::ECS::Entity a_entity, uint32_t a_salt)
		{
			uint32_t _x = static_cast<uint32_t>(a_entity ^ (a_entity >> 32)) * 0x9E3779B1u + a_salt * 0x85EBCA77u;
			_x ^= _x >> 16;
			_x *= 0x7FEB352Du;
			_x ^= _x >> 15;
			_x *= 0x846CA68Bu;
			_x ^= _x >> 16;
			return static_cast<float>(_x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
		}

		//--------------------------------------------------------------------------
		// 軸 a_axis に垂直な2本(輪の面)。上向きを基準に作るので、真上・真下を向くとき以外は
		// 軸が回っても輪の面が急に裏返らない(群れのボスの Pitch は ±80 度まで)
		//--------------------------------------------------------------------------
		void MakeRingBasis(const Math::Vector3& a_axis, Math::Vector3& a_outU, Math::Vector3& a_outV)
		{
			const Math::Vector3 _ref = (std::fabs(a_axis.y) < 0.999f) ? Math::Vector3::Up() : Math::Vector3::Right();
			a_outU = _ref.Cross(a_axis);
			a_outU.Normalize();
			a_outV = a_axis.Cross(a_outU);
		}

		// 輪の半径に掛ける倍率の下限(全員が同じ半径だと細い輪になり、胴の太さが出ない)
		constexpr float SPIN_RADIUS_MIN_RATE = 0.6f;
	}

	void FollowLeaderSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveJobTask<const Component::FollowTargetComponent, Component::BoidTargetComponent>(
			Engine::ECS::ESystemType::PreUpdate,
			"FollowLeaderSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_activeTags*/,
				const Component::FollowTargetComponent* a_followTargetArray,
				Component::BoidTargetComponent* a_targetArray
			)
			{
				auto& _world = *a_ctx.pWorld;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::FollowTargetComponent& _targetComp = a_followTargetArray[_i];
					Component::BoidTargetComponent& _targetPosComp = a_targetArray[_i];

					// 有効チェック
					if (_targetComp.target == Engine::ECS::Limits::INVALID_ENTITY) continue;
					if (!_world.HasComponent<Component::LocalTransformComponent>(_targetComp.target)) continue;

					// リーダーの座標を取得
					const Component::LocalTransformComponent* _pTargetTrance = _world.RefData<Component::LocalTransformComponent>(_targetComp.target);
					if (!_pTargetTrance) continue;

					// ボイドの目標地点として設定
					_targetPosComp.targetPos = _pTargetTrance->pos;

					//------------------------------------------------------
					// 胴回りを回れと命じられていれば、目標を輪の上へずらす
					//------------------------------------------------------
					if (!_world.HasComponent<Component::PlatoonSpinComponent>(_targetComp.target)) continue;
					const Component::PlatoonSpinComponent* _pSpin = _world.RefData<Component::PlatoonSpinComponent>(_targetComp.target);
					if (!_pSpin || _pSpin->radius <= 0.0f) continue;
					if (!_world.HasComponent<Component::LookAngleComponent>(_targetComp.target)) continue;

					const Math::Vector3 _axis = Component::MakeLookForward(*_world.RefData<Component::LookAngleComponent>(_targetComp.target));
					Math::Vector3 _u = {};
					Math::Vector3 _v = {};
					MakeRingBasis(_axis, _u, _v);

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];
					const float _phase  = Hash01(_self, 1u) * DirectX::XM_2PI;
					const float _rate   = SPIN_RADIUS_MIN_RATE + (1.0f - SPIN_RADIUS_MIN_RATE) * Hash01(_self, 2u);
					const float _angle  = _pSpin->angle + _phase;
					const float _radius = _pSpin->radius * _rate;

					_targetPosComp.targetPos += (_u * std::cos(_angle) + _v * std::sin(_angle)) * _radius;
				}
			}
		)
		// 絞り込みに使わない読み : 追従先の位置・回転の命令・向きを RefData で読む
		.Reads<Component::LocalTransformComponent, Component::PlatoonSpinComponent, Component::LookAngleComponent>();
	}
}
