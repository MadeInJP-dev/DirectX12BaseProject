#include "WeaponLimitSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Weapon/WeaponConfig/WeaponOverHeatComponent.h"
#include "Application/Components/Weapon/WeaponConfig/WeaponReloadComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"

#include "Application/Utility/EffectSpawnHelper.h"

namespace App::System
{
	void WeaponLimitSystem::Init(App::ECS::APPWorld& a_world)
	{
		//==================================================================================
		// 熱 : 撃っていてもいなくても毎フレーム冷ます
		//==================================================================================
		a_world.ActiveJobTask<Component::WeaponOverHeatComponent>(
			Engine::ECS::ESystemType::Update,
			"WeaponOverHeatSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				Component::WeaponOverHeatComponent* a_heatArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::WeaponOverHeatComponent& _heat = a_heatArray[_i];
					auto& _rt = _heat.runtimeData;

					// 合図は1フレームだけ。立てるのは撃った瞬間(撃ち方のシステム)
					_rt.isOverheatStarted = false;

					// オーバーヒート中は冷えを鈍らせられる(無理をした分だけ復帰を待たされる)
					const float _coolScale = _rt.isOverheat ? _heat.overheatCoolScale : 1.0f;
					_rt.heat = (std::max)(_rt.heat - _heat.coolRate * _coolScale * a_ctx.dt, 0.0f);

					if (_rt.isOverheat && _rt.heat <= _heat.limit * _heat.restartHeatRatio)
					{
						_rt.isOverheat = false;
					}
				}
			}
		);

		//==================================================================================
		// リロード : 進めて、済んだら弾倉を満たす。空ならリロードに入る
		//==================================================================================
		a_world.ActiveJobTask<Component::WeaponReloadComponent>(
			Engine::ECS::ESystemType::Update,
			"WeaponReloadSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				Component::WeaponReloadComponent* a_reloadArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::WeaponReloadComponent& _reload = a_reloadArray[_i];
					auto& _rt = _reload.runtimeData;

					if (!_rt.isReload)
					{
						// 撃ち切ったらリロードへ(撃った瞬間にも入るが、容量を下げられたときのためにここでも見る)
						if (_reload.GetRemaining() == 0)
						{
							_rt.isReload = true;
							_rt.reloadTimer = 0.0f;
						}
						continue;
					}

					_rt.reloadTimer += a_ctx.dt;
					if (_rt.reloadTimer < _reload.reloadTime) continue;

					_rt.isReload = false;
					_rt.reloadTimer = 0.0f;
					_rt.usedCount = 0;
				}
			}
		);

		//==================================================================================
		// オーバーヒートした瞬間のエフェクト
		//
		// 出しっぱなしで武器から離れて構わない(湯気・火花の一瞬の演出)ので、
		// 武器に枠を持たせず、その場に出して自分で消えさせる。
		// 生成の予約を積むのでメインスレッドで回す
		//==================================================================================
		a_world.ActiveTask<const Component::WeaponOverHeatComponent, const Component::WorldMatrixComponent>(
			Engine::ECS::ESystemType::Update,
			"WeaponOverHeatEffectSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				const Component::WeaponOverHeatComponent* a_heatArray,
				const Component::WorldMatrixComponent* a_worldMatArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					const Component::WeaponOverHeatComponent& _heat = a_heatArray[_i];
					if (!_heat.runtimeData.isOverheatStarted) continue;
					if (_heat.heatEffect.guid == Core::DEFAULT_GUID) continue;

					const Math::Vector3 _pos = Math::Matrix(a_worldMatArray[_i].worldMat).Translation();
					App::Utility::ReserveSpawnEffectAt(
						*a_ctx.pWorld, _heat.heatEffect.guid, _pos, true, {}, _heat.heatEffect.scale);
				}
			}
		)
		// 順序 : 撃った瞬間の合図(撃ち方のシステムが立てる)を、同じフレームのうちに拾う
		.After({ "AutoFireSystem", "BurstFireSystem", "SemiAutoFireSystem" });
	}
}
