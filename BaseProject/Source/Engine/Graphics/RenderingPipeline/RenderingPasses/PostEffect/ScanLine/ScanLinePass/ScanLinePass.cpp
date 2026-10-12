#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/ScanLine/ScanLinePass/ScanLinePass.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"

namespace Engine::Graphics::Pipeline
{
	namespace
	{
		// 走査線の定数
		// ※ HLSL 側(Asset/Shader/Source/PostProcess/ScanLine/ScanLineCS.hlsl の ScanLine)と並びを合わせること
		struct ScanLineCB
		{
			float time;					// パスが回り始めてからの経過時間(秒)
			float opacityScanLine;		// 走査線の濃さ
			float opacityNoise;			// ノイズの濃さ
			float flickeringSpeed;		// 明滅の速さ

			float flickeringStrength;	// 明滅の強さ
			int   enable;				// 0 ならそのまま通す
			int   enableScanLine;		// 0 なら走査線を掛けない
			int   enableNoise;			// 0 ならノイズを掛けない

			int   enableFlickering;		// 0 なら明滅させない
			float pad0;
			float pad1;
			float pad2;
		};
	}

	void ScanLinePass::SetupSlots()
	{
		DeclareInput("Color", EAccessType::SRV, EPassSlotType::Texture, true, ROOT_COLOR_SRV);

		// 掛ける範囲(UIPass の ScanLineMask。任意) : r が 1 のところだけに掛ける。
		// 繋がないと画面全体に掛かる
		DeclareInput("Mask", EAccessType::SRV, EPassSlotType::Texture, false, ROOT_COLOR_SRV);

		DeclareOutput("Result", "ScanLineColor", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::UAV, EPassSlotType::Texture, false, ROOT_OUTPUT_UAV);
	}

	void ScanLinePass::Compile(const PassContext& a_context)
	{
		SetupComputeShader(a_context, "Asset/Shader/Source/PostProcess/ScanLine/ScanLineCS.cso", "ScanLineCS");
	}

	void ScanLinePass::Update(const PassContext& a_context)
	{
		if (!a_context.pRenderContext || !a_context.pCmdList) return;

		// ノイズと明滅を動かす経過時間
		m_elapsedTime += MainEngine::Instance().GetDeltaTime();

		ScanLineCB _cb = {};
		_cb.time = m_elapsedTime;
		_cb.opacityScanLine = m_params.opacityScanLine;
		_cb.opacityNoise = m_params.opacityNoise;
		_cb.flickeringSpeed = m_params.flickeringSpeed;
		_cb.flickeringStrength = m_params.flickeringStrength;
		_cb.enable = m_params.enable;
		_cb.enableScanLine = m_params.enableScanLine;
		_cb.enableNoise = m_params.enableNoise;
		_cb.enableFlickering = m_params.enableFlickering;

		a_context.pRenderContext->BindCB()->BindAndAttachDataComputeRootCBV(a_context.pCmdList, ROOT_SCAN_LINE_CB, _cb);
		DispatchFullScreen(a_context);
	}



	void ScanLinePass::Archive(Engine::Persistence::Archive& a_arch)
	{
		a_arch.Field("opacityScanLine", m_params.opacityScanLine);
		a_arch.Field("opacityNoise", m_params.opacityNoise);
		a_arch.Field("flickeringSpeed", m_params.flickeringSpeed);
		a_arch.Field("flickeringStrength", m_params.flickeringStrength);
		a_arch.Field("enable", m_params.enable);

		// 後から足した値。古いパイプラインには無いので、読めなければ既定値(有効)のまま
		a_arch.Field("enableScanLine", m_params.enableScanLine);
		a_arch.Field("enableNoise", m_params.enableNoise);
		a_arch.Field("enableFlickering", m_params.enableFlickering);
	}
}
