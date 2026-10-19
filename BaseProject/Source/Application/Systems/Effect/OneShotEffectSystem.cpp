#include "OneShotEffectSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Application/ECS/Event/EffectEvent/EffectEvent.h"
#include "Application/Utility/EffectSpawnHelper.h"

namespace App::System
{
	//==========================================================================================
	// OneShotEffectSystem
	//
	// OneShotEffectEventResource に積まれた単発エフェクトを、1フレーム分まとめて出す。
	//
	// ・出すエンティティの組み合わせは ReserveSpawnEffectAt と同じ(EffectSpawnHelper の BuildEffectEntity)。
	//   出し切ったら自分から消える(destroyOnFinish)ので、出したあとは誰も面倒を見ない。
	// ・生成は遅延コマンドに積むだけで、実体化は次の BeginFrame。
	// ・出したら配列を空にする。PostUpdate の最後のほうに登録してあるので、
	//   同じフレームに積まれたものはここで出る。これより後で積まれたものは次のフレームで出る(捨てない)。
	//==========================================================================================
	void OneShotEffectSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveCustomTask(
			Engine::ECS::ESystemType::PostUpdate,
			"OneShotEffectSystem",
			Engine::ECS::ReadList<>{},
			Engine::ECS::WriteList<>{},
			[](const Engine::ECS::SystemContext& a_ctx)
			{
				if (!a_ctx.pWorld) return;
				if (!a_ctx.pWorld->HasResource<ECS::OneShotEffectEventResource>()) return;

				auto& _effectEvents = a_ctx.pWorld->RefResource<ECS::OneShotEffectEventResource>();
				if (_effectEvents.events.empty()) return;

				for (const ECS::OneShotEffectEvent& _event : _effectEvents.events)
				{
					if (!App::Utility::ReserveSpawnEffect(*a_ctx.pWorld, _event.effectGUID, _event.worldMat, true, _event.scale))
					{
						ENGINE_LOG("[OneShotEffect] エフェクトを出せなかった : %s", _event.effectGUID.String().c_str());
					}
				}

				_effectEvents.Clear();
			}
		)
		// 単発エフェクトの一覧を読んで空にする(積むのは DeathMotionSystem など)
		.WritesResource<ECS::OneShotEffectEventResource>();
	}
}
