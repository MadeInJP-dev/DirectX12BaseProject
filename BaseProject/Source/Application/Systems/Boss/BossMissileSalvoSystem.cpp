#include "BossMissileSalvoSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Boss/BossParamsComponent.h"
#include "Application/Components/Combat/TargetEntityComponent.h"
#include "Application/Components/Attachment/AttachmentSlotsComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileLockComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileFireComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"

namespace App::System
{
	//==========================================================================================
	// BossMissileSalvoSystem
	//
	// ボスのミサイルを一斉射する。プレイヤー側(MissileSalvoSystem)との違いは溜め方だけ。
	//
	//   プレイヤー … ミサイルキーを押している間、コンバットレティクルの円に入った敵を
	//                 スクリーン座標で拾って溜め、離した瞬間に撃つ
	//   ボス       … 狙う相手は最初からプレイヤー1体に決まっているので溜めが要らない。
	//                 BossCombatIntentSystem が間隔で立てる要求(isMissileRequest)を受けて、
	//                 全弾をその1体へ割り振る
	//
	// 撃ち出しは肩の武器の側(MissileFireSystem)で、プレイヤーとまったく同じ。
	// ここは要求を出すだけ。弾数・間隔・散らし角・待ち時間は肩の武器の MissileFireComponent、
	// 狙う高さ(targetOffsetY)は MissileLockComponent を使うので、ボス専用の設定項目は無い。
	//
	// ・PostUpdate に置く理由
	//     相手の「今フレームの」位置から狙いの向きを作るため(プレイヤー側と同じ帯)。
	//==========================================================================================
	void BossMissileSalvoSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<
			Component::BossCommandComponent,
			const Component::MissileLockComponent,
			const Component::AttachmentSlotsComponent,
			const Component::TargetEntityComponent,
			const Component::WorldMatrixComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"BossMissileSalvoSystem",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				Component::BossCommandComponent*             a_commandArray,
				const Component::MissileLockComponent*       a_missileArray,
				const Component::AttachmentSlotsComponent*   a_slotsArray,
				const Component::TargetEntityComponent*      a_targetArray,
				const Component::WorldMatrixComponent*       a_worldMatArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::BossCommandComponent&           _command   = a_commandArray[_i];
					const Component::MissileLockComponent&     _missile   = a_missileArray[_i];
					const Component::AttachmentSlotsComponent& _slots     = a_slotsArray[_i];
					const Component::TargetEntityComponent&    _target    = a_targetArray[_i];
					const Component::WorldMatrixComponent&     _selfWorld = a_worldMatArray[_i];

					const Math::Matrix& _selfMat = _selfWorld.worldMat;
					const Math::Vector3 _selfPos = { _selfMat._41, _selfMat._42, _selfMat._43 };

					//==========================================================
					// 相手の位置
					//==========================================================
					const Engine::ECS::Entity _targetEntity = _target.targetEntity;

					bool          _hasTarget = false;
					Math::Vector3 _targetPos = {};

					if (_targetEntity != Engine::ECS::Limits::INVALID_ENTITY &&
						a_ctx.pWorld->HasComponent<Component::WorldMatrixComponent>(_targetEntity))
					{
						if (const auto* _pTargetWorld =
							a_ctx.pWorld->RefData<Component::WorldMatrixComponent>(_targetEntity))
						{
							_targetPos = Math::Matrix(_pTargetWorld->worldMat).Translation();
							_targetPos.y += _missile.targetOffsetY;
							_hasTarget = true;
						}
					}

					//==========================================================
					// 肩の武器へ要求を出す
					//----------------------------------------------------------
					// 命令は成否に関わらず1回で消す。撃てない状況(発射中/待ち時間中)
					// のぶんを溜めておくと、明けた瞬間に立て続けに撃ってしまうため。
					// 次の間隔でまた命令が立つので取りこぼしにはならない。
					//==========================================================
					if (!_command.isMissileRequest) continue;
					_command.isMissileRequest = false;

					if (!_hasTarget) continue;

					const Engine::ECS::Entity _pod = _slots.missile.id;
					if (_pod == Engine::ECS::Limits::INVALID_ENTITY || !a_ctx.pWorld->IsAliveEntity(_pod)) continue;

					auto* _pPodFire = a_ctx.pWorld->RefData<Component::MissileFireComponent>(_pod);
					if (!_pPodFire || !_pPodFire->IsReady()) continue;

					// 相手は1体なので全弾が同じ的を追う。散らし角のぶんだけ
					// いったん広がってから、HomingSystem が寄せてくる
					auto& _req = _pPodFire->request;
					_req.targets[0] = _targetEntity;
					_req.targetCount = 1;
					_req.targetOffsetY = _missile.targetOffsetY;
					_req.aimDir = _targetPos - _selfPos;
					_req.isRequested = true;
				}
			}
		)
		// 順序 : 肩の武器への要求の書き手同士(プレイヤーとボスで対象は重ならない)
		.After("MissileSalvoSystem")
		// 絞り込みに使わない読み書き : 狙う相手の位置を読み、肩の武器へ要求を書く
		.Reads<Component::WorldMatrixComponent>()
		.Writes<Component::MissileFireComponent>();
	}
}
