#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/DamageEffect/DamageEffectPass/DamageEffectPass.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"

namespace Engine::Graphics::Pipeline
{
	namespace
	{
		// 被弾の画面効果の定数
		// ※ HLSL 側(Asset/Shader/Source/PostProcess/DamageEffect/DamageEffectCS.hlsl の DamageEffect)と並びを合わせること
		struct DamageEffectCB
		{
			float intensity;			// 掛かり具合(0〜1)
			float time;					// パスが回り始めてからの経過時間(秒)
			float screenNoise;
			float screenGlitch;

			float screenRGBShift;
			float screenDesaturate;
			float vignette;
			float glitchBands;

			Math::Vector3 vignetteColor;
			float glitchRate;

			float hudNoise;
			float hudGlitch;
			float hudRGBShift;
			float hudFlicker;

			float glitchSpeed;
			float pad0;
			float pad1;
			float pad2;
		};
	}

	void DamageEffectPass::SetupSlots()
	{
		// 画面(トーンマップ後の絵)
		DeclareInput("Color", EAccessType::SRV, EPassSlotType::Texture, true, ROOT_INPUT_SRV);

		// HUD(UI を描いた板。任意)。繋がないと HUD の出力は透明になる
		DeclareInput("UI", EAccessType::SRV, EPassSlotType::Texture, false, ROOT_INPUT_SRV);

		DeclareOutput("Color", "DamageEffectColor", DXGI_FORMAT_R8G8B8A8_UNORM,
			EAccessType::UAV, EPassSlotType::Texture, false, ROOT_OUTPUT_UAV);
		DeclareOutput("UI", "DamageEffectUI", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::UAV, EPassSlotType::Texture, false, ROOT_OUTPUT_UAV);
	}

	void DamageEffectPass::Compile(const PassContext& a_context)
	{
		SetupComputeShader(a_context, "Asset/Shader/Source/PostProcess/DamageEffect/DamageEffectCS.cso", "DamageEffectCS");
	}

	void DamageEffectPass::Update(const PassContext& a_context)
	{
		if (!a_context.pRenderContext || !a_context.pCmdList || !a_context.pGraphicsEngine) return;

		m_elapsedTime += MainEngine::Instance().GetDeltaTime();

		const DamageEffectData& _data = a_context.pGraphicsEngine->GetSceneView()->GetDamageEffectData();

		DamageEffectCB _cb = {};
		_cb.intensity = std::clamp(_data.intensity, 0.0f, 1.0f);
		_cb.time = m_elapsedTime;
		_cb.screenNoise = _data.screenNoise;
		_cb.screenGlitch = _data.screenGlitch;
		_cb.screenRGBShift = _data.screenRGBShift;
		_cb.screenDesaturate = _data.screenDesaturate;
		_cb.vignette = _data.vignette;
		_cb.glitchBands = (std::max)(_data.glitchBands, 1.0f);
		_cb.vignetteColor = _data.vignetteColor;
		_cb.glitchRate = _data.glitchRate;
		_cb.hudNoise = _data.hudNoise;
		_cb.hudGlitch = _data.hudGlitch;
		_cb.hudRGBShift = _data.hudRGBShift;
		_cb.hudFlicker = _data.hudFlicker;
		_cb.glitchSpeed = _data.glitchSpeed;

		// 掛からないフレームも回す(出力は別のリソースなので、写さないと次段が空の絵を読む)
		a_context.pRenderContext->BindCB()->BindAndAttachDataComputeRootCBV(a_context.pCmdList, ROOT_DAMAGE_CB, _cb);
		DispatchFullScreen(a_context);
	}



	void DamageEffectPass::Archive(Engine::Persistence::Archive& /*a_arch*/)
	{}
}
