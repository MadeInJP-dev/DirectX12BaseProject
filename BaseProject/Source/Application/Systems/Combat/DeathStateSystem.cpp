#include "DeathStateSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Boss/BossParamsComponent.h"
#include "Application/Components/Movement/BoostIntentComponent.h"
#include "Application/Components/Movement/MoveIntentComponent.h"
#include "Application/Components/Combat/ActionIntentComponent.h"

namespace App::System
{
	//==============================================================================
	// DeathStateSystem
	//
	// 「死んだ」から「消える」までの間を受け持つ。
	//
	// 体力が尽きた時点で消してしまうと、死亡を読む側(死亡エフェクトを出す
	// EffectEventSystem など)が本人のコンポーネントを引けない。
	// そこで HealthSystem は死亡状態にするだけにして、実際に消すのはここが
	// releaseDelay 秒あとに行う。そのあいだ死体は動かないようにする。
	//
	// タスクは分かれているが、どれも「死んでいるか」しか見ていない。
	//
	//   [Update] 入力/AIの結果を握りつぶす
	//       意図を作るのは Input 帯(プレイヤー)と PreUpdate 帯(敵・ボス)なので、
	//       Update 帯で消せば作り手がどれでも後から潰せる。
	//       消費側(CharacterMovementSystem / RobotBoostSystem / BossMissileSalvoSystem)は
	//       ここが書いたものを読む側になるため、依存の向きだけで自動的に後ろへ並ぶ。
	//       ただし攻撃入力だけは PreUpdate 帯で消す(銃へ配られるのが PreUpdate 帯のため)。
	//       移動系は「移動入力 × 速度」で水平速度を毎フレーム上書きするので、
	//       入力を消せば水平方向は止まる(重力はそのまま = その場に落ちる)。
	//       向きを変えないのは旋回系(LockOnRotation / FaceTarget / LookAround)が
	//       IsDeadEntity を見て自分で止める。
	//
	//   [PostUpdate] 時間を進めて解放予約する
	//       releaseDelay を過ぎたら ReserveReleaseEntity。解放予約したエンティティは
	//       次の BeginFrame で ActiveTag が外れるので、このタスクは二度と当たらない。
	//
	// ※ 死亡状態そのものを別コンポーネント(DeadTag 等)にしなかったのは、
	//   ランタイムの ReserveAddComponent が「アーキタイプの引っ越し + PostDeserialize からやり直し」に
	//   なるため。初期化系(StateMachineFixupSystem など)が死ぬたびに走り直してしまう。
	//==============================================================================
	void DeathStateSystem::Init(App::ECS::APPWorld& a_world)
	{
		//--------------------------------------------------------------------------
		// [Update] 死んでいるあいだの移動入力を消す
		//--------------------------------------------------------------------------
		a_world.ActiveTask<const Component::HealthComponent, Component::MoveIntentComponent>(
			Engine::ECS::ESystemType::Update,
			"DeathMoveIntentGateSystem",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::HealthComponent*            a_healthArray,
				Component::MoveIntentComponent*              a_intentArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					if (!a_healthArray[_i].isDead) continue;

					Component::MoveIntentComponent& _intent = a_intentArray[_i];
					_intent.value   = { 0.0f, 0.0f, 0.0f };
					_intent.jumpPow = 0.0f;
				}
			}
		);

		//--------------------------------------------------------------------------
		// [PreUpdate] 死んでいるあいだの攻撃入力を消す
		//
		// 攻撃入力は同じ PreUpdate 帯で武器の引き金(WeaponTriggerComponent)へ配られ、
		// 銃(撃ち方のシステム)はそちらを読む。Update 帯で消していた頃は配った後だったので、
		// 死んでいる間も銃を撃ち続けていた。
		// 入力を作る側(敵・ボス・近距離型)の後、配る側(AttachmentDispatch / SelfWeaponTrigger)の前で消す。
		// 配る側はここが書いた ActionIntent を読むので、読み書きで自動的に後ろへ並ぶ
		//--------------------------------------------------------------------------
		a_world.ActiveTask<const Component::HealthComponent, Component::ActionIntentComponent>(
			Engine::ECS::ESystemType::PreUpdate,
			"DeathActionIntentGateSystem",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::HealthComponent*            a_healthArray,
				Component::ActionIntentComponent*            a_intentArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					if (!a_healthArray[_i].isDead) continue;

					Component::ActionIntentComponent& _intent = a_intentArray[_i];
					_intent.isLeftWeaponShoot  = false;
					_intent.isRightWeaponShoot = false;
					_intent.isMissileHold      = false;
					_intent.isLeftMissileHold = false;
				}
			}
		)
		// 順序 : 攻撃入力(ActionIntent)の書き手同士。入力を作る側の後に消す
		.After({ "EnemyShootIntentSystem", "CloseCombatIntentSystem", "BossCombatIntentSystem" });

		//--------------------------------------------------------------------------
		// [Update] 死んでいるあいだのブーストを止める
		//
		// ブーストは移動入力とは別系統(RobotBoostSystem が推力に変える)なので、
		// MoveIntent を消しただけでは飛び続けてしまう
		//--------------------------------------------------------------------------
		a_world.ActiveTask<const Component::HealthComponent, Component::BoostIntentComponent>(
			Engine::ECS::ESystemType::Update,
			"DeathBoostGateSystem",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::HealthComponent*            a_healthArray,
				Component::BoostIntentComponent*             a_boostArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					if (!a_healthArray[_i].isDead) continue;

					Component::BoostIntentComponent& _boost = a_boostArray[_i];
					_boost.isBoostTriger = false;
					_boost.isBoostIntent = false;
				}
			}
		);

		//--------------------------------------------------------------------------
		// [Update] 死んでいるあいだのボスの一斉射要求を消す
		//
		// ボスのミサイルは ActionIntent ではなく BossParamsComponent 側の要求フラグで飛ぶ。
		// 消費するのは PostUpdate の BossMissileSalvoSystem
		//--------------------------------------------------------------------------
		a_world.ActiveTask<const Component::HealthComponent, Component::BossBrainStateComponent, Component::BossCommandComponent>(
			Engine::ECS::ESystemType::Update,
			"DeathBossOrderGateSystem",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::HealthComponent*            a_healthArray,
				Component::BossBrainStateComponent*          a_brainArray,
				Component::BossCommandComponent*             a_commandArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					if (!a_healthArray[_i].isDead) continue;

					a_commandArray[_i].isMissileRequest = false;
					a_brainArray[_i].isGunActive        = false;
				}
			}
		);

		//--------------------------------------------------------------------------
		// [PostUpdate] 死亡してからの時間を進め、尽きたら解放予約する
		//--------------------------------------------------------------------------
		a_world.ActiveTask<Component::HealthComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"DeathReleaseSystem",
			[](
				Engine::ECS::Chunk*      a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				Component::HealthComponent*                  a_healthArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::HealthComponent& _health = a_healthArray[_i];

					if (!_health.isDead) continue;

					_health.deathTimer += a_ctx.dt;
					if (_health.deathTimer < _health.releaseDelay) continue;

					// 借りているもの(ポーズ行列・ボイスなど)を Release フェーズで
					// 返してから消すため、直接消さずに解放予約を通す
					a_ctx.pWorld->ReserveReleaseEntity(a_pChunk->entityData[_i]);
				}
			}
		)
		// 順序 : 体力(HealthComponent)の書き手同士。死亡状態にする HealthSystem の後で時間を進める
		// (以前は登録順で後ろに並ぶ想定だったが、実際には段の都合で先に走っていた)
		.After("HealthSystem");
	}
}
