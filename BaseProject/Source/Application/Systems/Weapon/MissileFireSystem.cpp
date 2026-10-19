#include "MissileFireSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Prefab/Prefab.h"

#include "Application/Components/Weapon/WeaponConfig/MuzzleComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileStateComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileFireComponent.h"
#include "Application/Components/Render/ModelComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Components/Enemy/EnemyTag.h"
#include "Application/Components/Combat/AimPointComponent.h"
#include "Application/Components/Combat/HealthComponent.h"

#include "ProjectileSpawn.h"
#include "WeaponMuzzleSystem.h"

namespace App::System
{
	namespace
	{
		//==================================================================================
		// 一斉射の弾を散らす向きを作る
		//
		// 基準方向を軸にしたコーンの側面へ、発射順で回しながら並べる。
		// 散らしたあとは HomingSystem が相手へ寄せるので、
		// 「いったん広がってから食いつく」いつもの見た目になる。
		//==================================================================================
		Math::Vector3 MakeSpreadDir(
			const Math::Vector3& a_baseDir,
			float                a_spreadRad,
			int                  a_index,
			int                  a_total)
		{
			if (a_spreadRad <= 0.0f || a_total <= 0) return a_baseDir;

			// 基準方向に直交する基底を作る。真上を向いている時だけ参照軸を変える
			const Math::Vector3 _ref = (std::fabs(a_baseDir.y) > 0.99f)
				? Math::Vector3(1.0f, 0.0f, 0.0f)
				: Math::Vector3(0.0f, 1.0f, 0.0f);

			Math::Vector3 _right = _ref.Cross(a_baseDir);
			if (_right.LengthSquared() <= 1e-8f) return a_baseDir;
			_right.Normalize();

			Math::Vector3 _up = a_baseDir.Cross(_right);
			_up.Normalize();

			// コーンの周りを等間隔に回す
			const float _azimuth = DirectX::XM_2PI * static_cast<float>(a_index) / static_cast<float>(a_total);

			Math::Vector3 _dir =
				a_baseDir * std::cos(a_spreadRad) +
				(_right * std::cos(_azimuth) + _up * std::sin(_azimuth)) * std::sin(a_spreadRad);

			_dir.Normalize();
			return _dir;
		}

		//==================================================================================
		// a_index 発目の発射位置(銃口からのずらし。武器ローカル)
		//
		// 段数で発射数を割り、1段ぶんを firstPos → endPos の横(X / Z)に等間隔で並べ、
		// 段を firstPos.y → endPos.y の縦に並べる。1段なら Y も横並びと一緒に動く。
		//==================================================================================
		Math::Vector3 MakeLaunchOffset(const Component::MissileFireComponent& a_fire, int a_index, int a_total)
		{
			if (a_total <= 0) return a_fire.firstPos;

			const int _rows = std::clamp(static_cast<int>(a_fire.columnNum), 1, a_total);
			const int _perRow = (a_total + _rows - 1) / _rows;

			const int _row = a_index / _perRow;
			const int _col = a_index % _perRow;

			const float _tCol = (_perRow > 1) ? static_cast<float>(_col) / static_cast<float>(_perRow - 1) : 0.0f;
			const float _tRow = (_rows > 1) ? static_cast<float>(_row) / static_cast<float>(_rows - 1) : 0.0f;

			Math::Vector3 _offset = Math::Vector3::Lerp(a_fire.firstPos, a_fire.endPos, _tCol);
			if (_rows > 1)
			{
				_offset.y = a_fire.firstPos.y + (a_fire.endPos.y - a_fire.firstPos.y) * _tRow;
			}
			return _offset;
		}
	}

	void MissileFireSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<Component::MissileFireComponent, Component::MissileStateComponent,
			const Component::MuzzleComponent, const Component::WorldMatrixComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"MissileFireSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				Component::MissileFireComponent* a_fireArray,
				Component::MissileStateComponent* a_stateArray,
				const Component::MuzzleComponent* a_muzzleArray,
				const Component::WorldMatrixComponent* a_worldMatArray
			)
			{
				auto& _rm = *a_ctx.pServices->pResourceManager;

				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::MissileFireComponent& _fire = a_fireArray[_i];
					Component::MissileStateComponent& _state = a_stateArray[_i];
					auto& _rt = _fire.runtimeData;

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					// 撃ち切った後の待ち時間を進める
					if (_rt.waitTimer > 0.0f)
					{
						_rt.waitTimer = (std::max)(_rt.waitTimer - a_ctx.dt, 0.0f);
					}

					// 持ち主(肩の武器なら機体、本体が撃つならその本体)が死んでいる間は撃たない。
					// 要求は捨て、撃っている途中の一斉射もここで止める(残りは出さない)
					if (Component::IsDeadSelfOrOwner(*a_ctx.pWorld, _self))
					{
						_fire.request.isRequested = false;
						continue;
					}

					//==============================================================
					// 要求を受ける
					//--------------------------------------------------------------
					// 要求は成否に関わらず1回で下ろす。撃てない間(発射中・待ち時間中)の分を
					// 溜めておくと、明けた瞬間に立て続けに撃ってしまうため。
					// 出す側は IsReady を見てから出すので、普段はここで捨てることはない
					//==============================================================
					if (_fire.request.isRequested)
					{
						auto& _req = _fire.request;
						_req.isRequested = false;

						const int _total = _fire.GetActiveBurstNum();
						if (!_fire.IsFiring() && !_fire.IsWaiting() && _total > 0)
						{
							// 狙う相手へ順番に割り振る。足りなければ先頭から使い回す
							const int _targetCount = std::clamp(_req.targetCount, 0, Component::MissileFireComponent::MISSILE_MAX);
							for (int _m = 0; _m < _total; ++_m)
							{
								_rt.fireTargets[_m] = (_targetCount > 0)
									? _req.targets[_m % _targetCount]
									: Engine::ECS::Limits::INVALID_ENTITY;
							}

							_rt.aimDir = _req.aimDir;
							_rt.targetOffsetY = _req.targetOffsetY;
							_rt.fireTotal = _total;
							_rt.fireCount = 0;
							_rt.burstTimer = 0.0f;
						}
					}

					if (!_fire.IsFiring()) continue;

					//==============================================================
					// ミサイルのプレハブ
					//--------------------------------------------------------------
					// 未設定なら撃てないので、この一斉射は撃ち切ったことにして待ちへ入る。
					// 読み込みが間に合わないフレームは撃たずに次を待つ
					//==============================================================
					if (_state.missilePrefabGUID == Core::DEFAULT_GUID)
					{
						_rt.fireCount = _rt.fireTotal;
						_rt.waitTimer = (std::max)(_fire.waitTime, 0.0f);
						continue;
					}

					// 取った参照は武器が消えるときに返す(MissileStateComponent の解放フック)
					if (!_rm.IsValid(_state.missilePrefabHandle))
					{
						_rm.AcquireImmediate(_state.missilePrefabHandle, _state.missilePrefabGUID);
					}
					auto* _pPrefab = _rm.Ref(_state.missilePrefabHandle);
					if (!_pPrefab) continue;

					//==============================================================
					// 間隔を進めて、撃てるぶんを撃つ
					//   isBurst が false、または burstTime が 0 なら同じフレームに全弾出る
					//==============================================================
					_rt.burstTimer -= a_ctx.dt;
					if (_fire.isBurst && _rt.burstTimer > 0.0f) continue;

					const Math::Matrix _weaponMat = a_worldMatArray[_i].worldMat;
					const Math::Vector3 _muzzleLocalPos = WeaponMuzzle::GetMuzzleLocalPos(
						_rm, a_muzzleArray[_i], a_ctx.pWorld->RefData<Component::ModelComponent>(_self));

					// 狙いの向きが潰れていたら武器の前方で代用する
					Math::Vector3 _aimDir = _rt.aimDir;
					if (_aimDir.LengthSquared() > 1e-8f) _aimDir.Normalize();
					else                                 _aimDir = Math::Vector3(_weaponMat._31, _weaponMat._32, _weaponMat._33);
					if (_aimDir.LengthSquared() > 1e-8f) _aimDir.Normalize();
					else                                 _aimDir = Math::Vector3(0.0f, 0.0f, 1.0f);

					const float _spreadRad = DirectX::XMConvertToRadians((std::max)(_fire.spreadAngle, 0.0f));
					const Engine::ECS::Entity _shooter = ProjectileSpawn::ResolveShooterEntity(*a_ctx.pWorld, _self);

					while (_fire.IsFiring() && (!_fire.isBurst || _rt.burstTimer <= 0.0f))
					{
						const int _index = _rt.fireCount;

						// 発射位置 : 銃口を基準に並べる
						const Math::Vector3 _localPos = _muzzleLocalPos + MakeLaunchOffset(_fire, _index, _rt.fireTotal);
						const Math::Vector3 _spawnPos = Math::Vector3::Transform(_localPos, _weaponMat);

						// 相手が居るならそちらを基準に、居なければ狙いの向きを基準に散らす
						const Engine::ECS::Entity _target = _rt.fireTargets[_index];
						Math::Vector3 _baseDir = _aimDir;
						if (_target != Engine::ECS::Limits::INVALID_ENTITY &&
							a_ctx.pWorld->IsAliveEntity(_target) &&
							a_ctx.pWorld->HasComponent<Component::WorldMatrixComponent>(_target))
						{
							if (const auto* _pTargetWorld = a_ctx.pWorld->RefData<Component::WorldMatrixComponent>(_target))
							{
								// 相手の狙ってほしい点(AimPointComponent。無ければ原点)から、撃つ側の分だけ上へずらす
								Math::Vector3 _targetPos = Component::GetAimPointPos(
									*a_ctx.pWorld, _target, Math::Matrix(_pTargetWorld->worldMat));
								_targetPos.y += _rt.targetOffsetY;

								Math::Vector3 _toTarget = _targetPos - _spawnPos;
								if (_toTarget.LengthSquared() > 1e-6f)
								{
									_toTarget.Normalize();
									_baseDir = _toTarget;
								}
							}
						}

						const Math::Vector3 _shootDir = MakeSpreadDir(_baseDir, _spreadRad, _index, _rt.fireTotal);

						ProjectileSpawn::Spawn(
							*a_ctx.pWorld,
							_pPrefab,
							_spawnPos,
							_shootDir * _state.missileSpeed,
							_shooter,
							_target,
							_state.missileAttack);

						++_rt.fireCount;
						_rt.burstTimer += (std::max)(_fire.burstTime, 0.0f);
					}

					// 撃ち切ったら次の要求まで待つ
					if (!_fire.IsFiring())
					{
						_rt.waitTimer = (std::max)(_fire.waitTime, 0.0f);
					}
				}
			}
		)
		// 順序 : 要求を出す側(プレイヤー・ボス)の後で受け取る(どちらも MissileFire を書く)
		.After({ "MissileSalvoSystem", "BossMissileSalvoSystem" })
		// 絞り込みに使わない読み : 自分のモデル(銃口ノード)・狙う相手の位置・発射元(ProjectileSpawn)
		.Reads<Component::ModelComponent, Component::WorldMatrixComponent, Component::ColliderComponent, Component::HierarchyComponent, Component::EnemyTag, Component::AimPointComponent>()
		// 持ち主の生死(IsDeadSelfOrOwner)
		.Reads<Component::HealthComponent>();
	}
}
