#include "AttachmentDispatchSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Attachment/AttachmentSlotsComponent.h"
#include "Application/Components/Combat/ActionIntentComponent.h"
#include "Application/Components/Combat/AimResultComponent.h"
#include "Application/Components/Weapon/WeaponTriggerComponent.h"

namespace App::System
{
	//==========================================================================================
	// AttachmentDispatchSystem
	//
	// プレイヤー(AttachmentSlotsComponent 保持者)を反復し、スロットが指す子エンティティの
	// コンポーネントへ入力を配信する。子エンティティはこのクエリには含まれないため、
	// World::RefData でエンティティ横断的に参照する(構造変更は行わないので反復中でも安全)。
	//
	// ここが渡すのは「引き金を引いているか」と「どこを狙っているか」だけ。
	// 撃てるかどうか(連射間隔・バースト・オーバーヒート)も、何をどう撃つか(弾・弾速・銃口)も
	// 武器側のコンポーネント(Hand / WeaponConfig)が持ち、撃ち方のシステム(WeaponFireModeSystem)が判断する。
	// 持ち主は左右どちらの武器を使うかしか知らない。
	//
	// ※ ブースター(移動スラスター)の噴射制御は ThrusterEffectSystem が担当する。
	//    ここでは武器系の入力配信のみを行う。
	//==========================================================================================
	void AttachmentDispatchSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<const Component::AttachmentSlotsComponent, const Component::ActionIntentComponent>(
			Engine::ECS::ESystemType::PreUpdate,
			"AttachmentDispatchSystem",
			[]
			(
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				const Component::AttachmentSlotsComponent* a_slotsArray,
				const Component::ActionIntentComponent* a_intentArray
				)
			{
				// 武器へ引き金の状態を配信
				auto _setTrigger = [&a_ctx](Engine::ECS::Entity a_e, bool a_pulled)
				{
					if (a_e == Engine::ECS::Limits::INVALID_ENTITY) return;
					if (!a_ctx.pWorld->HasComponent<Component::WeaponTriggerComponent>(a_e)) return;
					if (auto* _p = a_ctx.pWorld->RefData<Component::WeaponTriggerComponent>(a_e))
					{
						_p->isPulled = a_pulled;
					}
				};

				// 狙点(AimTargetSystem がカメラのレイで求めた着弾点)を子へ配信
				auto _setAimTarget = [&a_ctx](Engine::ECS::Entity a_e, const Component::AimResultComponent* a_pSrc)
				{
					if (!a_pSrc) return;
					if (a_e == Engine::ECS::Limits::INVALID_ENTITY) return;
					if (!a_ctx.pWorld->HasComponent<Component::AimResultComponent>(a_e)) return;
					if (auto* _p = a_ctx.pWorld->RefData<Component::AimResultComponent>(a_e))
					{
						// 結果だけを渡す(maxDistance / startOffset は子側の設定を壊さない)
						_p->pos			= a_pSrc->pos;
						_p->dir			= a_pSrc->dir;
						_p->hitEntity	= a_pSrc->hitEntity;
						_p->isHit		= a_pSrc->isHit;
						_p->isValid		= a_pSrc->isValid;
					}
				};

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::AttachmentSlotsComponent& _slots = a_slotsArray[_i];
					const Component::ActionIntentComponent& _intent = a_intentArray[_i];

					// 親の狙点(付いていないプレイヤーもあり得るので任意扱い)
					Engine::ECS::Entity _self = a_pChunk->entityData[_i];
					const Component::AimResultComponent* _pAim =
						a_ctx.pWorld->HasComponent<Component::AimResultComponent>(_self)
						? a_ctx.pWorld->RefData<Component::AimResultComponent>(_self)
						: nullptr;

					// --- 左右の武器へ配信 ---
					// スロットが空でも、片方だけでも、ここは何も気にしない。
					// 「押されている」と伝えるだけで、撃つかどうかは武器が決める
					_setTrigger(_slots.leftWeapon.id,  _intent.isLeftWeaponShoot);
					_setTrigger(_slots.rightWeapon.id, _intent.isRightWeaponShoot);

					_setAimTarget(_slots.leftWeapon.id,  _pAim);
					_setAimTarget(_slots.rightWeapon.id, _pAim);

					// missile スロットへは配信しない。ミサイルは「押している間に溜めて
					// 離すと一斉射」なので、入力を子へ流すだけでは足りない。
					// MissileSalvoSystem がプレイヤー側で溜めを持ち、離したときに
					// このスロットが指す肩の武器の MissileFireComponent へ要求を出す
				}
			}
		)
		// 絞り込みに使わない読み書き : 自分の狙点を読み、子(武器)の引き金と狙点へ RefData で配る
		.Reads<Component::AimResultComponent>()
		.Writes<Component::WeaponTriggerComponent, Component::AimResultComponent>();
	}
}
