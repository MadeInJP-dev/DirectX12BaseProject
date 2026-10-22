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
	// ・同じエフェクトが MERGE_TIME 秒以内に MERGE_RADIUS m(× 大きさ倍率)以内で出ていたら、出さない。
	//   自爆ミサイルを同じ所で受け続けると爆発が毎秒何発も重なり、煙の板で塗りが詰まるため。
	//   残っている爆発の煙に紛れるので、1発ぶん抜けても見た目はほとんど変わらない。
	//   同じフレームに積まれたものどうしも、先に出したものを見て間引く。
	//==========================================================================================
	namespace
	{
		// 同じエフェクトを出さない時間(秒)
		constexpr float MERGE_TIME = 0.5f;

		// 同じエフェクトを出さない距離(m。大きさ倍率 1 のとき。倍率の大きい方を掛ける)
		constexpr float MERGE_RADIUS = 4.0f;

		// 1フレームに出す数の上限。千体が一気に死んでも、爆発のエンティティと粒が1フレームに集中しないように。
		// 超えたぶんは出さずに捨てる(同じ瞬間の爆発は多すぎて見分けがつかないため)
		constexpr uint32_t MAX_SPAWN_PER_FRAME = 8;

		// 近くで出たばかりか
		bool IsRecentlySpawned(const ECS::OneShotEffectEventResource& a_res, const Core::GUID& a_guid, const Math::Vector3& a_pos, float a_scale)
		{
			for (const ECS::OneShotEffectRecent& _recent : a_res.recent)
			{
				if (_recent.effectGUID != a_guid) continue;

				const float _radius = MERGE_RADIUS * std::max(a_scale, _recent.scale);
				if ((_recent.pos - a_pos).LengthSquared() < _radius * _radius) return true;
			}
			return false;
		}
	}

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

				// 時刻は積まれていないフレームも進める(間引く時間を測るため)
				_effectEvents.time += a_ctx.dt;
				if (_effectEvents.events.empty()) return;

				// 間引く時間を過ぎたものは忘れる
				const float _now = _effectEvents.time;
				std::erase_if(_effectEvents.recent,
					[_now](const ECS::OneShotEffectRecent& a_recent) { return (_now - a_recent.time) >= MERGE_TIME; });

				uint32_t _spawnedCount = 0;
				for (const ECS::OneShotEffectEvent& _event : _effectEvents.events)
				{
					if (_spawnedCount >= MAX_SPAWN_PER_FRAME) break;

					const Math::Vector3 _pos = _event.worldMat.Translation();
					if (IsRecentlySpawned(_effectEvents, _event.effectGUID, _pos, _event.scale)) continue;

					if (!App::Utility::ReserveSpawnEffect(*a_ctx.pWorld, _event.effectGUID, _event.worldMat, true, _event.scale))
					{
						ENGINE_LOG("[OneShotEffect] エフェクトを出せなかった : %s", _event.effectGUID.String().c_str());
						continue;
					}

					_effectEvents.recent.push_back({ _event.effectGUID, _pos, _event.scale, _now });
					++_spawnedCount;
				}

				_effectEvents.Clear();
			}
		)
		// 単発エフェクトの一覧を読んで空にする(積むのは DeathMotionSystem など)
		.WritesResource<ECS::OneShotEffectEventResource>();
	}
}
