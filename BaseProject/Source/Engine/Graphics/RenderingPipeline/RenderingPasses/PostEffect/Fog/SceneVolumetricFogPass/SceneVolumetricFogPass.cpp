#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Fog/SceneVolumetricFogPass/SceneVolumetricFogPass.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Graphics/Frame/SceneView/AmbientData.h"
#include "Engine/Graphics/Frame/SceneView/CameraData.h"
#include "Engine/Graphics/Frame/SceneView/SceneFogData.h"

namespace Engine::Graphics::Pipeline
{
	void SceneVolumetricFogPass::SetupSlots()
	{
		// 同じ番号を指定した入力は、宣言した順にルート定数へ並ぶ(シェーダーの b100 と合わせる)。
		// シーン全体の深度 : レイの終点
		DeclareInput("SceneDepth", EAccessType::SRV, EPassSlotType::Texture, true, ROOT_INPUT_SRV);
		// 地面だけの深度(GroundDepthPass の出力。任意) : ダストの高さの基準。
		// 繋がないとダストは出ない
		DeclareInput("GroundDepth", EAccessType::SRV, EPassSlotType::Texture, false, ROOT_INPUT_SRV);
		// グラウンドフィールド(GroundFieldPass の出力。任意) : 衝撃で払われた・寄せられたチリ。
		// 繋がないとチリは衝撃で動かない
		DeclareInput("GroundField", EAccessType::SRV, EPassSlotType::Texture, false, ROOT_INPUT_SRV);
		// 主光源のシャドウマップ(ShadowMapPass の出力。任意) : レイに沿って平行光の影を引く。
		// 繋がないと平行光は遮られずにフォグを照らす
		DeclareInput("ShadowMap", EAccessType::SRV, EPassSlotType::Texture, false, ROOT_INPUT_SRV);
		// レイトレの影(RaytracingVolumeShadowPass の VolumeShadow 出力。任意) : 視線に沿った日なたの割合。
		// 低解像度で届くので、シェーダーが範囲の終わりを見ながら引き伸ばす。
		// 影の求め方がレイトレのフレームはこれを使う。繋がないとレイトレのフレームは平行光が遮られない
		DeclareInput("VolumeShadow", EAccessType::SRV, EPassSlotType::Texture, false, ROOT_INPUT_SRV);

		// フォグ。rgb = 色 / a = 濃さ。
		// 全画素を書き潰すのでクリアは不要。大きさは描画解像度の resolutionDivisor 分の1
		DeclareOutput("Fog", "SceneFog", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::UAV, EPassSlotType::Texture, false, ROOT_OUTPUT_UAV);

		ApplyResolution();
	}

	void SceneVolumetricFogPass::ApplyResolution()
	{
		Slot* _pOut = FindOutputSlot(MakeSlotID("Fog"));
		if (!_pOut) return;

		const uint32_t _divisor = std::clamp(m_params.resolutionDivisor, 1u, 4u);

		// 描画解像度 × scale : 解像度違いのカメラやリサイズに追従させるため絶対値にはしない
		_pOut->width = 0;
		_pOut->height = 0;
		_pOut->scale = 1.0f / static_cast<float>(_divisor);
	}

	void SceneVolumetricFogPass::Compile(const PassContext& a_context)
	{
		SetupComputeShader(a_context, "Asset/Shader/Source/PostProcess/Fog/SceneVolumetricFogCS.cso", "SceneVolumetricFogCS");
	}

	void SceneVolumetricFogPass::Update(const PassContext& a_context)
	{
		RenderContext* _pCtx = a_context.pRenderContext;
		GraphicsEngine* _pGE = a_context.pGraphicsEngine;
		if (!_pCtx || !_pGE || !a_context.pCmdList || !a_context.pResourceManager) return;

		auto* _pCmd = a_context.pCmdList;
		auto& _resManager = *a_context.pResourceManager;
		const SceneView* _pSceneView = _pGE->GetSceneView();

		// カメラ : 深度からワールド座標を戻す
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV<CameraData>(
			_pCmd, ROOT_CAMERA_CB, _pSceneView->GetCameraData());

		// シーンのフォグ
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV(_pCmd, ROOT_SCENE_FOG_CB, _pSceneView->GetSceneFogData());

		// グラウンドダスト : 経過時間だけはパスが進める
		m_elapsedTime += MainEngine::Instance().GetDeltaTime();

		GroundDustCB _dustCB = _pSceneView->GetGroundDustData();
		_dustCB.time = m_elapsedTime;
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV(_pCmd, ROOT_GROUND_DUST_CB, _dustCB);

		// ノイズテクスチャ : 読み込みはシーンが始めている。
		// 届くまでのフレーム(と未設定のとき)はノイズなしで描く
		UINT _noiseIndex = DESCRIPTOR_INDEX_NONE;
		const auto& _noiseHandle = _pSceneView->GetFogNoiseTexture();
		if (_resManager.IsReady(_noiseHandle))
		{
			if (const auto* _pNoiseTex = _resManager.Get(_noiseHandle))
			{
				_noiseIndex = _pNoiseTex->GetSRV().GetIndex();
			}
		}
		_pCtx->ComputeBindDescriptorIndices(ROOT_NOISE_SRV, std::span<const UINT>(&_noiseIndex, 1));

		// 媒質を照らす光
		const LightManager* _pLightManager = _pGE->RefLightManager();

		// 環境光
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV<AmbientData>(_pCmd, ROOT_AMBIENT_CB, _pSceneView->GetAmbientData());

		// 平行光の影 : カスケードの行列・区切り・バイアス。
		// ShadowMapPass が描いたときと同じもの(影の求め方がシャドウマップでなければカスケード数 0 で届く)
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV(_pCmd, ROOT_SHADOW_CB, _pLightManager->GetSunShadowCB());

		// 平行光の色と強さ : 影の CB には向きしか無いので、主光源を別に送る
		const auto _sunCB = _pLightManager->GetSunLightCB();
		_pCtx->BindCB()->BindAndAttachDataComputeRootCBV(_pCmd, ROOT_SUN_LIGHT_CB, _sunCB);

		// 出力の大きさで回す(描画解像度より小さいことがある)
		const Slot* _pOut = FindOutputSlot(MakeSlotID("Fog"));
		if (_pOut) DispatchForSlot(a_context, *_pOut);
	}



	void SceneVolumetricFogPass::Archive(Engine::Persistence::Archive& a_arch)
	{
		// フォグの値はシーンの持ち物。パスとして保存するのは回す解像度だけ
		a_arch.Field("resolutionDivisor", m_params.resolutionDivisor);

		if (a_arch.IsLoading()) ApplyResolution();
	}
}
