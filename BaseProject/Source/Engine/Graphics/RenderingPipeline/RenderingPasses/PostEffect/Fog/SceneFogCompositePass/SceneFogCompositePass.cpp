#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Fog/SceneFogCompositePass/SceneFogCompositePass.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/SceneView/CameraData.h"

namespace Engine::Graphics::Pipeline
{
	void SceneFogCompositePass::SetupSlots()
	{
		// 同じ番号を指定した入力は、宣言した順にルート定数へ並ぶ
		// メインカラー(トーンマップ前)
		DeclareInput("Color", EAccessType::SRV, EPassSlotType::Texture, true, ROOT_INPUT_SRV);
		// フォグ(SceneVolumetricFogPass の出力)
		DeclareInput("Fog", EAccessType::SRV, EPassSlotType::Texture, true, ROOT_INPUT_SRV);
		// シーンの深度(任意) : フォグが縮小解像度のとき、深度を見ながら引き伸ばす。
		// フォグのパスの SceneDepth と同じものを繋ぐ
		DeclareInput("SceneDepth", EAccessType::SRV, EPassSlotType::Texture, false, ROOT_INPUT_SRV);

		DeclareOutput("Result", "SceneFogCompositeColor", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::UAV, EPassSlotType::Texture, false, ROOT_OUTPUT_UAV);
	}

	void SceneFogCompositePass::Compile(const PassContext& a_context)
	{
		SetupComputeShader(a_context, "Asset/Shader/Source/PostProcess/Fog/SceneFogCompositeCS.cso", "SceneFogCompositeCS");
	}

	void SceneFogCompositePass::Update(const PassContext& a_context)
	{
		if (!a_context.pRenderContext || !a_context.pCmdList || !a_context.pGraphicsEngine) return;

		const SceneView* _pSceneView = a_context.pGraphicsEngine->GetSceneView();

		// 合成の設定はシーンの持ち物。受け取ったものをそのまま送る
		a_context.pRenderContext->BindCB()->BindAndAttachDataComputeRootCBV(
			a_context.pCmdList, ROOT_COMPOSITE_CB, _pSceneView->GetSceneFogCompositeData());

		// カメラ : 深度を奥行きへ戻す(引き伸ばすときの重み)
		a_context.pRenderContext->BindCB()->BindAndAttachDataComputeRootCBV<CameraData>(
			a_context.pCmdList, ROOT_CAMERA_CB, _pSceneView->GetCameraData());

		DispatchFullScreen(a_context);
	}



	void SceneFogCompositePass::Archive(Engine::Persistence::Archive& a_arch)
	{
		// 値はシーンの持ち物なので、パスとして保存するものは無い
		(void)a_arch;
	}
}
