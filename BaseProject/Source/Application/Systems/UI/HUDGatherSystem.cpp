#include "HUDGatherSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Input/PlayerControllTag.h"
#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Combat/LockOnTargetComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileLockComponent.h"
#include "Application/Components/Weapon/WeaponConfig/WeaponOverHeatComponent.h"
#include "Application/Components/Weapon/WeaponConfig/WeaponReloadComponent.h"
#include "Application/Components/Movement/BoostParamsComponent.h"
#include "Application/Components/Movement/BoostStateComponent.h"
#include "Application/Components/Movement/ChargeDashComponent.h"
#include "Application/Components/Attachment/AttachmentSlotsComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileFireComponent.h"
#include "Application/Components/Movement/ActualVelocityComponent.h"
#include "Application/Components/Movement/MovementParamsComponent.h"
#include "Application/Components/Physics/GroundStateComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Physics/Collider.h"

#include "Engine/Physics/PhysicsWorld.h"

#include "Application/InstanceResource/HitEventResource.h"
#include "Application/InstanceResource/PlayerHUDResource.h"

namespace App::System
{
	namespace
	{
		// 高度を測るレイの長さ(m)。これより高いと地面が無いのと同じ扱い(最大のまま出す)
		constexpr float ALTITUDE_RAY_MAX = 500.0f;

		// レイの始点を足元からこれだけ上げる(m)。足元から打つと、立っているときは地表と重なって当たらない
		constexpr float ALTITUDE_RAY_START_UP = 1.0f;

		// m/s → km/h
		constexpr float MPS_TO_KMPH = 3.6f;

		//==================================================================================
		// 操作している機体の高度と速さ
		//----------------------------------------------------------------------------------
		// 高度は真下の地面(StaticObject)までの距離。海抜ではなく対地高度にしてあるのは、
		// 起伏のある地形で「あとどれだけで着地するか」が分かる方が操作に効くため。
		//
		// 速さは実速度の大きさを km/h で出す。最大は目安の最高速(チャージダッシュがあればその速さ、
		// 無ければ移動速度)。ゲージの見え方を変えたいときは UIGauge の MaxOverride で上書きする。
		// 接地中は上下の成分を見ない(地面へ押し付ける下向きの速度が残っていて、止まっていても数字が出るため)
		//==================================================================================
		void GatherFlight(
			Engine::ECS::World* a_pWorld,
			Engine::ECS::Entity a_player,
			InstanceResource::PlayerHUDResource& a_hud)
		{
			using InstanceResource::EHUDGaugeKind;
			using InstanceResource::EHUDSubject;

			if (a_pWorld->HasComponent<Component::ActualVelocityComponent>(a_player))
			{
				if (const auto* _pVel = a_pWorld->RefData<Component::ActualVelocityComponent>(a_player))
				{
					float _maxSpeed = 0.0f;
					if (a_pWorld->HasComponent<Component::ChargeDashComponent>(a_player))
					{
						if (const auto* _pDash = a_pWorld->RefData<Component::ChargeDashComponent>(a_player))
						{
							_maxSpeed = _pDash->dashSpeed;
						}
					}
					if (_maxSpeed <= 0.0f && a_pWorld->HasComponent<Component::MovementParamsComponent>(a_player))
					{
						if (const auto* _pMove = a_pWorld->RefData<Component::MovementParamsComponent>(a_player))
						{
							_maxSpeed = _pMove->moveSpeed;
						}
					}

					Math::Vector3 _velocity = Math::Vector3(_pVel->value);
					if (a_pWorld->HasComponent<Component::GroundStateComponent>(a_player))
					{
						if (const auto* _pGround = a_pWorld->RefData<Component::GroundStateComponent>(a_player))
						{
							if (_pGround->isGround) _velocity.y = 0.0f;
						}
					}

					a_hud.RefGauge(EHUDSubject::Player, EHUDGaugeKind::Speed) = {
						_velocity.Length() * MPS_TO_KMPH, _maxSpeed * MPS_TO_KMPH, true };
				}
			}

			if (a_pWorld->HasComponent<Component::WorldMatrixComponent>(a_player) &&
				a_pWorld->HasResource<Engine::Physics::PhysicsWorld>())
			{
				if (const auto* _pWorldMat = a_pWorld->RefData<Component::WorldMatrixComponent>(a_player))
				{
					Math::Ray _ray;
					_ray.origin = Math::Matrix(_pWorldMat->worldMat).Translation();
					_ray.origin.y += ALTITUDE_RAY_START_UP;
					_ray.direction = { 0.0f, -1.0f, 0.0f };
					_ray.maxDistance = ALTITUDE_RAY_MAX + ALTITUDE_RAY_START_UP;

					float _altitude = ALTITUDE_RAY_MAX;
					Engine::Physics::RayHit _hit = {};
					const auto& _physicsWorld = a_pWorld->GetResource<Engine::Physics::PhysicsWorld>();
					if (_physicsWorld.CastRay(_ray, static_cast<uint32_t>(Component::ECollisionLayer::StaticObject), a_player, _hit))
					{
						_altitude = std::max(_hit.distance - ALTITUDE_RAY_START_UP, 0.0f);
					}

					a_hud.RefGauge(EHUDSubject::Player, EHUDGaugeKind::Altitude) = { _altitude, ALTITUDE_RAY_MAX, true };
				}
			}
		}

		//==================================================================================
		// 1体ぶんのゲージの値を集める
		//----------------------------------------------------------------------------------
		// 持っていないコンポーネントのぶんは isValid が false のまま残る。
		// 「HPを出すゲージにロック中の敵を入れたが、その敵はHPを持っていない」
		// といった組み合わせでも、出す側は何も出さないだけで済む
		//==================================================================================
		void GatherGauges(
			Engine::ECS::World* a_pWorld,
			Engine::ECS::Entity a_entity,
			InstanceResource::PlayerHUDResource& a_hud,
			InstanceResource::EHUDSubject a_subject)
		{
			using InstanceResource::EHUDGaugeKind;

			if (a_entity == Engine::ECS::Limits::INVALID_ENTITY) return;
			if (!a_pWorld->IsAliveEntity(a_entity)) return;

			if (a_pWorld->HasComponent<Component::HealthComponent>(a_entity))
			{
				if (const auto* _pHealth = a_pWorld->RefData<Component::HealthComponent>(a_entity))
				{
					a_hud.RefGauge(a_subject, EHUDGaugeKind::Health) = { _pHealth->currentHealth, _pHealth->maxHealth, true };
				}
			}

			if (a_pWorld->HasComponent<Component::BoostParamsComponent>(a_entity) &&
				a_pWorld->HasComponent<Component::BoostStateComponent>(a_entity))
			{
				const auto* _pParams = a_pWorld->RefData<Component::BoostParamsComponent>(a_entity);
				const auto* _pState = a_pWorld->RefData<Component::BoostStateComponent>(a_entity);
				if (_pParams && _pState)
				{
					a_hud.RefGauge(a_subject, EHUDGaugeKind::BoostFuel) = { _pState->currentFuel, _pParams->maxFuel, true };
				}
			}

			if (a_pWorld->HasComponent<Component::WeaponOverHeatComponent>(a_entity))
			{
				// 熱は「溜まるほど満タン」。色のしきい値も溜まった側で読むことになるので、
				// ゲージ側では危険色を残量の大きい側へ置くこと
				if (const auto* _pHeat = a_pWorld->RefData<Component::WeaponOverHeatComponent>(a_entity))
				{
					a_hud.RefGauge(a_subject, EHUDGaugeKind::Overheat) = { _pHeat->runtimeData.heat, _pHeat->limit, true };
				}
			}

			if (a_pWorld->HasComponent<Component::WeaponReloadComponent>(a_entity))
			{
				// 弾倉は「残弾 / 容量」。リロード中は進み具合で満ちていくように見せる
				if (const auto* _pReload = a_pWorld->RefData<Component::WeaponReloadComponent>(a_entity))
				{
					const float _capacity = static_cast<float>(_pReload->magazineMaxCapacity);
					const float _current = _pReload->runtimeData.isReload && _pReload->reloadTime > 0.0f
						? _capacity * std::clamp(_pReload->runtimeData.reloadTimer / _pReload->reloadTime, 0.0f, 1.0f)
						: static_cast<float>(_pReload->GetRemaining());
					a_hud.RefGauge(a_subject, EHUDGaugeKind::Magazine) = { _current, _capacity, true };
				}
			}

			if (a_pWorld->HasComponent<Component::ChargeDashComponent>(a_entity))
			{
				if (const auto* _pDash = a_pWorld->RefData<Component::ChargeDashComponent>(a_entity))
				{
					a_hud.RefGauge(a_subject, EHUDGaugeKind::ChargeDash) = { _pDash->chargeTimer, _pDash->chargeTime, true };
				}
			}

			if (a_pWorld->HasComponent<Component::MissileFireComponent>(a_entity))
			{
				// 次を撃てるまでの溜まり具合。撃ち切ってから waitTime かけて満ちていき、満タンで撃てる。
				// 一斉射の最中はまだ待ちが始まっていないので空にしておく
				if (const auto* _pFire = a_pWorld->RefData<Component::MissileFireComponent>(a_entity))
				{
					const float _wait = std::max(_pFire->waitTime, 0.0f);
					float _current = _wait;
					if (_pFire->IsFiring())			_current = 0.0f;
					else if (_pFire->IsWaiting())	_current = std::clamp(_wait - _pFire->runtimeData.waitTimer, 0.0f, _wait);

					// 待ち時間の無い武器は常に満タン(最大が 0 だと空に見えるので 1 で割る)
					a_hud.RefGauge(a_subject, EHUDGaugeKind::FireInterval) = (_wait > 0.0f)
						? InstanceResource::HUDGaugeValue{ _current, _wait, true }
						: InstanceResource::HUDGaugeValue{ _pFire->IsFiring() ? 0.0f : 1.0f, 1.0f, true };
				}
			}
		}
	}

	//==========================================================================================
	// HUDGatherSystem
	//
	// HUD が出す値を毎フレーム集めて PlayerHUDResource へ置く。HUD はそこを読むだけにする。
	//
	// ・プレイヤーを探すのはここ1か所。以前は HUD ごとに ForEach で探し直していた
	//   (TargetBoxHUD / MissileLockBoxHUD / HitEffectHUD / UIGauge …)。
	//
	// ・PostUpdate に置く理由
	//     集める元は LockOnTargetSystem / MissileSalvoSystem(PostUpdate)、
	//     HealthSystem などが書く体力、当たり判定が積むヒットの一覧。
	//     どれも読むと宣言してあるので、書く側の後ろへ自動で回る(RAW の辺)。
	//     HUD(GameObjectManager::Update)は PostUpdate の後なので、同じフレームの値が読める。
	//
	// ・ここは読むだけで、ゲームの状態は書かない。書くのは PlayerHUDResource だけ。
	//
	// ・カスタムタスクなのは、プレイヤー1体を起点に別エンティティ(ロック中の敵・武器)を
	//   引くため。チャンクごとに呼ばれる普通のタスクでは組みにくい。
	//==========================================================================================
	void HUDGatherSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveCustomTask(
			Engine::ECS::ESystemType::PostUpdate,
			"HUDGatherSystem",
			Engine::ECS::ReadList<
				Component::PlayerControllTag,
				Component::LockOnTargetComponent,
				Component::MissileLockComponent,
				Component::AttachmentSlotsComponent,
				Component::HealthComponent,
				Component::BoostParamsComponent,
				Component::BoostStateComponent,
				Component::WeaponOverHeatComponent,
				Component::WeaponReloadComponent,
				Component::ChargeDashComponent,
				Component::MissileFireComponent,
				Component::ActualVelocityComponent,
				Component::MovementParamsComponent,
				Component::GroundStateComponent,
				Component::WorldMatrixComponent>{},
			Engine::ECS::WriteList<>{},
			[](const Engine::ECS::SystemContext& a_ctx)
			{
				Engine::ECS::World* _pWorld = a_ctx.pWorld;
				if (!_pWorld) return;
				if (!_pWorld->HasResource<InstanceResource::PlayerHUDResource>()) return;

				auto& _hud = _pWorld->RefResource<InstanceResource::PlayerHUDResource>();
				_hud.ClearFrame();

				//==================================================================
				// 操作しているプレイヤー(1体の想定。先に見つかったものを使う)
				//==================================================================
				_pWorld->ForEach<const Component::ActiveTag, const Component::PlayerControllTag>(
					[&_hud](
						Engine::ECS::Chunk* a_pChunk,
						uint32_t a_count,
						const Component::ActiveTag* /*a_activeTagArray*/,
						const Component::PlayerControllTag* /*a_playerTagArray*/
					)
					{
						if (_hud.HasPlayer() || a_count == 0) return;
						_hud.player = a_pChunk->entityData[0];
					}
				);

				if (!_hud.HasPlayer()) return;

				const Engine::ECS::Entity _player = _hud.player;
				Engine::ECS::Entity _lockedEnemy = Engine::ECS::Limits::INVALID_ENTITY;

				//==================================================================
				// 銃のロックオン
				//------------------------------------------------------------------
				// 射影もレティクルの内外判定も LockOnTargetSystem が済ませてある。
				// HUD で射影をやり直すと、条件のわずかな差で
				// 「枠は出ているのにロックされない」ズレが起きるので、結果をそのまま渡す
				//==================================================================
				if (_pWorld->HasComponent<Component::LockOnTargetComponent>(_player))
				{
					if (const auto* _pLockOn = _pWorld->RefData<Component::LockOnTargetComponent>(_player))
					{
						_hud.aimReticle = { Math::Vector2(_pLockOn->reticleCenter), _pLockOn->reticleRadius, true };

						const int _count = std::clamp(
							_pLockOn->targetCount, 0, InstanceResource::PlayerHUDResource::TARGET_MAX);

						for (int _i = 0; _i < _count; ++_i)
						{
							InstanceResource::HUDTargetMark& _mark = _hud.targets[_i];
							_mark.screenPos = Math::Vector2(_pLockOn->screenPos[_i]);
							_mark.isLocked = _pLockOn->IsLocked() && _pLockOn->targets[_i] == _pLockOn->lockedEntity;
						}
						_hud.targetCount = _count;

						if (_pLockOn->IsLocked())
						{
							_hud.isLocked = true;
							_hud.lockedScreenPos = Math::Vector2(_pLockOn->lockedScreenPos);
							_lockedEnemy = _pLockOn->lockedEntity;
						}
					}
				}

				//==================================================================
				// ミサイルの溜め(MissileSalvoSystem の結果)
				//==================================================================
				if (_pWorld->HasComponent<Component::MissileLockComponent>(_player))
				{
					if (const auto* _pMissile = _pWorld->RefData<Component::MissileLockComponent>(_player))
					{
						// 出すのは見た目の円。判定はこれに reticleScale を掛けたもの
						_hud.missileReticle = { Math::Vector2(_pMissile->reticleCenter), _pMissile->reticleRadius, true };
						_hud.isMissileCharging = _pMissile->IsAnyCharging();

						// 左右の肩で溜めた敵をまとめて並べる(枠を出すのは溜めている肩のぶんだけ)
						int _count = 0;
						for (const auto& _side : _pMissile->sides)
						{
							if (!_side.isCharging) continue;
							for (int _i = 0; _i < _side.lockCount; ++_i)
							{
								if (_count >= InstanceResource::PlayerHUDResource::MISSILE_LOCK_MAX) break;
								_hud.missileLockScreenPos[_count++] = Math::Vector2(_side.lockScreenPos[_i]);
							}
						}
						_hud.missileLockCount = _count;
					}
				}

				//==================================================================
				// ゲージ : プレイヤー / ロック中の敵 / 左右の武器 / 左右の肩の武器
				//------------------------------------------------------------------
				// 武器はアタッチメントスロットが指す別のエンティティ。
				// 熱(オーバーヒート)は武器の側が持っているので、そこまで辿る
				//==================================================================
				Engine::ECS::Entity _rightWeapon = Engine::ECS::Limits::INVALID_ENTITY;
				Engine::ECS::Entity _leftWeapon = Engine::ECS::Limits::INVALID_ENTITY;
				Engine::ECS::Entity _rightShoulderWeapon = Engine::ECS::Limits::INVALID_ENTITY;
				Engine::ECS::Entity _leftShoulderWeapon = Engine::ECS::Limits::INVALID_ENTITY;

				if (_pWorld->HasComponent<Component::AttachmentSlotsComponent>(_player))
				{
					if (const auto* _pSlots = _pWorld->RefData<Component::AttachmentSlotsComponent>(_player))
					{
						_rightWeapon = _pSlots->rightWeapon.id;
						_leftWeapon = _pSlots->leftWeapon.id;
						_rightShoulderWeapon = _pSlots->missile.id;
						_leftShoulderWeapon = _pSlots->leftMissile.id;
					}
				}

				GatherGauges(_pWorld, _player, _hud, InstanceResource::EHUDSubject::Player);
				GatherGauges(_pWorld, _lockedEnemy, _hud, InstanceResource::EHUDSubject::LockedEnemy);
				GatherGauges(_pWorld, _rightWeapon, _hud, InstanceResource::EHUDSubject::RightWeapon);
				GatherGauges(_pWorld, _leftWeapon, _hud, InstanceResource::EHUDSubject::LeftWeapon);
				GatherGauges(_pWorld, _rightShoulderWeapon, _hud, InstanceResource::EHUDSubject::RightShoulderWeapon);
				GatherGauges(_pWorld, _leftShoulderWeapon, _hud, InstanceResource::EHUDSubject::LeftShoulderWeapon);

				// 高度と速さ(高度は真下へレイを打つ)
				GatherFlight(_pWorld, _player, _hud);

				//==================================================================
				// 手応え : プレイヤーの弾が当たったか
				//------------------------------------------------------------------
				// 「自分が撃った弾か」は HitEvent.shooter(弾を撃った本体)で見る。
				// attacker は弾そのものなので、そちらでは判定できない。
				//
				// 数えるのはダメージが通る相手(HealthComponent を持つもの)に当てたときだけ。
				// 壁や地面に当てても手応えが出ると、当たった合図として意味を成さないため。
				// 同じフレームに何発当たっても進めるのは1回
				//==================================================================
				if (_pWorld->HasResource<InstanceResource::HitEventResource>())
				{
					const auto& _hitEvents = _pWorld->GetResource<InstanceResource::HitEventResource>();

					for (const InstanceResource::HitEvent& _event : _hitEvents.events)
					{
						if (_event.shooter != _player) continue;
						if (_event.victim == Engine::ECS::Limits::INVALID_ENTITY) continue;
						if (!_pWorld->HasComponent<Component::HealthComponent>(_event.victim)) continue;

						++_hud.hitSerial;
						break;
					}
				}
			}
		)
		// ヒットの一覧を読む(積むのは当たり判定系、消すのは次フレームの HitEventClearSystem)
		.ReadsResource<InstanceResource::HitEventResource>()
		// 高度を測るレイを撃つ
		.ReadsResource<Engine::Physics::PhysicsWorld>()
		// 集めた答えの置き場
		.WritesResource<InstanceResource::PlayerHUDResource>();
	}
}
