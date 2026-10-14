#include "WeaponProjectileSpawnSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Prefab/Prefab.h"

#include "Application/Components/Weapon/WeaponFireComponent.h"
#include "Application/Components/Weapon/Hand/WeaponStateComponent.h"
#include "Application/Components/Combat/AimResultComponent.h"
#include "Application/Components/Combat/TargetEntityComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Components/Enemy/EnemyTag.h"

#include "ProjectileSpawn.h"

namespace App::System
{
	//==========================================================================================
	// WeaponProjectileSpawnSystem(旧 GunProjectileSpawnSystem)
	//
	// 撃ち方のシステムが「撃った」と決めた手持ちの武器(WeaponFireComponent::isFired)から、
	// WeaponStateComponent の弾を、その初速と威力で出す。位置と向きは WeaponMuzzleSystem が埋めてある。
	//
	// ・生成はシステムの反復中に即時に行えない(アーキタイプが壊れる)ため、
	//   World の遅延生成コマンドに積み、BeginFrame で生成する(ProjectileSpawn)。
	//   予約はメインスレッドで積むものなので、このシステムはメインスレッドで回す。
	// ・弾のプレハブが引けなかったフレームは isFired を下ろす。
	//   銃口の光(MuzzleFlashSystem)は弾が出たときだけ出すため(この後に読む)。
	// ・弾のプレハブが HomingComponent を持っていた場合は、ここで「追う相手」を埋める。
	//   発射した後から相手を探すのではなく、撃った瞬間に撃った側が捉えている相手を
	//   弾へ渡す形にしている(敵の索敵結果 = TargetEntityComponent がそのまま弾の的になる)。
	//   実際に曲げるのは HomingSystem。
	//==========================================================================================
	namespace
	{
		//======================================================================================
		// 撃った側が「今どのエンティティを狙っているか」を解決する
		//
		//   1) 自分 → 親 と辿って TargetEntityComponent(索敵結果)を探す。
		//      銃はアタッチメントの子エンティティになっていることがあるので、
		//      索敵している本体(敵キャラ)は親側にいる。
		//   2) 見つからなければ狙点(レティクル)が当たっている相手を使う。
		//      プレイヤーが誘導弾を撃った時はこちらが拾われる。
		//
		// 誰も狙っていなければ無効値を返す(＝誘導せずに直進する弾になる)。
		//======================================================================================
		Engine::ECS::Entity ResolveHomingTarget(
			Engine::ECS::World&          a_world,
			Engine::ECS::Entity          a_shooter,
			const Component::AimResultComponent* a_pAim)
		{
			// 親を辿る深さの上限。親子が循環していても止まるように付けておく
			constexpr int MAX_DEPTH = 8;

			Engine::ECS::Entity _entity = a_shooter;
			for (int _d = 0; _d < MAX_DEPTH; ++_d)
			{
				if (_entity == Engine::ECS::Limits::INVALID_ENTITY) break;

				if (const auto* _pTarget = a_world.RefData<Component::TargetEntityComponent>(_entity))
				{
					// 見失っている間の的は信用しない(古い位置を追ってしまうため)
					if (_pTarget->isFind && _pTarget->targetEntity != Engine::ECS::Limits::INVALID_ENTITY)
					{
						return _pTarget->targetEntity;
					}
				}

				// 親へ
				const auto* _pHierarchy = a_world.RefData<Component::HierarchyComponent>(_entity);
				if (!_pHierarchy) break;
				_entity = _pHierarchy->parentID;
			}

			// 狙点が何かに当たっているなら、それを追わせる
			if (a_pAim && a_pAim->isHit) return a_pAim->hitEntity;

			return Engine::ECS::Limits::INVALID_ENTITY;
		}
	}

	void WeaponProjectileSpawnSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<Component::WeaponStateComponent, Component::WeaponFireComponent>(
			Engine::ECS::ESystemType::Update,
			"WeaponProjectileSpawnSystem",
			[]
			(
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				Component::WeaponStateComponent* a_stateArray,
				Component::WeaponFireComponent* a_fireArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::WeaponFireComponent& _fireComp = a_fireArray[_i];
					if (!_fireComp.isFired) continue;

					Component::WeaponStateComponent& _state = a_stateArray[_i];

					// 弾が未設定なら撃たない(間隔と熱は撃ち方のシステムで進めてある)
					if (_state.bulletPrefabGUID == Core::DEFAULT_GUID)
					{
						_fireComp.isFired = false;
						continue;
					}

					// プレハブのハンドルを解決(未ロードならロード)。
					// 取った参照は武器が消えるときに返す(WeaponStateComponent の解放フック)
					auto& _rm = *a_ctx.pServices->pResourceManager;
					if (!_rm.IsValid(_state.bulletPrefabHandle))
					{
						_rm.AcquireImmediate(_state.bulletPrefabHandle, _state.bulletPrefabGUID);
					}
					auto* _pPrefab = _rm.Ref(_state.bulletPrefabHandle);
					if (!_pPrefab)
					{
						// 弾が出ないので、銃口も光らせない
						_fireComp.isFired = false;
						continue;
					}

					// 誘導先は狙点(まだ一度も計算されていなければ使わない)と索敵結果から決める
					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];
					const Component::AimResultComponent* _pAim = a_ctx.pWorld->RefData<Component::AimResultComponent>(_self);
					if (_pAim && !_pAim->isValid) _pAim = nullptr;

					// 生成はミサイルと共通のヘルパーへ(遅延生成コマンドに積まれる)
					App::System::ProjectileSpawn::Spawn(
						*a_ctx.pWorld,
						_pPrefab,
						_fireComp.spawnPos,
						_fireComp.shootDir * _state.bulletSpeed,
						App::System::ProjectileSpawn::ResolveShooterEntity(*a_ctx.pWorld, _self),
						ResolveHomingTarget(*a_ctx.pWorld, _self, _pAim),
						_state.bulletAttack);
				}
			}
		)
		// 順序 : 位置と向きが埋まった後に読み、弾が出なければ印を下ろす(どちらも WeaponFire を書く)
		.After("WeaponMuzzleSystem")
		// 絞り込みに使わない読み : 狙点・親を辿った索敵結果と発射元(ProjectileSpawn)
		.Reads<Component::AimResultComponent, Component::TargetEntityComponent, Component::HierarchyComponent, Component::ColliderComponent, Component::EnemyTag>();
	}
}
