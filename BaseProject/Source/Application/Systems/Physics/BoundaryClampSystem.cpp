#include "BoundaryClampSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/InstanceResource/BoundaryResource.h"
#include "Application/Components/Input/PlayerControllTag.h"
#include "Application/Components/Movement/ActualVelocityComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"

namespace App::System
{
	//==============================================================================
	// BoundaryClampSystem
	//
	// プレイヤーの位置を行ける範囲(Boundary オブジェクトが置く BoundaryResource)の
	// 内側へ挟む。外へ出ようとしても壁に沿って止まる。
	//
	// ・座標を書く仲間(積分・地面のレイ・カプセル/球の押し出し)より後ろに置く。
	//   最後に挟めば、押し出しで外へ押されても範囲の外には残らない。
	//   ワールド行列を組む CalcMatrixSystem(PostUpdate)とカメラ(Camera)より前なので、
	//   はみ出した位置が絵やカメラに映ることもない。
	// ・プレイヤーは親を持たないので、ローカル座標がそのままワールド座標。
	// ・挟んだ軸は、外向きの実速度も捨てる。残すと押し付けている間も速さが溜まり、
	//   壁から離れる向きへ切り返したときに、溜まった速さの分だけ遅れる。
	//==============================================================================
	void BoundaryClampSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<const Component::PlayerControllTag, Component::LocalTransformComponent>(
			Engine::ECS::ESystemType::Physics,
			"BoundaryClampSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_activeTag*/,
				const Component::PlayerControllTag* /*a_playerTagArray*/,
				Component::LocalTransformComponent* a_transArray
				)
			{
				auto& _world = *a_ctx.pWorld;
				if (!_world.HasResource<InstanceResource::BoundaryResource>()) return;

				const auto& _boundary = _world.GetResource<InstanceResource::BoundaryResource>();
				if (!_boundary.isActive) return;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::LocalTransformComponent& _trans = a_transArray[_i];

					const Math::Vector3 _pos = _trans.pos;
					const Math::Vector3 _clamped = _boundary.ClampInside(_pos);
					if (_clamped == _pos) continue;

					_trans.pos = _clamped;
					_trans.isDirty = true;

					// 外向きの実速度を捨てる(押し戻した向き = 外へ出ようとしていた向きの逆)
					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];
					if (!_world.HasComponent<Component::ActualVelocityComponent>(_self)) continue;

					auto* _pVelocity = _world.RefData<Component::ActualVelocityComponent>(_self);
					if (!_pVelocity) continue;

					Math::Vector3& _vel = _pVelocity->value;
					const Math::Vector3 _push = _clamped - _pos;
					if ((_push.x > 0.0f && _vel.x < 0.0f) || (_push.x < 0.0f && _vel.x > 0.0f)) _vel.x = 0.0f;
					if ((_push.y > 0.0f && _vel.y < 0.0f) || (_push.y < 0.0f && _vel.y > 0.0f)) _vel.y = 0.0f;
					if ((_push.z > 0.0f && _vel.z < 0.0f) || (_push.z < 0.0f && _vel.z > 0.0f)) _vel.z = 0.0f;
				}
			}
		)
		// 順序 : 座標の書き手(積分・地面のレイ・押し出し)の一番後ろで挟む
		.ReadsResource<InstanceResource::BoundaryResource>()
		.After({ "MovementIntegrationSystem", "PositionIntegrationSystem", "RayCollisionSystem", "CapsuleCollisionSystem", "SphereCollisionSystem" })
		// 絞り込みに使わない書き : 外向きの実速度を捨てる
		.Writes<Component::ActualVelocityComponent>();
	}
}
