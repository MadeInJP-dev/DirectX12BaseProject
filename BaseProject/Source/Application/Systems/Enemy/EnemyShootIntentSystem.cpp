#include "EnemyShootIntentSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Enemy/EnemyTag.h"
#include "Application/Components/Combat/TargetEntityComponent.h"
#include "Application/Components/Boss/BossParamsComponent.h"
#include "Application/Components/Enemy/CloseCombatComponent.h"
#include "Application/Components/Combat/ActionIntentComponent.h"

namespace App::System
{
	//==============================================================================
	// EnemyShootIntentSystem
	//
	// 敵の発射入力を作る。プレイヤー側の InputActionSystem(キー入力 → Intent)と
	// 同じ役割を、索敵結果(SearchPlayerSystem が書く isInAttackRange)で置き換えたもの。
	//
	// ・撃つのは「攻撃可能距離の内側」だけ。発見しただけ(isFind)ではまだ撃たず、
	//   EnemyMoveIntentSystem の追従で攻撃圏まで詰めてから撃ち始める。
	//   距離の設定は TargetEntityComponent(detectDistance / attackDistance)。
	// ・撃つ間隔(連射レート)や単発/フルオート、オーバーヒートは武器側の部品(WeaponConfig)に任せる。
	//   ここが作るのは「引き金を引きたいか」だけ。
	// ・書き込み先は自分の ActionIntentComponent。銃を子エンティティとして持つ場合は
	//   AttachmentDispatchSystem が同じ PreUpdate 帯で子の WeaponTriggerComponent へ配信する
	//   (このシステムが ActionIntent を書き、あちらが読むので実行順は自動で決まる)。
	// ・敵本体が直接武器の部品(WeaponStateComponent と撃ち方)を持っていてもよい。その場合は
	//   SelfWeaponTriggerSystem が自分の WeaponTriggerComponent へ渡す。
	// ・左右の区別は付けず両方に同じ値を入れる。片手しか持たない敵に
	//   「どちらの手で撃つか」を決めさせても意味が無いため。
	// ・ボスは除外する。ボスは距離ではなく戦闘開始命令で撃ち始め、撃つ/休むのリズムも
	//   自分で持つので、BossCombatIntentSystem が同じ ActionIntentComponent を書く。
	//   どちらも書き手になると実行順が登録順頼みになり、撃つ/撃たないが安定しない。
	// ・近距離型(CloseCombatComponent 保持者)も除外する。
	//   あちらは撃つ/動くを交互に切り替えるので、撃つかどうかは間合いだけでは決まらない。
	//   CloseCombatIntentSystem が同じ ActionIntentComponent を書くため、
	//   両方が書き手になると登録順しだいで撃ちっぱなしになる。
	//==============================================================================
	void EnemyShootIntentSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveJobTask<const Component::EnemyTag, const Component::TargetEntityComponent, Component::ActionIntentComponent>(
			Engine::ECS::ESystemType::PreUpdate,
			"EnemyShootIntentSystem",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag*                        /*a_activeTags*/,
				const Component::EnemyTag*                   /*a_enemyTags*/,
				const Component::TargetEntityComponent*      a_targetArray,
				Component::ActionIntentComponent*            a_intentArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::TargetEntityComponent& _target = a_targetArray[_i];
					Component::ActionIntentComponent&       _intent = a_intentArray[_i];

					// 攻撃圏の内側にいる間だけ撃つ。離されたらその時点で止める
					const bool _shoot =
						_target.isInAttackRange &&
						_target.targetEntity != Engine::ECS::Limits::INVALID_ENTITY;

					_intent.isLeftWeaponShoot  = _shoot;
					_intent.isRightWeaponShoot = _shoot;
				}
			},
			Engine::ECS::Exclude<Component::BossParamsComponent, Component::CloseCombatComponent>{}
		);
	}
}
