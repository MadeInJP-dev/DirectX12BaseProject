#include "AlignToVelocitySystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Weapon/Projectile/AlignToVelocityComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"

namespace App::System
{
	//==============================================================================
	// AlignToVelocitySystem
	//
	// 速度の向きへモデルのローカル +Z を向ける。
	//
	// ・弾は生成時に向きを入れないので、これが無いと細長いモデルはプレハブの向きのまま飛ぶ。
	// ・Update 帯に置く。速度を書く HomingSystem(PreUpdate)の後で、
	//   そのフレームの曲がった向きに合わせられる(LockOnRotationSystem と同じ帯)。
	// ・上の向きはワールドの +Y。真上・真下へ飛ぶときは LookRotation が別の軸で作り直す。
	//==============================================================================
	void AlignToVelocitySystem::Init(App::ECS::APPWorld& a_world)
	{
		// 自分のチャンクの値だけを書くので、ワーカーで回す
		a_world.ActiveJobTask<const Component::AlignToVelocityComponent, const Component::DesiredVelocityComponent, Component::LocalTransformComponent>(
			Engine::ECS::ESystemType::Update,
			"AlignToVelocitySystem",
			[](
				Engine::ECS::Chunk*                             /*a_pChunk*/,
				uint32_t                                        a_count,
				const Engine::ECS::SystemContext&               /*a_ctx*/,
				Component::ActiveTag*                           /*a_tags*/,
				const Component::AlignToVelocityComponent*      a_alignArray,
				const Component::DesiredVelocityComponent*      a_velArray,
				Component::LocalTransformComponent*             a_trsArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Math::Vector3 _vel(a_velArray[_i].value);
					const float _minSpeed = a_alignArray[_i].minSpeed;
					if (_vel.LengthSquared() <= _minSpeed * _minSpeed) continue;

					Component::LocalTransformComponent& _trs = a_trsArray[_i];
					_trs.quat = Math::Quaternion::LookRotation(_vel, Math::Vector3::Up());
					_trs.isDirty = true;
				}
			}
		);
	}
}
