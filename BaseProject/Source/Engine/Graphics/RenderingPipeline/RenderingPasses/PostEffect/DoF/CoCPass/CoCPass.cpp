#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/DoF/CoCPass/CoCPass.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/SceneView/CameraData.h"
#include "Engine/Graphics/Frame/SceneView/SkyData.h"

namespace Engine::Graphics::Pipeline
{
	void CoCPass::SetupSlots()
	{
		// ルートパラメータ : 0=カメラCB / 1=DoF調整値CB / 2=SRVテーブル / 3=UAV / 4=スカイCB
		DeclareInput("Depth", EAccessType::SRV, EPassSlotType::Texture, true, 2);

		DeclareOutput("CoC", "CoC", DXGI_FORMAT_R16_FLOAT,
			EAccessType::UAV, EPassSlotType::Texture, false, 3);
	}

	void CoCPass::Compile(const PassContext& a_context)
	{
		SetupComputeShader(a_context, "Asset/Shader/Source/PostProcess/DoF/CoCShader.cso", "CoCShader");
	}

	void CoCPass::Update(const PassContext& a_context)
	{
		if (!a_context.pRenderContext || !a_context.pCmdList) return;
		if (!a_context.pGraphicsEngine) return;

		auto* _pCB = a_context.pRenderContext->BindCB();

		// カメラとスカイはエンジンの持ち物。パスの調整値ではないのでそのまま引く
		_pCB->BindAndAttachDataComputeRootCBV<CameraData>(a_context.pCmdList, 0, a_context.pGraphicsEngine->GetSceneView()->GetCameraData());
		//----------------------------------------------------------------------------------
		// 調整値の出どころ
		//
		// カメラが FocusParamComponent を持っていれば、CamSetShaderSystem が
		// 毎フレーム値を送ってくる(速度に応じて動くのはこちら)。
		// 送られてこないフレームは、このパスがアセットに保存している自分の値を使う。
		//
		// カメラ側を優先しないと、演出でボケや流れが動かなくなる
		//----------------------------------------------------------------------------------
		DoFOptionCB _cb = a_context.pGraphicsEngine->GetSceneView()->IsDoFOverride()
			? a_context.pGraphicsEngine->GetSceneView()->GetDoFData()
			: m_cb;

		// 重ねたシーンの後ろのぼかしはカメラの値の上へ重ねる
		a_context.pGraphicsEngine->GetSceneView()->ApplyBackdropBlur(_cb);

		_pCB->BindAndAttachDataComputeRootCBV(a_context.pCmdList, 1, _cb);
		_pCB->BindAndAttachDataComputeRootCBV<SkyData>(a_context.pCmdList, 4, a_context.pGraphicsEngine->GetSceneView()->GetSkyData());

		DispatchFullScreen(a_context);
	}



	void CoCPass::Archive(Engine::Persistence::Archive& a_arch)
	{
		a_arch.Field("focusDistance", m_cb.focusDistance);
		a_arch.Field("focusRange", m_cb.focusRange);
		a_arch.Field("nearRange", m_cb.nearRange);
		a_arch.Field("farRange", m_cb.farRange);
		a_arch.Field("maxBlurRadius", m_cb.maxBlurRadius);
		a_arch.Field("enable", m_cb.enable);
	}
}
