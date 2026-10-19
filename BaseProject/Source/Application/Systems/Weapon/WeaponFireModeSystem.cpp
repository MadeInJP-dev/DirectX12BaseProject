#include "WeaponFireModeSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Weapon/WeaponTriggerComponent.h"
#include "Application/Components/Weapon/WeaponFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/AutoFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/BurstFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/SemiAutoFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/WeaponOverHeatComponent.h"
#include "Application/Components/Weapon/WeaponConfig/WeaponReloadComponent.h"
#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"

namespace App::System
{
	namespace
	{
		//==================================================================================
		// 熱と弾倉と持ち主の生死による「撃てる / 撃てない」
		//
		// どれも持っていなければ常に撃てる。
		// 持ち主(自分か親)が死んでいる間は撃たない。途中のバーストもここで打ち切る。
		// 死んだときの攻撃入力は DeathStateSystem が PreUpdate 帯で消しているが、
		// 引き金を配る側との前後が帯の中の並びしだいなので、撃つ側でも見ておく
		// 撃ち方のシステムから、自分のエンティティの分だけを RefData で触る
		// (チャンクを分けて回しても、同じエンティティを2つのワーカーが触ることはない)
		//==================================================================================
		struct FireLimit
		{
			Component::WeaponOverHeatComponent* pHeat = nullptr;
			Component::WeaponReloadComponent* pReload = nullptr;
			bool isOwnerDead = false;

			// 今撃てるか
			bool CanShoot() const
			{
				if (isOwnerDead) return false;
				if (pHeat && pHeat->runtimeData.isOverheat) return false;
				if (pReload && (pReload->runtimeData.isReload || pReload->GetRemaining() == 0)) return false;
				return true;
			}

			// 撃ったぶん熱を溜めて弾を減らす。上限に届いたら true(バーストを打ち切る合図)
			bool NotifyShot()
			{
				bool _isLimit = false;

				if (pHeat)
				{
					auto& _rt = pHeat->runtimeData;
					_rt.heat += pHeat->perShot;
					if (_rt.heat >= pHeat->limit)
					{
						_rt.heat = pHeat->limit;
						_rt.isOverheat = true;
						_rt.isOverheatStarted = true;
						_isLimit = true;
					}
				}

				if (pReload)
				{
					auto& _rt = pReload->runtimeData;
					++_rt.usedCount;
					if (pReload->GetRemaining() == 0)
					{
						_rt.isReload = true;
						_rt.reloadTimer = 0.0f;
						_isLimit = true;
					}
				}

				return _isLimit;
			}
		};

		FireLimit MakeFireLimit(Engine::ECS::World& a_world, Engine::ECS::Entity a_entity)
		{
			FireLimit _limit = {};
			_limit.pHeat = a_world.RefData<Component::WeaponOverHeatComponent>(a_entity);
			_limit.pReload = a_world.RefData<Component::WeaponReloadComponent>(a_entity);
			_limit.isOwnerDead = Component::IsDeadSelfOrOwner(a_world, a_entity);
			return _limit;
		}
	}

	void WeaponFireModeSystem::Init(App::ECS::APPWorld& a_world)
	{
		//==================================================================================
		// 連射 : 押している間、発射レートの間隔で撃ち続ける
		//==================================================================================
		a_world.ActiveJobTask<Component::AutoFireComponent, Component::WeaponFireComponent, const Component::WeaponTriggerComponent>(
			Engine::ECS::ESystemType::Update,
			"AutoFireSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				Component::AutoFireComponent* a_autoArray,
				Component::WeaponFireComponent* a_fireArray,
				const Component::WeaponTriggerComponent* a_triggerArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::AutoFireComponent& _auto = a_autoArray[_i];
					auto& _rt = _auto.runtimeData;

					// 撃たなかったフレームは下ろしておく(読み手はこの印だけを見る)
					a_fireArray[_i].isFired = false;
					_rt.timeSinceShot += a_ctx.dt;

					if (!a_triggerArray[_i].isPulled) continue;

					const float _interval = (_auto.fireRate > 0.0f) ? (1.0f / _auto.fireRate) : 0.0f;
					if (_rt.timeSinceShot < _interval) continue;

					FireLimit _limit = MakeFireLimit(*a_ctx.pWorld, a_pChunk->entityData[_i]);
					if (!_limit.CanShoot()) continue;

					_rt.timeSinceShot = 0.0f;
					_limit.NotifyShot();
					a_fireArray[_i].isFired = true;
				}
			}
		)
		// 順序 : 熱と弾倉の進行(冷却・リロード)を済ませてから判定し、撃ったぶんを書き足す
		.After({ "WeaponOverHeatSystem", "WeaponReloadSystem" })
		.Writes<Component::WeaponOverHeatComponent, Component::WeaponReloadComponent>()
		// 絞り込みに使わない読み : 持ち主の生死(IsDeadSelfOrOwner。死亡状態を書くのは PostUpdate 帯)
		.Reads<Component::HealthComponent, Component::HierarchyComponent>();

		//==================================================================================
		// バースト : 一度始まったら引き金を離しても撃ち切り、撃ち切ったら waitTime あけて次へ
		//==================================================================================
		a_world.ActiveJobTask<Component::BurstFireComponent, Component::WeaponFireComponent, const Component::WeaponTriggerComponent>(
			Engine::ECS::ESystemType::Update,
			"BurstFireSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				Component::BurstFireComponent* a_burstArray,
				Component::WeaponFireComponent* a_fireArray,
				const Component::WeaponTriggerComponent* a_triggerArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::BurstFireComponent& _burst = a_burstArray[_i];
					auto& _rt = _burst.runtimeData;

					a_fireArray[_i].isFired = false;
					_rt.timeSinceShot += a_ctx.dt;

					// 熱か弾倉が尽きている間は撃てない。途中のバーストもここで打ち切る
					FireLimit _limit = MakeFireLimit(*a_ctx.pWorld, a_pChunk->entityData[_i]);
					if (!_limit.CanShoot())
					{
						_rt.remain = 0;
						continue;
					}

					bool _isFire = false;
					if (_rt.remain > 0)
					{
						// バースト継続中。残りは burstTime 間隔で撃つ
						_isFire = (_rt.timeSinceShot >= _burst.burstTime);
					}
					else if (a_triggerArray[_i].isPulled && _rt.timeSinceShot >= _burst.waitTime)
					{
						// 次のバーストを始める(前のバーストの最後の1発からの間隔で見る)
						_rt.remain = (std::max)(_burst.burstNum, 1u);
						_isFire = true;
					}

					if (!_isFire) continue;

					_rt.timeSinceShot = 0.0f;
					--_rt.remain;
					if (_limit.NotifyShot()) _rt.remain = 0;
					a_fireArray[_i].isFired = true;
				}
			}
		)
		// 順序 : 熱と弾倉の書き手同士。撃ち方どうしは対象が重ならないが、並びを決めておく
		.After({ "WeaponOverHeatSystem", "WeaponReloadSystem", "AutoFireSystem" })
		.Writes<Component::WeaponOverHeatComponent, Component::WeaponReloadComponent>()
		// 絞り込みに使わない読み : 持ち主の生死(IsDeadSelfOrOwner。死亡状態を書くのは PostUpdate 帯)
		.Reads<Component::HealthComponent, Component::HierarchyComponent>();

		//==================================================================================
		// 単発 : 引いた瞬間に1発。撃ってから waitTime たつまでは引き直しても撃たない
		//==================================================================================
		a_world.ActiveJobTask<Component::SemiAutoFireComponent, Component::WeaponFireComponent, const Component::WeaponTriggerComponent>(
			Engine::ECS::ESystemType::Update,
			"SemiAutoFireSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				Component::SemiAutoFireComponent* a_semiArray,
				Component::WeaponFireComponent* a_fireArray,
				const Component::WeaponTriggerComponent* a_triggerArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::SemiAutoFireComponent& _semi = a_semiArray[_i];
					auto& _rt = _semi.runtimeData;

					a_fireArray[_i].isFired = false;
					_rt.timeSinceShot += a_ctx.dt;

					// 引いた瞬間だけを拾う。撃てなかった瞬間も「引いた」ことにはするので、
					// 押しっぱなしのまま撃てるようになっても撃たない(引き直しが要る)
					const bool _isPulled = a_triggerArray[_i].isPulled;
					const bool _isPressed = _isPulled && !_rt.wasPulled;
					_rt.wasPulled = _isPulled;

					if (!_isPressed) continue;
					if (_rt.timeSinceShot < _semi.waitTime) continue;

					FireLimit _limit = MakeFireLimit(*a_ctx.pWorld, a_pChunk->entityData[_i]);
					if (!_limit.CanShoot()) continue;

					_rt.timeSinceShot = 0.0f;
					_limit.NotifyShot();
					a_fireArray[_i].isFired = true;
				}
			}
		)
		.After({ "WeaponOverHeatSystem", "WeaponReloadSystem", "BurstFireSystem" })
		.Writes<Component::WeaponOverHeatComponent, Component::WeaponReloadComponent>()
		// 絞り込みに使わない読み : 持ち主の生死(IsDeadSelfOrOwner。死亡状態を書くのは PostUpdate 帯)
		.Reads<Component::HealthComponent, Component::HierarchyComponent>();
	}
}
