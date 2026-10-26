#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/DoF/DoFPass/DoFPass.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/GraphicsEngine.h"

namespace Engine::Graphics::Pipeline
{
	void DoFPass::SetupSlots()
	{
		// ルートパラメータ : 0=調整値CB / 1=SRVテーブル(色, CoC) / 2=UAV
		// 同じ番号を指定した入力は、宣言した順にテーブルへ並ぶ
		DeclareInput("Color", EAccessType::SRV, EPassSlotType::Texture, true, 1);
		DeclareInput("CoC", EAccessType::SRV, EPassSlotType::Texture, true, 1);

		DeclareOutput("Result", "DoFColor", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::UAV, EPassSlotType::Texture, false, 2);
	}

	void DoFPass::Compile(const PassContext& a_context)
	{
		SetupComputeShader(a_context, "Asset/Shader/Source/PostProcess/DoF/DoFShader.cso", "DoFShader");
	}

	void DoFPass::Update(const PassContext& a_context)
	{
		if (!a_context.pRenderContext || !a_context.pCmdList) return;

		//----------------------------------------------------------------------------------
		// 調整値の出どころ
		//
		// カメラが FocusParamComponent を持っていれば、CamSetShaderSystem が
		// 毎フレーム値を送ってくる(速度に応じて動くのはこちら)。
		// 送られてこないフレームは、このパスがアセットに保存している自分の値を使う。
		//
		// カメラ側を優先しないと、演出でボケや流れが動かなくなる
		//----------------------------------------------------------------------------------
		DoFOptionCB _cb = a_context.pGraphicsEngine && a_context.pGraphicsEngine->GetSceneView()->IsDoFOverride()
			? a_context.pGraphicsEngine->GetSceneView()->GetDoFData()
			: m_cb;

		// 重ねたシーンの後ろのぼかしはカメラの値の上へ重ねる
		if (a_context.pGraphicsEngine) a_context.pGraphicsEngine->GetSceneView()->ApplyBackdropBlur(_cb);

		a_context.pRenderContext->BindCB()->BindAndAttachDataComputeRootCBV(a_context.pCmdList, 0, _cb);
		DispatchFullScreen(a_context);
	}



	void DoFPass::Archive(Engine::Persistence::Archive& a_arch)
	{
		a_arch.Field("focusDistance", m_cb.focusDistance);
		a_arch.Field("focusRange", m_cb.focusRange);
		a_arch.Field("nearRange", m_cb.nearRange);
		a_arch.Field("farRange", m_cb.farRange);
		a_arch.Field("maxBlurRadius", m_cb.maxBlurRadius);
		a_arch.Field("enable", m_cb.enable);
	}
}
