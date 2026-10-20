#include "Engine/Graphics/RenderingPipeline/StandardPipeline/StandardPipeline.h"

#include "Engine/Graphics/RenderingPipeline/PassMetaRegistry/PassMetaRegistry.h"
#include "Engine/Graphics/RenderingPipeline/RenderGraph/RenderGraph.h"

#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ZPrePass/ZPrePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/GBufferPass/GBufferPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/RaytracingShadowPass/RaytracingShadowPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/RaytracingGIPass/RaytracingGIPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/RaytracingReflectionPass/RaytracingReflectionPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/ShadowMapPass/ShadowMapPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/ShadowMapMaskPass/ShadowMapMaskPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Utility/CopyPass/CopyPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/Shadow/ShadowTemporalAccumulationPass/ShadowTemporalAccumulationPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/Shadow/ShadowSpatialDenoisePass/ShadowSpatialDenoisePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/GI/GITemporalAccumulationPass/GITemporalAccumulationPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/GI/GISpatialDenoisePass/GISpatialDenoisePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/UpScale/UpScalePass/UpScalePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/DeferredLightingPass/DeferredLightingPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Sky/SkyPass/SkyPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ParticlePass/ParticlePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/AntiAliasing/TAAPass/TAAPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/DoF/CoCPass/CoCPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/DoF/DoFPass/DoFPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/BloomExtractPass/BloomExtractPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Blur/GaussianBlurPass/GaussianBlurPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/KawaseBlurPass/KawaseBlurPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/BloomCompositePass/BloomCompositePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/ScreenSpaceLensFlarePass/ScreenSpaceLensFlarePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Blur/RadialBlurPass/RadialBlurPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Distortion/FishEyePass/FishEyePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/DebugLinePass/DebugLinePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/UI/UIPass/UIPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/ToneMap/ToneMapPass/ToneMapPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Present/FinalOutputPass/FinalOutputPass.h"

namespace Engine::Graphics::Pipeline
{
	namespace
	{
		// ノードの置き場所 : 左から右へ流れるように、列と行で並べる
		constexpr float COLUMN_WIDTH = 360.0f;
		constexpr float ROW_HEIGHT = 250.0f;

		Math::Vector2 NodePos(int a_column, int a_row)
		{
			return Math::Vector2(40.0f + COLUMN_WIDTH * a_column, 40.0f + ROW_HEIGHT * a_row);
		}

		// パスを1つ足して、表示名と置き場所まで決める
		template<typename T>
		T* AddPass(RenderGraph& a_graph, const PassMetaRegistry& a_registry,
			const std::string& a_name, int a_column, int a_row)
		{
			Pass* _pPass = a_graph.AddPass(a_registry, a_registry.GetTypeID<T>());
			if (!_pPass)
			{
				ENGINE_WARNING("[StandardPipeline] パスを作れませんでした : %s", a_name.c_str());
				return nullptr;
			}

			// 同じ型を複数置くので、どれがどれか分かるように名前を付け替える。
			// 名前はPSOのキーにも使われるので、段ごとに変えておく
			if (!a_name.empty()) _pPass->SetName(a_name);
			_pPass->SetEditorPos(NodePos(a_column, a_row));

			return static_cast<T*>(_pPass);
		}

		// つなぎを1本張る : 失敗したら分かるように出しておく
		bool Link(RenderGraph& a_graph,
			Pass* a_pSrc, const char* a_srcPin,
			Pass* a_pDst, const char* a_dstPin)
		{
			if (!a_pSrc || !a_pDst) return false;

			const bool _isOK = a_graph.Link(
				a_pSrc->GetGUID(), Pass::MakeSlotID(a_srcPin),
				a_pDst->GetGUID(), Pass::MakeSlotID(a_dstPin));

			if (!_isOK)
			{
				ENGINE_WARNING("[StandardPipeline] 繋げませんでした : %s.%s -> %s.%s",
					a_pSrc->GetName().c_str(), a_srcPin,
					a_pDst->GetName().c_str(), a_dstPin);
			}
			return _isOK;
		}
	}

	bool BuildStandardPipeline(RenderingPipelineAsset& a_asset, const PassMetaRegistry& a_registry)
	{
		RenderGraph* _pGraph = a_asset.RefRenderGraph();
		if (!_pGraph) return false;

		//----------------------------------------------------------------------------------
		// 今入っているものを全部捨てる
		//----------------------------------------------------------------------------------
		{
			std::vector<Core::GUID> _guidVec = {};
			_guidVec.reserve(_pGraph->GetPasses().size());
			for (const auto& _upPass : _pGraph->GetPasses())
			{
				if (_upPass) _guidVec.push_back(_upPass->GetGUID());
			}
			for (const Core::GUID& _guid : _guidVec)
			{
				_pGraph->RemovePass(_guid);
			}
		}

		RenderGraph& _graph = *_pGraph;

		//----------------------------------------------------------------------------------
		// ジオメトリ
		//----------------------------------------------------------------------------------
		// 深度だけを先に埋める。GBuffer はこの深度を受け取るのでクリアしない
		auto* _pZPre = AddPass<ZPrePass>(_graph, a_registry, "ZPrePass", 0, 0);
		auto* _pGBuffer = AddPass<GBufferPass>(_graph, a_registry, "GBufferPass", 1, 0);

		//----------------------------------------------------------------------------------
		// 前フレームのGBuffer
		//
		// デノイズの再投影に要る。2枚組(History)にしてあるので、
		// 「前フレームを読む」と宣言したピンからは1つ前の中身が返る。
		// 実行順はどこでもよく、書き手と読み手が待ち合わせない
		//----------------------------------------------------------------------------------
		auto* _pPrevDepth = AddPass<CopyPass>(_graph, a_registry, "PrevDepthPass", 2, 3);
		if (_pPrevDepth) _pPrevDepth->Configure("PrevDepth", 3, true);

		auto* _pPrevNormal = AddPass<CopyPass>(_graph, a_registry, "PrevNormalPass", 2, 4);
		if (_pPrevNormal) _pPrevNormal->Configure("PrevNormal", 2, true);

		//----------------------------------------------------------------------------------
		// 速度バッファの写し
		//
		// 空は自分が描いたピクセルの速度も書き足すが、それはTAAのためのもので、
		// デノイズが読むのは「モデルが描いた速度」だけでよい。
		// 同じリソースへ空が描き足すと、デノイズが空(=ライティングの後)を待つことになり、
		// ライティングがデノイズを待つ関係と噛み合わなくなる(循環)。
		// 写しを1枚挟んで、空が描き足す先をモデルのものと分けている
		//----------------------------------------------------------------------------------
		auto* _pVelocityCopy = AddPass<CopyPass>(_graph, a_registry, "SceneVelocityPass", 2, 5);
		if (_pVelocityCopy) _pVelocityCopy->Configure("SceneVelocity", 2);

		//----------------------------------------------------------------------------------
		// レイトレーシング
		//----------------------------------------------------------------------------------
		auto* _pRayShadow = AddPass<RaytracingShadowPass>(_graph, a_registry, "RaytracingShadowPass", 2, 0);
		auto* _pRayGI = AddPass<RaytracingGIPass>(_graph, a_registry, "RaytracingGIPass", 2, 1);
		auto* _pRayReflection = AddPass<RaytracingReflectionPass>(_graph, a_registry, "RaytracingReflectionPass", 2, 2);

		//----------------------------------------------------------------------------------
		// デノイズ(影)
		//
		// 時間方向にためてから、空間方向にならす
		//----------------------------------------------------------------------------------
		auto* _pShadowTA = AddPass<ShadowTemporalAccumulationPass>(_graph, a_registry, "ShadowTemporalAccumulationPass", 3, 0);
		auto* _pShadowSpatial = AddPass<ShadowSpatialDenoisePass>(_graph, a_registry, "ShadowSpatialDenoisePass", 4, 0);
		if (_pShadowSpatial) _pShadowSpatial->SetResourceName("DenoisedShadow");

		//----------------------------------------------------------------------------------
		// シャドウマップ
		//
		// 主光源の影をレイトレの代わりに求める。どちらで求めるかはシーンの設定で決まり、
		// 使わない側は何もしないので、両方を置いたままにしておく。
		// マスクはデノイズ後の影へ描き足す(シャドウマップの影は時間方向にためなくてよい)
		//----------------------------------------------------------------------------------
		auto* _pShadowMap = AddPass<ShadowMapPass>(_graph, a_registry, "ShadowMapPass", 4, 2);
		auto* _pShadowMapMask = AddPass<ShadowMapMaskPass>(_graph, a_registry, "ShadowMapMaskPass", 5, 0);

		//----------------------------------------------------------------------------------
		// デノイズ(GI)
		//
		// 生のGIはノイズが強いので、時間方向にためる前に一度ならしておく。
		// 反復はノードを並べて表す方針なので、段ごとに1ノード置く
		//----------------------------------------------------------------------------------
		auto* _pGIPreSpatial = AddPass<GISpatialDenoisePass>(_graph, a_registry, "GIPreSpatialDenoisePass", 3, 1);
		if (_pGIPreSpatial) _pGIPreSpatial->Configure("RayGIDenoised", 2);

		auto* _pGITA = AddPass<GITemporalAccumulationPass>(_graph, a_registry, "GITemporalAccumulationPass", 4, 1);

		auto* _pGISpatial = AddPass<GISpatialDenoisePass>(_graph, a_registry, "GISpatialDenoisePass", 5, 1);
		if (_pGISpatial) _pGISpatial->Configure("FinalGI", 2);

		// GIはハーフ解像度で回っているので、ここでフル解像度へ戻す
		auto* _pUpScale = AddPass<UpScalePass>(_graph, a_registry, "UpScalePass", 6, 1);

		//----------------------------------------------------------------------------------
		// ライティング
		//----------------------------------------------------------------------------------
		auto* _pLighting = AddPass<DeferredLightingPass>(_graph, a_registry, "DeferredLightingPass", 7, 0);
		auto* _pSky = AddPass<SkyPass>(_graph, a_registry, "SkyPass", 8, 0);
		auto* _pParticle = AddPass<ParticlePass>(_graph, a_registry, "ParticlePass", 9, 0);

		//----------------------------------------------------------------------------------
		// ポストプロセス
		//----------------------------------------------------------------------------------
		auto* _pTAA = AddPass<TAAPass>(_graph, a_registry, "TAAPass", 10, 0);

		// 被写界深度 : ボカした絵にTAAを掛けると履歴がにじむので、必ずTAAの後
		auto* _pCoC = AddPass<CoCPass>(_graph, a_registry, "CoCPass", 10, 1);
		auto* _pDoF = AddPass<DoFPass>(_graph, a_registry, "DoFPass", 11, 0);

		//----------------------------------------------------------------------------------
		// 川瀬式ブルーム
		//
		//   抽出(等倍) → 1/2 → 1/4 → 1/8 → 1/16 と縮小しながらガウシアンブラー
		//              → 4枚を平均して1枚に → メインカラーへ加算
		//
		// 縮小率ごとにボケの広がりが変わるので、それを重ねると
		// 「芯は明るく、外へ行くほどゆるく広がる」ブルーム特有の減衰になる。
		// 等倍へ戻す拡大パスは持たない。合流がUVでサンプリングするので、
		// 解像度の違いはサンプラーのバイリニアが吸収する
		//----------------------------------------------------------------------------------
		auto* _pBloomExtract = AddPass<BloomExtractPass>(_graph, a_registry, "BloomExtractPass", 12, 0);

		// 各段の解像度スケール
		constexpr float BLOOM_SCALES[4] = { 0.5f, 0.25f, 0.125f, 0.0625f };

		// ブラーの広がり。すべて縮小後の低解像度で回るので広め(5x5)に取れる
		constexpr float BLUR_SIGMA = 1.2f;
		constexpr int   BLUR_TAP_RADIUS = 2;

		GaussianBlurPass* _pBloomDown[4] = {};
		for (int _i = 0; _i < 4; ++_i)
		{
			const std::string _name = "BloomBlurDownPass" + std::to_string(_i);
			_pBloomDown[_i] = AddPass<GaussianBlurPass>(_graph, a_registry, _name, 13, _i);
			if (!_pBloomDown[_i]) continue;

			_pBloomDown[_i]->Configure(
				"BloomBlurDown" + std::to_string(_i), BLOOM_SCALES[_i], BLUR_SIGMA, BLUR_TAP_RADIUS);
		}

		auto* _pKawase = AddPass<KawaseBlurPass>(_graph, a_registry, "KawaseBlurPass", 14, 0);
		auto* _pBloomComposite = AddPass<BloomCompositePass>(_graph, a_registry, "BloomCompositePass", 15, 0);

		// レンズフレア : まとめたブルームを中心で折り返して足す。ブルームを足し終えた絵に重ねる
		auto* _pLensFlare = AddPass<ScreenSpaceLensFlarePass>(_graph, a_registry, "ScreenSpaceLensFlarePass", 15, 1);

		// ラジアルブラーはブルームの後。
		// 光ったところごと放射状に流れてほしいので、逆にすると跡だけが後から光る
		auto* _pRadial = AddPass<RadialBlurPass>(_graph, a_registry, "RadialBlurPass", 16, 0);

		// 魚眼はラジアルブラーの後。
		// 引きずった跡ごとレンズで歪んでほしい
		auto* _pFishEye = AddPass<FishEyePass>(_graph, a_registry, "FishEyePass", 17, 0);

		//----------------------------------------------------------------------------------
		// UI(ポストプロセスの後に重ねる)
		//----------------------------------------------------------------------------------
		auto* _pDebugLine = AddPass<DebugLinePass>(_graph, a_registry, "DebugLinePass", 18, 0);
		auto* _pUI = AddPass<UIPass>(_graph, a_registry, "UIPass", 19, 0);

		//----------------------------------------------------------------------------------
		// 提示
		//----------------------------------------------------------------------------------
		auto* _pToneMap = AddPass<ToneMapPass>(_graph, a_registry, "ToneMapPass", 20, 0);

		// グラフの出口。捨てたぶんを足し直す
		a_asset.EnsureFinalPass();

		Pass* _pFinal = nullptr;
		for (const auto& _upPass : _graph.GetPasses())
		{
			if (!_upPass) continue;
			if (!a_registry.IsFinalPassType(_upPass->GetTypeID())) continue;

			_pFinal = _upPass.get();
			_pFinal->SetEditorPos(NodePos(21, 0));
			break;
		}

		//==================================================================================
		//
		// 配線
		//
		//==================================================================================

		// ---- ジオメトリ ----
		Link(_graph, _pZPre, "Depth", _pGBuffer, "PreDepth");

		// ---- 前フレームのGBuffer / 速度の写し ----
		Link(_graph, _pGBuffer, "Depth", _pPrevDepth, "Source");
		Link(_graph, _pGBuffer, "Normal", _pPrevNormal, "Source");
		Link(_graph, _pGBuffer, "Velocity", _pVelocityCopy, "Source");

		// ---- レイトレ ----
		Link(_graph, _pGBuffer, "Normal", _pRayShadow, "Normal");
		Link(_graph, _pGBuffer, "Depth", _pRayShadow, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pRayGI, "Normal");
		Link(_graph, _pGBuffer, "Depth", _pRayGI, "Depth");
		Link(_graph, _pGBuffer, "Depth", _pRayReflection, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pRayReflection, "Normal");

		// ---- 影のデノイズ ----
		Link(_graph, _pRayShadow, "Shadow", _pShadowTA, "Shadow");
		Link(_graph, _pGBuffer, "Velocity", _pShadowTA, "Velocity");
		Link(_graph, _pShadowTA, "HistoryOut", _pShadowTA, "History");	// 前フレームの自分
		Link(_graph, _pGBuffer, "Depth", _pShadowTA, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pShadowTA, "Normal");
		Link(_graph, _pPrevDepth, "Result", _pShadowTA, "PrevDepth");
		Link(_graph, _pPrevNormal, "Result", _pShadowTA, "PrevNormal");

		Link(_graph, _pShadowTA, "HistoryOut", _pShadowSpatial, "Shadow");
		Link(_graph, _pGBuffer, "Depth", _pShadowSpatial, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pShadowSpatial, "Normal");

		// ---- シャドウマップ : デノイズ後の影へ描き足す ----
		Link(_graph, _pShadowSpatial, "Result", _pShadowMapMask, "Shadow");
		Link(_graph, _pGBuffer, "Depth", _pShadowMapMask, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pShadowMapMask, "Normal");
		Link(_graph, _pShadowMap, "ShadowMap", _pShadowMapMask, "ShadowMap");

		// ---- GIのデノイズ ----
		Link(_graph, _pRayGI, "GI", _pGIPreSpatial, "GI");
		Link(_graph, _pGBuffer, "Depth", _pGIPreSpatial, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pGIPreSpatial, "Normal");

		Link(_graph, _pGIPreSpatial, "Result", _pGITA, "GI");
		Link(_graph, _pGBuffer, "Velocity", _pGITA, "Velocity");
		Link(_graph, _pGITA, "HistoryOut", _pGITA, "History");			// 前フレームの自分
		Link(_graph, _pGBuffer, "Depth", _pGITA, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pGITA, "Normal");
		Link(_graph, _pPrevDepth, "Result", _pGITA, "PrevDepth");
		Link(_graph, _pPrevNormal, "Result", _pGITA, "PrevNormal");

		Link(_graph, _pGITA, "HistoryOut", _pGISpatial, "GI");
		Link(_graph, _pGBuffer, "Depth", _pGISpatial, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pGISpatial, "Normal");

		Link(_graph, _pGISpatial, "Result", _pUpScale, "GI");
		Link(_graph, _pGBuffer, "Depth", _pUpScale, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pUpScale, "Normal");

		// ---- ライティング ----
		Link(_graph, _pGBuffer, "Albedo", _pLighting, "Albedo");
		Link(_graph, _pGBuffer, "Normal", _pLighting, "Normal");
		Link(_graph, _pGBuffer, "Material", _pLighting, "Material");
		Link(_graph, _pGBuffer, "Emissive", _pLighting, "Emissive");
		Link(_graph, _pGBuffer, "Depth", _pLighting, "Depth");
		Link(_graph, _pShadowMapMask, "Shadow", _pLighting, "Shadow");
		Link(_graph, _pUpScale, "Result", _pLighting, "GI");
		Link(_graph, _pRayReflection, "Reflection", _pLighting, "Reflection");

		// ---- 空 : ライティングの結果と速度へ描き足す ----
		Link(_graph, _pGBuffer, "Depth", _pSky, "Depth");
		Link(_graph, _pLighting, "Color", _pSky, "Color");
		Link(_graph, _pVelocityCopy, "Result", _pSky, "Velocity");

		// ---- パーティクル : 空の後の絵へ重ねる ----
		Link(_graph, _pGBuffer, "Depth", _pParticle, "Depth");
		Link(_graph, _pSky, "Color", _pParticle, "Color");

		// ---- TAA ----
		Link(_graph, _pParticle, "Color", _pTAA, "Color");
		Link(_graph, _pTAA, "HistoryOut", _pTAA, "History");			// 前フレームの自分
		Link(_graph, _pSky, "Velocity", _pTAA, "Velocity");
		Link(_graph, _pGBuffer, "Depth", _pTAA, "Depth");
		Link(_graph, _pGBuffer, "Normal", _pTAA, "Normal");

		// ---- 被写界深度 ----
		Link(_graph, _pGBuffer, "Depth", _pCoC, "Depth");
		Link(_graph, _pTAA, "HistoryOut", _pDoF, "Color");
		Link(_graph, _pCoC, "CoC", _pDoF, "CoC");

		// ---- ブルーム ----
		Link(_graph, _pDoF, "Result", _pBloomExtract, "Color");

		for (int _i = 0; _i < 4; ++_i)
		{
			// 1つ前の段を入力にして半分ずつ小さくしていく
			Pass* _pSrc = (_i == 0) ? static_cast<Pass*>(_pBloomExtract) : static_cast<Pass*>(_pBloomDown[_i - 1]);
			Link(_graph, _pSrc, "Result", _pBloomDown[_i], "Color");
		}

		Link(_graph, _pBloomDown[0], "Result", _pKawase, "Down0");
		Link(_graph, _pBloomDown[1], "Result", _pKawase, "Down1");
		Link(_graph, _pBloomDown[2], "Result", _pKawase, "Down2");
		Link(_graph, _pBloomDown[3], "Result", _pKawase, "Down3");

		Link(_graph, _pDoF, "Result", _pBloomComposite, "Color");
		Link(_graph, _pKawase, "Result", _pBloomComposite, "Bloom");

		// ---- レンズフレア ----
		Link(_graph, _pBloomComposite, "Result", _pLensFlare, "Color");
		Link(_graph, _pKawase, "Result", _pLensFlare, "Bloom");

		// ---- 歪み ----
		Link(_graph, _pLensFlare, "Result", _pRadial, "Color");
		Link(_graph, _pRadial, "Result", _pFishEye, "Color");

		// ---- UI ----
		Link(_graph, _pGBuffer, "Depth", _pDebugLine, "Depth");
		Link(_graph, _pFishEye, "Result", _pDebugLine, "Color");
		Link(_graph, _pDebugLine, "Color", _pUI, "Color");

		// ---- 提示 ----
		Link(_graph, _pUI, "Color", _pToneMap, "Color");
		Link(_graph, _pToneMap, "Result", _pFinal, FinalOutputPass::INPUT_NAME);

		//----------------------------------------------------------------------------------
		// 並べ替えまで通しておく
		//----------------------------------------------------------------------------------
		a_asset.SetDirty();
		a_asset.Compile();

		return true;
	}
}
