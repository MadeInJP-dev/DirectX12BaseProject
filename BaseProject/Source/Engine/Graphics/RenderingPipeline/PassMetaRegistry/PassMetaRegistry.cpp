#include "Engine/Graphics/RenderingPipeline/PassMetaRegistry/PassMetaRegistry.h"

// ---- 登録する標準パス ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Test/TestGBufferPass/TestGBufferPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Present/FinalOutputPass/FinalOutputPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Test/TestClearPass/TestClearPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/GBufferPass/GBufferPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/DeferredLightingPass/DeferredLightingPass.h"

// ---- ポストプロセス ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Blur/RadialBlurPass/RadialBlurPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Distortion/FishEyePass/FishEyePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/ScanLine/ScanLinePass/ScanLinePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/DoF/CoCPass/CoCPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/DoF/DoFPass/DoFPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/GroundEffect/GroundFieldPass/GroundFieldPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Fog/SceneVolumetricFogPass/SceneVolumetricFogPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Fog/SceneFogCompositePass/SceneFogCompositePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/AntiAliasing/TAAPass/TAAPass.h"

// ---- ブルーム ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/BloomExtractPass/BloomExtractPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Blur/GaussianBlurPass/GaussianBlurPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/KawaseBlurPass/KawaseBlurPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/BloomCompositePass/BloomCompositePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/ScreenSpaceLensFlarePass/ScreenSpaceLensFlarePass.h"

// ---- デノイズ ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/Shadow/ShadowTemporalAccumulationPass/ShadowTemporalAccumulationPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/Shadow/ShadowSpatialDenoisePass/ShadowSpatialDenoisePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/GI/GITemporalAccumulationPass/GITemporalAccumulationPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/GI/GISpatialDenoisePass/GISpatialDenoisePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/UpScale/UpScalePass/UpScalePass.h"

// ---- ジオメトリ・提示 ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ZPrePass/ZPrePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ZPrePass/GroundDepth.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Sky/SkyPass/SkyPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/ToneMap/ToneMapPass/ToneMapPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/UI/UIPass/UIPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/DebugLinePass/DebugLinePass.h"

// ---- リソース操作 ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Utility/CopyPass/CopyPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Utility/BlendPass/BlendPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Utility/MonitorPass/MonitorPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ParticlePass/ParticlePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/BoundaryPass/BoundaryPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/TransparentPass/TransparentPass.h"

// ---- レイトレ ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/RaytracingShadowPass/RaytracingShadowPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/RaytracingVolumeShadowPass/RaytracingVolumeShadowPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/RaytracingGIPass/RaytracingGIPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/RaytracingReflectionPass/RaytracingReflectionPass.h"

// ---- シャドウマップ ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/ShadowMapPass/ShadowMapPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/ShadowMapMaskPass/ShadowMapMaskPass.h"

namespace Engine::Graphics::Pipeline
{
	ID<Pass> PassMetaRegistry::GetTypeID(const std::string& a_name) const
	{
		auto _it = m_nameMap.find(a_name);
		if (_it != m_nameMap.end())
		{
			return _it->second;
		}

		// 見つからなければ無効値を返す
		return ID<Pass>();
	}
	ID<Pass> PassMetaRegistry::GetTypeID(TypeInfo::TypeKey a_key) const
	{
		auto _it = m_typeKeyMap.find(a_key);
		if (_it != m_typeKeyMap.end())
		{
			return _it->second;
		}

		// 見つからなければ無効値を返す
		return ID<Pass>();
	}
	const PassMeta* PassMetaRegistry::GetMeta(ID<Pass> a_id) const
	{
		if (!a_id.IsValid()) return nullptr;

		auto _it = m_metaMap.find(a_id);
		if (_it != m_metaMap.end())
		{
			return &_it->second;
		}

		return nullptr;
	}
	std::unique_ptr<Pass> PassMetaRegistry::Create(ID<Pass> a_id) const
	{
		auto _it = m_funcMap.find(a_id);
		if (_it != m_funcMap.end() && _it->second.factory)
		{
			return _it->second.factory();
		}
		return nullptr;
	}

	// 登録名は保存データのキーになる(ハッシュを取ってタイプIDにしている)ので、
	// 一度出したら変えないこと。変えると既存のパイプラインからパスが消える
	void RegisterBuiltinPasses(PassMetaRegistry& a_registry)
	{
		a_registry.RegisterType<TestGBufferPass>("TestGBufferPass");

		// 不透明モデルをGBufferへ描く(既存パスの移植)
		a_registry.RegisterType<GBufferPass>("GBufferPass");

		// GBufferと影・GIを合成して色を作る(既存パスの移植)
		a_registry.RegisterType<DeferredLightingPass>("DeferredLightingPass");

		// ---- ポストプロセス ----
		a_registry.RegisterType<RadialBlurPass>("RadialBlurPass");
		a_registry.RegisterType<FishEyePass>("FishEyePass");
		a_registry.RegisterType<ScanLinePass>("ScanLinePass");
		a_registry.RegisterType<CoCPass>("CoCPass");
		a_registry.RegisterType<DoFPass>("DoFPass");
		a_registry.RegisterType<GroundFieldPass>("GroundFieldPass");
		a_registry.RegisterType<SceneVolumetricFogPass>("SceneVolumetricFogPass");
		a_registry.RegisterType<SceneFogCompositePass>("SceneFogCompositePass");
		a_registry.RegisterType<TAAPass>("TAAPass");

		// ---- ブルーム ----
		a_registry.RegisterType<BloomExtractPass>("BloomExtractPass");
		a_registry.RegisterType<GaussianBlurPass>("GaussianBlurPass");
		a_registry.RegisterType<KawaseBlurPass>("KawaseBlurPass");
		a_registry.RegisterType<BloomCompositePass>("BloomCompositePass");
		a_registry.RegisterType<ScreenSpaceLensFlarePass>("ScreenSpaceLensFlarePass");

		// ---- デノイズ(1ノード＝1回。反復はノードを並べる) ----
		a_registry.RegisterType<ShadowTemporalAccumulationPass>("ShadowTemporalAccumulationPass");
		a_registry.RegisterType<ShadowSpatialDenoisePass>("ShadowSpatialDenoisePass");
		a_registry.RegisterType<GITemporalAccumulationPass>("GITemporalAccumulationPass");
		a_registry.RegisterType<GISpatialDenoisePass>("GISpatialDenoisePass");
		a_registry.RegisterType<UpScalePass>("UpScalePass");

		// ---- ジオメトリ・提示 ----
		a_registry.RegisterType<ZPrePass>("ZPrePass");
		a_registry.RegisterType<GroundDepthPass>("GroundDepthPass");
		a_registry.RegisterType<SkyPass>("SkyPass");
		a_registry.RegisterType<ToneMapPass>("ToneMapPass");
		a_registry.RegisterType<UIPass>("UIPass");
		a_registry.RegisterType<DebugLinePass>("DebugLinePass");

		// リソースを写すだけの汎用パス(履歴の作成などに使う)
		a_registry.RegisterType<CopyPass>("CopyPass");

		// リソースブレンド用のパス
		a_registry.RegisterType<BlendPass>("BlendPass");

		// パスの間にはさんで、流れている絵をノードの中に出す確認用
		a_registry.RegisterType<MonitorPass>("MonitorPass");

		// パーティクル描画(発生と更新は GraphicsEngine 側)
		a_registry.RegisterType<ParticlePass>("ParticlePass");

		// 行ける範囲の境界面(プレイヤーが近づくと見えてくる半透明の壁)
		a_registry.RegisterType<BoundaryPass>("BoundaryPass");

		// 半透明のモデル(ライティングの後・ポストプロセスの前で、奥から手前の順に重ねる)
		a_registry.RegisterType<TransparentPass>("TransparentPass");

		// ---- レイトレ ----
		a_registry.RegisterType<RaytracingShadowPass>("RaytracingShadowPass");
		a_registry.RegisterType<RaytracingVolumeShadowPass>("RaytracingVolumeShadowPass");
		a_registry.RegisterType<RaytracingGIPass>("RaytracingGIPass");
		a_registry.RegisterType<RaytracingReflectionPass>("RaytracingReflectionPass");

		// ---- シャドウマップ(主光源の影をレイトレの代わりに求める) ----
		a_registry.RegisterType<ShadowMapPass>("ShadowMapPass");
		a_registry.RegisterType<ShadowMapMaskPass>("ShadowMapMaskPass");

		// 配線が通っているかを画面の色で確かめる用
		a_registry.RegisterType<TestClearPass>("TestClearPass");

		// グラフの出口。どのパイプラインにも自動で1つ置かれる
		a_registry.RegisterType<FinalOutputPass>("FinalOutputPass", true);
	}

	ID<Pass> PassMetaRegistry::GetFinalPassTypeID() const
	{
		for (const auto& [_id, _meta] : m_metaMap)
		{
			if (_meta.isFinalPass) return _id;
		}
		return ID<Pass>();
	}

	bool PassMetaRegistry::IsFinalPassType(ID<Pass> a_id) const
	{
		const PassMeta* _pMeta = GetMeta(a_id);
		return _pMeta && _pMeta->isFinalPass;
	}
}