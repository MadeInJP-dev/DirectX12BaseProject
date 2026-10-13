#include "SkinningRegisterSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Engine/Graphics/Raytracing/RayEngine.h"

#include "Application/Components/Render/RayTag.h"
#include "Application/Components/Render/ModelComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Animation/SkeletonPoseComponent.h"
#include "Application/Components/Render/DynamicRaytracingComponent.h"
#include "Application/Components/Animation/NodePoseComponent.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"

namespace App::System
{
	void SkinningRegisterSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<
			const Component::ModelComponent, 
			const Component::WorldMatrixComponent,
			const Component::DynamicRaytracingComponent,
			const Component::NodePoseComponent,
			const Component::SkeletonPoseComponent
			>
			(
			Engine::ECS::ESystemType::Draw,
			"SkinningRegisterSystem",
			[]
			(
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_pTags*/,
				const Component::ModelComponent* a_pModelArray,
				const Component::WorldMatrixComponent* /*a_pWorldMatArray*/,
				const Component::DynamicRaytracingComponent* a_pAnimationArray,
				const Component::NodePoseComponent* a_nodePoseArray,
				const Component::SkeletonPoseComponent* a_skeletonArray
				)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::ModelComponent& _modelComp = a_pModelArray[_i];
					const Component::DynamicRaytracingComponent& _rayComp = a_pAnimationArray[_i];
					const Component::NodePoseComponent& _nodePoseComp = a_nodePoseArray[_i];
					const Component::SkeletonPoseComponent& _skePoseComp = a_skeletonArray[_i];

					auto* _pGE = a_ctx.pServices->pMainEngine->RefGraphicsEngine();
					if (!_pGE) continue;

					// GPUスキニング登録
					Engine::Graphics::GPUSkiningSubmitDesc _desc = {};
					_desc.pWorld = a_ctx.pWorld;
					_desc.modelHandle = _modelComp.handle;
					_desc.boneHandle = _skePoseComp.skeletonPoseHandle;
					_desc.nodeposeHandle = _nodePoseComp.nodePoseHandle;
					_desc.rtAnimHandle = _rayComp.dynamicInstanceHandle;
					_pGE->RefDrawSubmitter()->SubmitSkinning(_desc);
				}
			}
		);
	}
}
