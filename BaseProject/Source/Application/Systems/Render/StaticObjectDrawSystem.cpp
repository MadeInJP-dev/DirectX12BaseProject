#include "StaticObjectDrawSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"

#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Transform/PreviousWorldMatrixComponent.h"
#include "Application/Components/Render/ModelComponent.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"

#include "Application/Components/Animation/AnimatorComponent.h"

namespace App::System
{
	void StaticObjectDrawSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<const Component::WorldMatrixComponent, const Component::ModelComponent>(
			Engine::ECS::ESystemType::Draw,
			"StaticObjectDrawSystem",
			[]
			(
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				const Component::WorldMatrixComponent* a_worldMatArray,
				const Component::ModelComponent* a_modelArray
				)
			{
				// グラフィックスエンジン取得
				auto* _pGE = a_ctx.pServices->pMainEngine->RefGraphicsEngine();
				if (!_pGE) return;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::WorldMatrixComponent& _worldMatComp = a_worldMatArray[_i];
					const Component::ModelComponent& _modelComp = a_modelArray[_i];

					// 描画
					Engine::Graphics::ModelSubmitDesc _desc = {};
					_desc.pWorld = a_ctx.pWorld;
					_desc.modelHandle = _modelComp.handle;
					_desc.worldMat = _worldMatComp.worldMat;
					_desc.colorScale = _modelComp.colorScale;
					_desc.emissiveScale = _modelComp.emissiveScale;
					_desc.bloomAdd = _modelComp.GetEmissiveAdd();
					_desc.fade = _modelComp.fade;
					_pGE->RefDrawSubmitter()->SubmitModel(_desc);
				}
			},
			Engine::ECS::Exclude<Component::AnimatorComponent, Component::PreviousWorldMatrixComponent>()
		);
	}
}
