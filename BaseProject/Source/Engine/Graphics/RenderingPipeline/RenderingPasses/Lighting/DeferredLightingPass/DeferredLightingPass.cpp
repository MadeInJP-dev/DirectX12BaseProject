#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/DeferredLightingPass/DeferredLightingPass.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/D3D12/DescriptorHeapManager/DescriptorHeapManager.h"
#include "Engine/Graphics/Frame/SceneView/AmbientData.h"
#include "Engine/Graphics/Frame/SceneView/CameraData.h"

namespace Engine::Graphics::Pipeline
{
	void DeferredLightingPass::SetupSlots()
	{
		// ルートパラメータ : 0=カメラCB / 1=アンビエントCB / 2=SRVテーブル / 3=UAV
		//                    4=ライティング調整値CB / 5=ライト配列SRV / 6=ライト数CB
		// テーブルの並びはシェーダーの t0.. と同じ順にすること
		DeclareInput("Albedo", EAccessType::SRV, EPassSlotType::Texture, true, 2);
		DeclareInput("Normal", EAccessType::SRV, EPassSlotType::Texture, true, 2);
		DeclareInput("Material", EAccessType::SRV, EPassSlotType::Texture, true, 2);
		DeclareInput("Emissive", EAccessType::SRV, EPassSlotType::Texture, true, 2);
		DeclareInput("Depth", EAccessType::SRV, EPassSlotType::Texture, true, 2);
		DeclareInput("Shadow", EAccessType::SRV, EPassSlotType::Texture, true, 2);
		// GI は無くてもよい(レイトレを抜いたパイプライン)。
		// 繋がっていなければ番号が無効値で届き、シェーダーはシーンの環境光を代わりに使う
		DeclareInput("GI", EAccessType::SRV, EPassSlotType::Texture, false, 2);
		// 鏡面反射も無くてよい(RaytracingReflectionPass の出力。1920x1080 固定なので UV で引く)。
		// 繋がっていなければ番号が無効値で届く
		DeclareInput("Reflection", EAccessType::SRV, EPassSlotType::Texture, false, 2);

		Slot& _out = DeclareOutput("Color", "AfterLighting", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::UAV, EPassSlotType::Texture, false, 3);
		_out.loadOp = ELoadOp::Clear;
	}

	void DeferredLightingPass::Compile(const PassContext& a_context)
	{
		SetupComputeShader(a_context,
			"Asset/Shader/Source/Lighting/Deferred/DeferredLightingShader.cso",
			"DeferredLightingShader");
	}

	void DeferredLightingPass::Update(const PassContext& a_context)
	{
		RenderContext* _pCtx = a_context.pRenderContext;
		GraphicsEngine* _pGE = a_context.pGraphicsEngine;
		if (!_pCtx || !_pGE || !a_context.pCmdList) return;

		auto* _pCmd = a_context.pCmdList;

		// カメラ
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV<CameraData>(_pCmd, 0, _pGE->GetSceneView()->GetCameraData());

		// アンビエントカラー
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV<AmbientData>(_pCmd, 1, _pGE->GetSceneView()->GetAmbientData());

		// ライティング調整値。
		// もとは OptionManager から引いていたが、パイプラインごとに変えられるよう
		// このパスの持ち物にしてある
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV(_pCmd, 4, m_cb);

		// ライト
		//
		// ライト配列はグラフの資源ではないので、スロットの宣言には乗らない。
		// GraphicsEngine が Execute() で今フレームぶんを詰め直したものの番号を、ここで直接渡す。
		// シェーダーは受け取った順に読むので、ポイント → 平行光 の順を崩さないこと
		const auto& _frameLight = _pGE->GetFrameLightData();
		const UINT _lightIndices[] = {
			_frameLight.plBuffer.GetSRV().GetIndex(),
			_frameLight.dlBuffer.GetSRV().GetIndex(),
		};
		_pCtx->ComputeBindDescriptorIndices(5, _lightIndices);

		// ライト数
		// StructuredBuffer は要素数を持たないので、ループの上限をCBで渡す。
		// HLSL の LightCountData と同じレイアウトで詰める
		struct LightCountCB
		{
			uint32_t directionalNum;
			uint32_t pointNum;
			uint32_t pad[2];
		};
		LightCountCB _countCB = {};
		_countCB.directionalNum = _frameLight.dlCount;
		_countCB.pointNum       = _frameLight.plCount;
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV(_pCmd, 6, _countCB);

		DispatchFullScreen(a_context);
	}



	void DeferredLightingPass::Archive(Engine::Persistence::Archive& a_arch)
	{
		a_arch.Field("giIntensity", m_cb.giIntensity);
		a_arch.Field("directionalIntensity", m_cb.directionalIntensity);
		a_arch.Field("dielectricF0", m_cb.dielectricF0);
		a_arch.Field("reflectionIntensity", m_cb.reflectionIntensity);
		a_arch.Field("reflectionRoughnessStart", m_cb.reflectionRoughnessStart);
		a_arch.Field("reflectionRoughnessEnd", m_cb.reflectionRoughnessEnd);
		a_arch.Field("reflectionBlurRadius", m_cb.reflectionBlurRadius);
		a_arch.Field("rimColor", m_cb.rimColor);
		a_arch.Field("rimIntensity", m_cb.rimIntensity);
		a_arch.Field("rimPower", m_cb.rimPower);
		a_arch.Field("rimBackLightStart", m_cb.rimBackLightStart);
		a_arch.Field("rimBackLightEnd", m_cb.rimBackLightEnd);
		a_arch.Field("rimShadowRate", m_cb.rimShadowRate);
	}
}
