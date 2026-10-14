#include "SelfWeaponTriggerSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Combat/ActionIntentComponent.h"
#include "Application/Components/Weapon/WeaponTriggerComponent.h"

namespace App::System
{
	//==========================================================================================
	// SelfWeaponTriggerSystem
	//
	// 「持ち主の命令」を「武器への引き金」へ変える経路のうち、本体が武器を兼ねている場合を担当する。
	// 武器を子エンティティとして持つ場合は AttachmentDispatchSystem が同じことをする。
	//
	// このクエリ(ActionIntent と WeaponTrigger の両方を持つ)に引っかかるのは、
	// 自分自身に武器の部品(WeaponStateComponent と撃ち方)がある敵のようなキャラだけ。
	// 武器エンティティ側は ActionIntentComponent を持たない(命令を出す側ではないので)ため、
	// 配信された引き金をここで踏み消してしまうことはない。
	//
	// 左右の区別は付けない。両手を持たないキャラに「どちらの手か」を決めさせても意味が無いので、
	// どちらかが押されていれば引く、とする。
	//==========================================================================================
	void SelfWeaponTriggerSystem::Init(App::ECS::APPWorld& a_world)
	{
		// 自分のチャンクの値だけを書く(ほかのエンティティは RefData で読むだけ)ので、ワーカーで回す
		a_world.ActiveJobTask<const Component::ActionIntentComponent, Component::WeaponTriggerComponent>(
			Engine::ECS::ESystemType::PreUpdate,
			"SelfWeaponTriggerSystem",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::ActionIntentComponent*      a_intentArray,
				Component::WeaponTriggerComponent*           a_triggerArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					a_triggerArray[_i].isPulled = a_intentArray[_i].IsAnyWeaponShoot();
				}
			}
		)
		// 順序 : 引き金(WeaponTrigger)の書き手同士。子の武器へ配る側と、本体が武器のものへ書く側で対象は重ならない
		.After("AttachmentDispatchSystem");
	}
}
