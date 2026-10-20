#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/ScreenSpaceLensFlarePass/ScreenSpaceLensFlarePass.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/GraphicsEngine.h"	// プレイヤーの画面設定

namespace Engine::Graphics::Pipeline
{
	void ScreenSpaceLensFlarePass::SetupSlots()
	{
		// ルートパラメータ : 0=調整値CB / 1=SRVテーブル(色, ブルーム) / 2=UAV
		DeclareInput("Color", EAccessType::SRV, EPassSlotType::Texture, true, 1);
		DeclareInput("Bloom", EAccessType::SRV, EPassSlotType::Texture, true, 1);

		// 全画素を書き潰すのでクリアは不要。
		// トーンマップ前なので、メインカラーと同じHDRで受ける
		DeclareOutput("Result", "ScreenSpaceLensFlareColor", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::UAV, EPassSlotType::Texture, false, 2);
	}

	void ScreenSpaceLensFlarePass::Compile(const PassContext& a_context)
	{
		SetupComputeShader(a_context, "Asset/Shader/Source/PostProcess/Bloom/ScreenSpaceLensFlareCS.cso", "ScreenSpaceLensFlareCS");
	}

	void ScreenSpaceLensFlarePass::Update(const PassContext& a_context)
	{
		if (!a_context.pRenderContext || !a_context.pCmdList) return;

		// ブルームから作るので、プレイヤーがブルームを切っていれば掛けない。
		// 持ち物(m_cb)は作り手の値のまま残す
		ScreenSpaceLensFlareCB _cb = m_cb;
		if (a_context.pGraphicsEngine && !a_context.pGraphicsEngine->IsUserBloomEnable()) _cb.enable = 0;

		a_context.pRenderContext->BindCB()->BindAndAttachDataComputeRootCBV(a_context.pCmdList, 0, _cb);
		DispatchFullScreen(a_context);
	}



	void ScreenSpaceLensFlarePass::Archive(Engine::Persistence::Archive& a_arch)
	{
		a_arch.Field("intensity", m_cb.intensity);
		a_arch.Field("enable", m_cb.enable);
	}
}
