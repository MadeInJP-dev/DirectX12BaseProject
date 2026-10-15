#include "MissileSalvoSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Option/OptionManager.h"

#include "Application/Components/Enemy/EnemyTag.h"
#include "Application/Components/Camera/ProjMatComponent.h"
#include "Application/Components/Combat/ActionIntentComponent.h"
#include "Application/Components/Combat/AimResultComponent.h"
#include "Application/Components/Attachment/AttachmentSlotsComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileLockComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileFireComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"

#include "Application/InstanceResource/SingletonEntityResource.h"

#include "Application/Components/Boss/BossParamsComponent.h"


//==========================================================================================
// MissileSalvoSystem
//
// ミサイルキー(ActionIntentComponent::isMissileHold)を
//   押している間 … コンバットレティクルの円に入った敵を MissileLockComponent へ溜める
//   離した瞬間   … 溜めた相手を、肩の武器(右肩はスロットの missile、左肩は leftMissile)の MissileFireComponent へ要求として渡す
// 右肩(isMissileHold / E)と左肩(isLeftMissileHold / Q)は別々に溜めて撃つ。
// 実際に撃つのは武器の側(MissileFireSystem)。何発を・どの間隔で撃つかも武器が持つ。
//
// ・PostUpdate に置く理由
//     収集はスクリーン座標で行うので、カメラと敵の「今フレームの」ワールド行列が要る。
//     WorldMatrixComponent を読むので CalcMatrixSystem /
//     CommitHierarchyWorldMatrixSystem より自動的に後ろへ回る(LockOnTargetSystem と同じ)。
// ・溜められる数は肩の武器の発射数まで。武器が付いていない・撃っている最中・待ち時間中は溜めない
//   (撃ち終わる前に溜め直せてしまうため)。
// ・ボスは溜め方が違う(相手が最初からプレイヤー1体に決まっている)ので除外する。
//==========================================================================================

namespace App::System
{
	void MissileSalvoSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<Component::MissileLockComponent, const Component::AttachmentSlotsComponent,
			const Component::ActionIntentComponent, const Component::WorldMatrixComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"MissileSalvoSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				Component::MissileLockComponent* a_missileArray,
				const Component::AttachmentSlotsComponent* a_slotsArray,
				const Component::ActionIntentComponent* a_intentArray,
				const Component::WorldMatrixComponent* a_worldMatArray
			)
			{
				if (a_count == 0) return;
				if (!a_ctx.pServices || !a_ctx.pServices->pOptionManager) return;

				// スクリーン解像度(px) : HUD の SubmitUI がピクセル座標を解釈する基準と合わせる
				const auto& _winOp = a_ctx.pServices->pOptionManager->GetWindowOption();
				const float _w = static_cast<float>(_winOp.windowWidth);
				const float _h = static_cast<float>(_winOp.windowHeight);
				if (_w <= 0.0f || _h <= 0.0f) return;

				//==================================================================
				// アクティブカメラから ViewProj を作る(LockOnTargetSystem と同じ)
				//==================================================================
				Math::Matrix _viewProjMat = Math::Matrix::Identity();
				bool _hasCamera = false;

				// どのカメラで映しているかは MainCameraSystem が決めて
				// SingletonEntityResource へ置いてあるので、ここでは探さずに引くだけ
				if (a_ctx.pWorld->HasResource<InstanceResource::SingletonEntityResource>())
				{
					const Engine::ECS::Entity _camera =
						a_ctx.pWorld->RefResource<InstanceResource::SingletonEntityResource>().mainCamera;

					if (_camera != Engine::ECS::Limits::INVALID_ENTITY &&
						a_ctx.pWorld->IsAliveEntity(_camera) &&
						a_ctx.pWorld->HasComponent<Component::ProjMatComponent>(_camera) &&
						a_ctx.pWorld->HasComponent<Component::WorldMatrixComponent>(_camera))
					{
						const auto* _pProjMat  = a_ctx.pWorld->RefData<Component::ProjMatComponent>(_camera);
						const auto* _pWorldMat = a_ctx.pWorld->RefData<Component::WorldMatrixComponent>(_camera);

						if (_pProjMat && _pWorldMat)
						{
							const Math::Matrix _camWorldMat = _pWorldMat->worldMat;

							_viewProjMat = _camWorldMat.Invert() * _pProjMat->projMat;
							_hasCamera   = true;
						}
					}
				}

				const Math::Vector2 _defaultCenter = { _w * 0.5f, _h * 0.5f };

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::MissileLockComponent&           _missile   = a_missileArray[_i];
					const Component::AttachmentSlotsComponent& _slots     = a_slotsArray[_i];
					const Component::ActionIntentComponent&    _intent    = a_intentArray[_i];
					const Component::WorldMatrixComponent&     _selfWorld = a_worldMatArray[_i];

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					// 収集の円は画面中央。中心は HUD がレティクルを出す位置にもなるので書いておく
					// (以前は CombatReticleHUD が円を書き込んでいたが、
					//  それだと HUD を置き忘れたシーンで判定が変わるので、こちらを正解にした)
					_missile.reticleCenter = _defaultCenter;

					//==============================================================
					// 肩ごとに溜めて撃つ。右肩(E)と左肩(Q)は別々に溜め、別々の武器へ要求を出す。
					// 判定の円・距離・狙う高さは左右で同じ
					//==============================================================
					for (int _s = 0; _s < Component::MissileLockComponent::SIDE_COUNT; ++_s)
					{
						const bool _isRight = (_s == Component::MissileLockComponent::RightShoulder);
						Component::MissileLockComponent::Side& _side = _missile.sides[_s];

						// 肩の武器(右肩 = スロットの missile / 左肩 = leftMissile)。付いていなければ溜めも発射もしない
						Component::MissileFireComponent* _pPodFire = nullptr;
						const Engine::ECS::Entity _pod = _isRight ? _slots.missile.id : _slots.leftMissile.id;
						if (_pod != Engine::ECS::Limits::INVALID_ENTITY && a_ctx.pWorld->IsAliveEntity(_pod))
						{
							_pPodFire = a_ctx.pWorld->RefData<Component::MissileFireComponent>(_pod);
						}
						const bool _isPodReady = _pPodFire && _pPodFire->IsReady();

						const Math::Matrix& _pm = _selfWorld.worldMat;
						const Math::Vector3 _selfPos = { _pm._41, _pm._42, _pm._43 };

						//==============================================================
						// 1. ターゲットの収集
						//--------------------------------------------------------------
						// 押している間だけ溜める。発射中とクールダウン中は次の収集を
						// 始めさせない(撃ち終わる前に溜め直せてしまうため)
						//==============================================================
						// 押し離しの検出は入力そのもので見る。溜められるかどうか(_canCharge)を
						// 混ぜると、クールダウンに入った次のフレームが「離した」扱いになってしまう
						const bool _rawHold = _isRight ? _intent.isMissileHold : _intent.isLeftMissileHold;
						const bool _canCharge = _isPodReady && _hasCamera;
						const bool _isHold = _rawHold && _canCharge;

						if (_isHold)
						{
							_side.isCharging = true;

							// 判定の円 : 見た目の半径(reticleRadius)× 判定の倍率(reticleScale)
							const Math::Vector2 _reticleCenter = _missile.reticleCenter;
							const float _reticleRadius = _missile.GetActiveReticleRadius();
							const int   _lockMax       = _pPodFire->GetActiveBurstNum();

							//----------------------------------------------------------
							// 消滅した敵をロックから外す(前へ詰める)
							//----------------------------------------------------------
							{
								int _write = 0;
								for (int _r = 0; _r < _side.lockCount; ++_r)
								{
									const Engine::ECS::Entity _e = _side.locks[_r];
									if (_e == Engine::ECS::Limits::INVALID_ENTITY) continue;
									if (!a_ctx.pWorld->HasComponent<Component::WorldMatrixComponent>(_e)) continue;

									_side.locks[_write]         = _e;
									_side.lockScreenPos[_write] = _side.lockScreenPos[_r];
									++_write;
								}
								_side.lockCount = _write;
							}

							//----------------------------------------------------------
							// 円の内側に居る敵を溜める
							// 一度ロックした相手は円から出ても外さない(溜める操作なので)
							//----------------------------------------------------------
							a_ctx.pWorld->ForEach<const Component::ActiveTag, const Component::EnemyTag, const Component::WorldMatrixComponent>(
								[&](
									Engine::ECS::Chunk* a_pEnemyChunk,
									uint32_t a_enemyCount,
									const Component::ActiveTag* /*a_activeTagArray*/,
									const Component::EnemyTag* /*a_enemyTagArray*/,
									const Component::WorldMatrixComponent* a_enemyWorldMatArray
								)
								{
									for (uint32_t _e = 0; _e < a_enemyCount; ++_e)
									{
										const Math::Matrix& _em = a_enemyWorldMatArray[_e].worldMat;
										Math::Vector3 _worldPos = { _em._41, _em._42, _em._43 };
										_worldPos.y += _missile.targetOffsetY;

										// 距離で足切り(0 以下なら距離では切らない)
										if (_missile.maxDistance > 0.0f)
										{
											if ((_worldPos - _selfPos).Length() > _missile.maxDistance) continue;
										}

										// ワールド → スクリーン
										Math::Vector4 _clipPos = Math::Vector4::Transform(_worldPos, _viewProjMat);

										// w <= 0 はカメラ後方。割ると画面内へ折り返して映るので弾く
										if (_clipPos.w <= 1e-4f) continue;

										const float _invW = 1.0f / _clipPos.w;
										const Math::Vector3 _ndc = {
											_clipPos.x * _invW, _clipPos.y * _invW, _clipPos.z * _invW };

										// ニア/ファーの外は狙わない(DirectXの深度は0..1)
										if (_ndc.z < 0.0f || _ndc.z > 1.0f) continue;

										// NDC(中心原点/Y上向き) → ピクセル(左上原点/Y下向き)
										const Math::Vector2 _screenPos = {
											(_ndc.x * 0.5f + 0.5f) * _w,
											(0.5f - _ndc.y * 0.5f) * _h
										};

										// レティクルの内側か
										if ((_screenPos - _reticleCenter).Length() > _reticleRadius) continue;

										// すでにロック済みなら座標だけ更新する(HUD 用)
										const Engine::ECS::Entity _entity = a_pEnemyChunk->entityData[_e];
										bool _found = false;
										for (int _l = 0; _l < _side.lockCount; ++_l)
										{
											if (_side.locks[_l] != _entity) continue;
											_side.lockScreenPos[_l] = _screenPos;
											_found = true;
											break;
										}
										if (_found) continue;

										// 新規ロック(1体につき1つ。弾数ぶん溜まったら打ち止め)
										if (_side.lockCount >= _lockMax) continue;

										_side.locks[_side.lockCount]         = _entity;
										_side.lockScreenPos[_side.lockCount] = _screenPos;
										++_side.lockCount;
									}
								}
							);
						}
						else
						{
							_side.isCharging = false;
						}

						//==============================================================
						// 2. 離した瞬間 : 肩の武器へ要求を出す
						//--------------------------------------------------------------
						// 溜めた相手を渡すだけ。何発をどの相手へ割り振るか(足りなければ使い回す)は
						// 武器の側が決める。1つもロックできていない時は、既定では誘導なしで撃つ
						// (requireLock を立てると撃たない)。
						//==============================================================
						const bool _released = _side.wasHold && !_rawHold;
						_side.wasHold = _rawHold;

						if (!_released) continue;

						if (_isPodReady && (!_missile.requireLock || _side.lockCount > 0))
						{
							auto& _req = _pPodFire->request;
							const int _lockCount = std::clamp(_side.lockCount, 0, Component::MissileLockComponent::MISSILE_MAX);
							for (int _l = 0; _l < _lockCount; ++_l)
							{
								_req.targets[_l] = _side.locks[_l];
							}
							_req.targetCount = _lockCount;
							_req.targetOffsetY = _missile.targetOffsetY;

							//----------------------------------------------------------
							// ロックした相手が居ない弾が飛ぶ向き。
							//   狙点(カメラのレイ)があればそちら、無ければ自機の前方(+Z)。
							//   相手が居る弾は、武器がその相手への向きを基準に散らす。
							//----------------------------------------------------------
							Math::Vector3 _aimDir = { _pm._31, _pm._32, _pm._33 };	// 左手系 +Z = 前方
							if (const auto* _pAim = a_ctx.pWorld->RefData<Component::AimResultComponent>(_self))
							{
								if (_pAim->isValid)
								{
									Math::Vector3 _d(_pAim->dir);
									if (_d.LengthSquared() > 1e-8f) _aimDir = _d;
								}
							}
							_req.aimDir = _aimDir;
							_req.isRequested = true;
						}

						// 溜めたぶんは撃つ / 撃たないに関わらず捨てる
						_side.lockCount  = 0;
						_side.isCharging = false;
					}
				}
			},
			Engine::ECS::Exclude<Component::BossParamsComponent>{}
		)
		// 絞り込みに使わない読み書き : カメラ・敵の一覧と位置・狙点を読み、肩の武器へ要求を書く
		.Reads<Component::ProjMatComponent, Component::WorldMatrixComponent, Component::EnemyTag, Component::AimResultComponent>()
		.Writes<Component::MissileFireComponent>()
		.ReadsResource<InstanceResource::SingletonEntityResource>();
	}
}
