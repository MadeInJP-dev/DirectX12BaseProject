#include "DebugKillSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/InstanceResource/DeathEventResource.h"
#include "Application/InstanceResource/DebugCommandResource.h"

namespace App::System
{
	//==============================================================================
	// DebugKillSystem
	//
	// デバッグキーで「倒す」と予約されたもの(DebugCommandResource の killReservedVec)を、
	// 体力切れと同じ形で死亡状態へ入れる。
	//
	// ・死亡状態にして死亡を積むところまでは HealthSystem と同じ。死亡エフェクトもスコアも
	//   いつもの読み手(EffectEventSystem / ScoreSystem)が出すので、ここは何も足さない。
	// ・予約するのはウェーブを飛ばす SceneSequence(オブジェクト)。オブジェクトはシステムの後で
	//   更新されるので、予約は次のフレームのここで消化される。
	// ・入口が無いワールド(Shipping)では何もしない。
	// ・PostUpdate 帯。死亡を積む側の並び(HealthSystem の後)に置く。
	//==============================================================================
	void DebugKillSystem::Init(App::ECS::APPWorld& a_world)
	{
		// コンポーネントを回さないのでカスタムタスクで登録する(フレームに1回だけ走る)
		a_world.ActiveCustomTask(
			Engine::ECS::ESystemType::PostUpdate,
			"DebugKillSystem",
			Engine::ECS::ReadList<Component::LocalTransformComponent>{},
			Engine::ECS::WriteList<Component::HealthComponent>{},
			[](const Engine::ECS::SystemContext& a_ctx)
			{
				auto* _pDebug = InstanceResource::DebugCommandResource::Find(a_ctx.pWorld);
				if (!_pDebug || _pDebug->killReservedVec.empty()) return;

				auto& _world = *a_ctx.pWorld;
				auto* _pDeathEvents = _world.HasResource<InstanceResource::DeathEventResource>()
					? &_world.RefResource<InstanceResource::DeathEventResource>()
					: nullptr;

				for (const Engine::ECS::Entity _entity : _pDebug->killReservedVec)
				{
					// 予約してから消えたもの・体力を持たないものは飛ばす
					if (!_world.IsAliveEntity(_entity)) continue;
					if (!_world.HasComponent<Component::HealthComponent>(_entity)) continue;

					Component::HealthComponent* _pHealth = _world.RefData<Component::HealthComponent>(_entity);
					if (!_pHealth || _pHealth->isDead) continue;

					_pHealth->currentHealth = 0.0f;
					_pHealth->isDead        = true;
					_pHealth->deathTimer    = 0.0f;

					if (!_pDeathEvents) continue;

					InstanceResource::DeathEvent _death = {};
					_death.entity = _entity;
					if (_world.HasComponent<Component::LocalTransformComponent>(_entity))
					{
						_death.pos = _world.RefData<Component::LocalTransformComponent>(_entity)->pos;
					}
					_pDeathEvents->Push(_death);
				}

				_pDebug->killReservedVec.clear();
			}
		)
		// 順序 : 死亡を積む側同士の並び
		.After("HealthSystem")
		.WritesResource<InstanceResource::DebugCommandResource>()
		.WritesResource<InstanceResource::DeathEventResource>();
	}
}
