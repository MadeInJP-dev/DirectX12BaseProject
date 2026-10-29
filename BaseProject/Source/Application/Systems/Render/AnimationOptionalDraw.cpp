#include "AnimationOptionalDraw.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"

#include "Application/Components/Animation/SkeletonPoseComponent.h"
#include "Application/Components/Render/ModelComponent.h"
#include "Application/Components/Render/DynamicRaytracingComponent.h"
#include "Application/Components/Animation/NodePoseComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"

namespace App::System
{
	void AnimationOptionalDrawSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<
			const Component::WorldMatrixComponent,
			const Component::ModelComponent, 
			const Component::SkeletonPoseComponent,
			const Component::DynamicRaytracingComponent, 
			const Component::NodePoseComponent
		>(
			Engine::ECS::ESystemType::Draw,
			"AnimationOptionalDrawSystem",
			[]
			(
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				const Component::WorldMatrixComponent* a_matArray,
				const Component::ModelComponent* a_modelArray,
				const Component::SkeletonPoseComponent* a_skeArray,
				const Component::DynamicRaytracingComponent* a_aniArray,
				const Component::NodePoseComponent* a_nodePoseArray
				)
			{
				// グラフィックスエンジン取得
				auto* _pGE = a_ctx.pServices->pMainEngine->RefGraphicsEngine();
				if (!_pGE) return;
		
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::WorldMatrixComponent& _matComp = a_matArray[_i];
					const Component::ModelComponent& _modelComp = a_modelArray[_i];
					const Component::SkeletonPoseComponent& _skeComp = a_skeArray[_i];
					const Component::NodePoseComponent& _nodePoseComp = a_nodePoseArray[_i];
					const Component::DynamicRaytracingComponent& _rayComp = a_aniArray[_i];

					// 描画
					Engine::Graphics::AnimationModelSubmitDesc _desc = {};
					_desc.pWorld = a_ctx.pWorld;
					_desc.modelHandle = _modelComp.handle;
					_desc.worldMat = _matComp.worldMat;
					_desc.boneHandle = _skeComp.skeletonPoseHandle;
					_desc.nodeposeHandle = _nodePoseComp.nodePoseHandle;
					_desc.rtAnimHandle = _rayComp.dynamicInstanceHandle;
					// 静的・動的の描画システムと同じく、コンポーネントの色設定を渡す。
					// (ここだけ既定値のままで、インスペクタの ColorScale /
					//  EmissiveScale がアニメーションモデルにだけ効いていなかった)
					_desc.colorScale = _modelComp.colorScale;
					_desc.emissiveScale = _modelComp.emissiveScale;
					_desc.bloomAdd = _modelComp.GetEmissiveAdd();
					_desc.fade = _modelComp.fade;
					_pGE->RefDrawSubmitter()->SubmitModel(_desc);
				}
			}); 
	}
}