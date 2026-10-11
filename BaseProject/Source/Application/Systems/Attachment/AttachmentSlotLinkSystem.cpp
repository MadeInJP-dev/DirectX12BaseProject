#include "AttachmentSlotLinkSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Attachment/AttachmentSlotsComponent.h"

namespace App::System
{
	void AttachmentSlotLinkSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.AwakeTask<Component::AttachmentSlotsComponent>(
			// AwakeTag を見るので Awake フェーズで回す
			Engine::ECS::ESystemType::Awake,
			"AttachmentSlotLinkSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::AwakeTag* /*a_tag*/,
				Component::AttachmentSlotsComponent* a_slotsArray
				)
			{
				// 1スロットの GUID -> id を解決。
				// GUID が空なら id は触らない(プレハブの即時生成がIDで付けたものを消さない)
				auto _resolve = [&a_ctx](Component::AttachmentSlot& a_slot)
				{
					if (a_slot.guid != Core::DEFAULT_GUID)
					{
						a_slot.id = a_ctx.pWorld->GetEntity(a_slot.guid);
					}
				};

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::ForEachAttachmentSlot(a_slotsArray[_i], _resolve);
				}
			}
		)
		// 絞り込みに使わない読み : GUID からの解決
		.Reads<Engine::ECS::GUIDComponent>();
	}
}
