#include "PlatoonAimPointSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Boid/PlatoonLeaderComponent.h"
#include "Application/Components/Combat/AimPointComponent.h"

#include "Application/InstanceResource/BoidSnapshotResource.h"

namespace App::System
{
	//==============================================================================
	// PlatoonAimPointSystem
	//
	// ボイドボスの小隊長の狙われる点(AimPointComponent::worldPos)を、
	// 率いるボイドの群れの重心にいちばん近いボイドの位置にする。
	//
	// ・小隊長は見えず当たりも無い「群れのまとめ役」で、ボイドは小隊長を遅れて追うので、
	//   動いている間は小隊長の位置が群れの先(何も無い所)になる。
	//   プレイヤーのミサイルはロックした小隊長の狙点へ誘導する(GetAimPointPos)ので、
	//   小隊長の位置のままだと群れの手前の空を追ってしまう。
	// ・重心そのものではなく重心に近い1体にする。群れが曲がって伸びていると、
	//   重心がすき間に落ちることがあるため。実在するボイドなら、追えばそのまま当たる。
	// ・ボイドが1体も居なければ isWorldPos を下ろし、小隊長の位置へ戻す。
	// ・位置はボイドの写し(BoidSnapshotResource。PreUpdate の頭で作る)から引く。
	//   誘導(HomingSystem)は同じ PreUpdate で読むので、遅れても1フレーム。
	//==============================================================================
	void PlatoonAimPointSystem::Init(App::ECS::APPWorld& a_world)
	{
		// 書くのは自分のチャンクの狙点だけ、写しは読むだけなので、チャンクを分けてワーカーで回す
		a_world.ActiveJobTask<const Component::PlatoonLeaderComponent, Component::AimPointComponent>(
			Engine::ECS::ESystemType::PreUpdate,
			"PlatoonAimPointSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				const Component::PlatoonLeaderComponent* /*a_leaderArray*/,
				Component::AimPointComponent* a_aimPointArray
			)
			{
				const InstanceResource::BoidSnapshotResource& _snapshot = a_ctx.pWorld->GetResource<InstanceResource::BoidSnapshotResource>();

				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::AimPointComponent& _aimPoint = a_aimPointArray[_i];

					const std::span<const InstanceResource::BoidSnapshotResource::Entry> _boids = _snapshot.Find(a_pChunk->entityData[_i]);
					if (_boids.empty())
					{
						_aimPoint.isWorldPos = false;
						continue;
					}

					// 群れの重心
					Math::Vector3 _center = {};
					for (const auto& _boid : _boids) _center += _boid.position;
					_center /= static_cast<float>(_boids.size());

					// 重心にいちばん近いボイド
					float _bestDistSq = (std::numeric_limits<float>::max)();
					Math::Vector3 _bestPos = _center;
					for (const auto& _boid : _boids)
					{
						const float _distSq = (_boid.position - _center).LengthSquared();
						if (_distSq >= _bestDistSq) continue;
						_bestDistSq = _distSq;
						_bestPos = _boid.position;
					}

					_aimPoint.worldPos = _bestPos;
					_aimPoint.isWorldPos = true;
				}
			}
		)
		// 順序 : そのフレームの写しを作ってから引く
		.After("BoidSnapshotSystem")
		.ReadsResource<InstanceResource::BoidSnapshotResource>();
	}
}
