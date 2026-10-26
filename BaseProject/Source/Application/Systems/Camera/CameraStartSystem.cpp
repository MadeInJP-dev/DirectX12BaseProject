#include "CameraStartSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Camera/ProjMatComponent.h"
#include "Application/Components/Camera/CameraParamComponent.h"

#include "Application/Components/Core/PhaseTag/StartTag.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"

#include "Engine/Graphics/GraphicsEngine.h"

namespace App::System
{
	void CameraStartSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.StartTask<Component::CameraParamComponent,Component::ProjMatComponent>(
			Engine::ECS::ESystemType::Start,
			"CameraStartSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::StartTag* /*a_startTag*/,
				Component::CameraParamComponent* a_camParamArray,
				Component::ProjMatComponent* a_projMatArray
			)
			{
				// アスペクトはウィンドウ設定から取る(持ち主はエンジン。サービスで受け取る)
				if (!a_ctx.pServices || !a_ctx.pServices->pMainEngine) return;
				const auto* _pGraphicsEngine = a_ctx.pServices->pMainEngine->RefGraphicsEngine();

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::CameraParamComponent& _camParamComp = a_camParamArray[_i];
					Component::ProjMatComponent& _projMatComp = a_projMatArray[_i];

					// カメラパラメーターの初期化
					_camParamComp.aspectRatio = static_cast<float>(_pGraphicsEngine->GetRenderWidth()) / static_cast<float>(_pGraphicsEngine->GetRenderHeight());

					// プロジェクション行列の作成
					_projMatComp.projMat = Math::Matrix::CreatePerspectiveFieldOfView(
						DirectX::XMConvertToRadians(_camParamComp.GetFovY()),
						_camParamComp.aspectRatio,
						_camParamComp.nearZ,
						_camParamComp.farZ
					);
				}
			}
		);
	}
}
