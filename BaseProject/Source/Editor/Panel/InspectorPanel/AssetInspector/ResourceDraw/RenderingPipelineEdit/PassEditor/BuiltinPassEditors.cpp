//==========================================================================================
//
// BuiltinPassEditors (Editor::Inspector)
//
// エンジン標準のパス30種ぶんの編集UI。
//
// 以前はパスごとの .cpp に EditNode() / EditUpdate() として書かれていたので、
// ランタイムのパスが ImGui を直接呼んでいた。中身はそのままここへ移してある。
//
// 編集対象の値はパス側の公開 Params を通して触る。
// 触れるものが増えたら、まずパス側の Params に足すこと
//
// 実体は型ごとに1つだけ作られ、状態は持たない(編集対象は毎回渡される)
//
//==========================================================================================
#include "PassEditor.h"

#include "Editor/Helper/EditorHelper.h"

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

// ---- Geometry ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ZPrePass/ZPrePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/GBufferPass/GBufferPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ParticlePass/ParticlePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/DebugLinePass/DebugLinePass.h"

// ---- Lighting ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/DeferredLightingPass/DeferredLightingPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/RaytracingGIPass/RaytracingGIPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/RaytracingReflectionPass/RaytracingReflectionPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/RaytracingShadowPass/RaytracingShadowPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/RaytracingVolumeShadowPass/RaytracingVolumeShadowPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/ShadowMapPass/ShadowMapPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/ShadowMapMaskPass/ShadowMapMaskPass.h"

// ---- Denoise ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/GI/GISpatialDenoisePass/GISpatialDenoisePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/GI/GITemporalAccumulationPass/GITemporalAccumulationPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/Shadow/ShadowSpatialDenoisePass/ShadowSpatialDenoisePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Denoise/Shadow/ShadowTemporalAccumulationPass/ShadowTemporalAccumulationPass.h"

// ---- PostEffect ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/AntiAliasing/TAAPass/TAAPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/BloomExtractPass/BloomExtractPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/BloomCompositePass/BloomCompositePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Bloom/KawaseBlurPass/KawaseBlurPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Blur/GaussianBlurPass/GaussianBlurPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Blur/RadialBlurPass/RadialBlurPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Distortion/FishEyePass/FishEyePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/ScanLine/ScanLinePass/ScanLinePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/DoF/CoCPass/CoCPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/DoF/DoFPass/DoFPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/GroundEffect/GroundFieldPass/GroundFieldPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Fog/SceneVolumetricFogPass/SceneVolumetricFogPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/Fog/SceneFogCompositePass/SceneFogCompositePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/PostEffect/ToneMap/ToneMapPass/ToneMapPass.h"

// ---- Sky / Present / UI ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Sky/SkyPass/SkyPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Present/FinalOutputPass/FinalOutputPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/UI/UIPass/UIPass.h"

// ---- Utility / Test ----
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/UpScale/UpScalePass/UpScalePass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Utility/BlendPass/BlendPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Utility/CopyPass/CopyPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Utility/MonitorPass/MonitorPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Test/TestClearPass/TestClearPass.h"
#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Test/TestGBufferPass/TestGBufferPass.h"

// 実行インスタンス側のパスを探すのに要る(モニターの中身の借り先)
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"

namespace Editor::Inspector
{
	using namespace Engine::Graphics::Pipeline;

	namespace
	{
		//----------------------------------------------------------------------------------
		// 説明だけを出すパス用の土台
		//
		// 触れる設定を持たないものが11個あるので、行数を増やさないためにまとめてある
		//----------------------------------------------------------------------------------
		template<class TPass>
		class NoteOnlyEditor : public PassEditor<TPass>
		{
		public:
			explicit NoteOnlyEditor(std::initializer_list<const char*> a_notes)
				: m_notes(a_notes)
			{}

		protected:
			EPassEditResult OnDrawDetail(TPass& a_pass) override
			{
				(void)a_pass;
				for (const char* _pNote : m_notes) Engine::EditorField::HelpText("%s", _pNote);
				return EPassEditResult::None;
			}

		private:
			std::vector<const char*> m_notes = {};
		};

		//----------------------------------------------------------------------------------
		// 出力リソース名の入力欄
		//
		// 同一性は「作ったパス + 出力ピン」で決まるので、ここが被っても中身は混ざらない。
		// リソース一覧やデバッグ表示で見分けるためのラベル
		//----------------------------------------------------------------------------------
		bool DrawResourceName(std::string& a_name)
		{
			char _nameBuf[128] = {};
			std::snprintf(_nameBuf, sizeof(_nameBuf), "%s", a_name.c_str());

			if (!Engine::EditorField::Field("ResourceName", _nameBuf, sizeof(_nameBuf))) return false;

			a_name = _nameBuf;
			return true;
		}

		// 有効/無効のチェックボックス : CB では int で持っているので橋渡しする
		bool DrawEnableCheck(int& a_enable, const char* a_pLabel = "Enable")
		{
			bool _isEnable = (a_enable != 0);
			if (!Engine::EditorField::Field(a_pLabel, _isEnable)) return false;

			a_enable = _isEnable ? 1 : 0;
			return true;
		}

		//==================================================================================
		//
		// Geometry
		//
		//==================================================================================
		class ZPreEditor : public PassEditor<ZPrePass>
		{
		protected:
			EPassEditResult OnDrawDetail(ZPrePass& a_pass) override
			{
				Engine::EditorField::HelpText("不透明モデルの深度だけを書きます");
				Engine::EditorField::Value("PassIndex", "%d", static_cast<int>(a_pass.GetPassIndex()));
				return EPassEditResult::None;
			}
		};

		class GBufferEditor : public PassEditor<GBufferPass>
		{
		protected:
			EPassEditResult OnDrawDetail(GBufferPass& a_pass) override
			{
				Engine::EditorField::HelpText("不透明モデルをGBufferへ描きます");
				Engine::EditorField::Value("PassIndex", "%d", static_cast<int>(a_pass.GetPassIndex()));
				return EPassEditResult::None;
			}
		};

		//==================================================================================
		//
		// Lighting
		//
		//==================================================================================
		class DeferredLightingEditor : public PassEditor<DeferredLightingPass>
		{
		protected:
			EPassEditResult OnDrawDetail(DeferredLightingPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = false;

				_isEdit |= Engine::EditorField::Field("GIIntensity", _params.giIntensity, 0.01f, 0.0f);
				_isEdit |= Engine::EditorField::Field("DirectionalIntensity", _params.directionalIntensity, 0.01f, 0.0f);
				_isEdit |= Engine::EditorField::Field("DielectricF0", _params.dielectricF0, 0.001f, 0.0f, 1.0f);

				_isEdit |= Engine::EditorField::Field("ReflectionIntensity", _params.reflectionIntensity, 0.01f, 0.0f);
				Engine::EditorField::Tooltip("鏡面反射の強さ。どれだけ映るかは金属度・粗さ・視線の角度で決まり、これはその全体に掛かります");

				_isEdit |= Engine::EditorField::Field("ReflectionRoughStart", _params.reflectionRoughnessStart, 0.01f, 0.0f, 1.0f);
				_isEdit |= Engine::EditorField::Field("ReflectionRoughEnd", _params.reflectionRoughnessEnd, 0.01f, 0.0f, 1.0f);
				Engine::EditorField::Tooltip("粗さが Start を超えるとレイトレの反射を周りの間接光(GI)へ置き換え始め、End で置き換え切ります");

				_isEdit |= Engine::EditorField::Field("ReflectionBlurRadius", _params.reflectionBlurRadius, 0.1f, 0.0f, 64.0f);
				Engine::EditorField::Tooltip("粗さ End のときのぼかし半径(反射テクスチャの画素数)。粗さに比例して広がります");

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		// レイトレ2種は PSO が組めているかだけが違う
		template<class TPass>
		class RaytracingEditor : public PassEditor<TPass>
		{
		public:
			explicit RaytracingEditor(const char* a_pNote) : m_pNote(a_pNote) {}

		protected:
			EPassEditResult OnDrawDetail(TPass& a_pass) override
			{
				Engine::EditorField::HelpText("%s", m_pNote);
				if (!a_pass.IsReady()) Engine::EditorField::ErrorText("PSO not ready");
				return EPassEditResult::None;
			}

		private:
			const char* m_pNote = "";
		};

		// フォグ用の影 : 歩数と解像度だけ触れる。
		// 範囲はシーンの影の距離(SceneAmbient の Shadow / Distance)
		class RaytracingVolumeShadowEditor : public PassEditor<RaytracingVolumeShadowPass>
		{
		protected:
			EPassEditResult OnDrawDetail(RaytracingVolumeShadowPass& a_pass) override
			{
				Engine::EditorField::HelpText("フォグ用に、視線に沿った日なたの割合を作ります(SceneVolumetricFogPass の VolumeShadow へ繋ぐ)");

				auto& _params = a_pass.RefParams();

				int _steps = static_cast<int>(_params.stepCount);
				const bool _isParam = Engine::EditorField::Field("Steps", _steps, 0.2f, 0, 64);
				Engine::EditorField::Tooltip("視線を歩く歩数(1歩ごとにレイを1本)。0 で作らない");
				if (_isParam) _params.stepCount = static_cast<uint32_t>(_steps);

				static constexpr const char* ITEMS[] = { "1/1", "1/2", "1/4", "1/8" };
				static constexpr uint32_t VALUES[] = { 1, 2, 4, 8 };

				int _index = -1;
				for (int _i = 0; _i < static_cast<int>(std::size(VALUES)); ++_i)
				{
					if (VALUES[_i] == _params.resolutionDivisor) _index = _i;
				}

				if (Engine::EditorField::Combo("Resolution", _index, ITEMS) && _index >= 0)
				{
					// テクスチャを作り直すので組み直しが要る
					_params.resolutionDivisor = VALUES[_index];
					a_pass.ApplyResolution();
					return EPassEditResult::Structure;
				}
				Engine::EditorField::Tooltip("描画解像度の何分の1で回すか。フォグの影は低周波なので 1/4 で足ります");

				return _isParam ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		// ボリュメトリックフォグ : 回す解像度だけ触れる。
		// フォグとダストの値はシーンの環境設定(SceneAmbientPanel)の持ち物
		class SceneVolumetricFogEditor : public PassEditor<SceneVolumetricFogPass>
		{
		protected:
			EPassEditResult OnDrawDetail(SceneVolumetricFogPass& a_pass) override
			{
				Engine::EditorField::HelpText("フォグとグラウンドダストの設定はシーンの環境設定(SceneAmbientPanel)の持ち物です");
				Engine::EditorField::HelpText("GroundDepth を繋がないとチリは出ません");
				Engine::EditorField::HelpText("GroundField を繋がないとチリは衝撃で動きません");

				auto& _params = a_pass.RefParams();

				static constexpr const char* ITEMS[] = { "1/1", "1/2", "1/4" };
				static constexpr uint32_t VALUES[] = { 1, 2, 4 };

				int _index = -1;
				for (int _i = 0; _i < static_cast<int>(std::size(VALUES)); ++_i)
				{
					if (VALUES[_i] == _params.resolutionDivisor) _index = _i;
				}

				if (Engine::EditorField::Combo("Resolution", _index, ITEMS) && _index >= 0)
				{
					// テクスチャを作り直すので組み直しが要る
					_params.resolutionDivisor = VALUES[_index];
					a_pass.ApplyResolution();
					return EPassEditResult::Structure;
				}
				Engine::EditorField::Tooltip("描画解像度の何分の1で回すか。縮めたときは SceneFogCompositePass の SceneDepth も繋いでください(物体の縁にフォグがにじむのを防ぎます)");

				return EPassEditResult::None;
			}
		};

		// 鏡面反射 : レイの届く距離と、当たった先の影だけ触れる
		class RaytracingReflectionEditor : public PassEditor<RaytracingReflectionPass>
		{
		protected:
			EPassEditResult OnDrawDetail(RaytracingReflectionPass& a_pass) override
			{
				Engine::EditorField::HelpText("視線を法線で反射したレイを1本飛ばし、反射先の色を書きます(1920x1080 固定。DeferredLightingPass の Reflection へ繋ぐ)");

				auto& _params = a_pass.RefParams();
				bool _isEdit = false;

				_isEdit |= Engine::EditorField::Field("MaxDistance", _params.maxDistance, 1.0f, 0.0f);
				Engine::EditorField::Tooltip("レイの届く距離。これより遠いものは映らず、空の色になります");

				_isEdit |= Engine::EditorField::Field("Shadow", _params.isShadow);
				Engine::EditorField::Tooltip("反射先で主光源への影を求めます(レイがもう1本増えます)");

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		// シャドウマップ : 触れるのはアトラスの解像度だけ。
		// 影の求め方・範囲・バイアスはシーンの環境設定(Engine::Scene::SceneAmbient)の持ち物
		class ShadowMapEditor : public PassEditor<ShadowMapPass>
		{
		protected:
			EPassEditResult OnDrawDetail(ShadowMapPass& a_pass) override
			{
				Engine::EditorField::HelpText("主光源から見た深度を 2x2 のカスケードアトラスへ描きます");
				Engine::EditorField::HelpText("影の求め方・範囲・バイアスはシーンの環境設定(SceneAmbientPanel)で設定します");

				auto& _params = a_pass.RefParams();

				// アトラス全体の1辺。タイル(カスケード1枚)はこの半分になる
				static constexpr const char* ITEMS[] = { "1024", "2048", "4096", "8192" };
				static constexpr uint32_t VALUES[] = { 1024, 2048, 4096, 8192 };

				int _index = -1;
				for (int _i = 0; _i < static_cast<int>(std::size(VALUES)); ++_i)
				{
					if (VALUES[_i] == _params.resolution) _index = _i;
				}

				if (!Engine::EditorField::Combo("Resolution", _index, ITEMS)) return EPassEditResult::None;
				if (_index < 0) return EPassEditResult::None;

				// テクスチャを作り直すので組み直しが要る
				_params.resolution = VALUES[_index];
				a_pass.ApplyResolution();
				return EPassEditResult::Structure;
			}
		};

		//==================================================================================
		//
		// Denoise
		//
		//==================================================================================
		// 空間デノイズ2種は中身が同じ
		template<class TPass>
		class SpatialDenoiseEditor : public PassEditor<TPass>
		{
		protected:
			EPassEditResult OnDrawDetail(TPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isParam = false;

				const bool _isStructure = DrawResourceName(_params.resourceName);

				// 段ごとに 1, 2, 4, 8... と変える
				_isParam |= Engine::EditorField::Field("StepSize", _params.cb.stepSize, 1, 1, 64);
				_isParam |= Engine::EditorField::Field("PhiDepth", _params.cb.phiDepth, 0.01f, 0.0f);
				_isParam |= Engine::EditorField::Field("PhiNormal", _params.cb.phiNormal, 0.1f, 0.0f);
				_isParam |= Engine::EditorField::Field("PhiColor", _params.cb.phiColor, 0.01f, 0.0f);

				if (_isStructure)
				{
					a_pass.ApplyResourceName();
					return EPassEditResult::Structure;
				}
				return _isParam ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		// 時間デノイズ2種も中身が同じ
		template<class TPass>
		class TemporalAccumulationEditor : public PassEditor<TPass>
		{
		protected:
			EPassEditResult OnDrawDetail(TPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = false;

				_isEdit |= Engine::EditorField::Field("PhiDepth", _params.phiDepth, 0.01f, 0.0f);
				_isEdit |= Engine::EditorField::Field("PhiNormal", _params.phiNormal, 0.1f, 0.0f);
				_isEdit |= Engine::EditorField::Field("BlendRate", _params.blendRate, 0.01f, 0.0f, 1.0f);

				Engine::EditorField::HelpText("HistoryOut を History へ繋いでください");
				Engine::EditorField::HelpText("(Temporal なので前フレームのぶんが入ります)");

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		//==================================================================================
		//
		// Bloom
		//
		//==================================================================================
		class BloomExtractEditor : public PassEditor<BloomExtractPass>
		{
		protected:
			EPassEditResult OnDrawDetail(BloomExtractPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = DrawEnableCheck(_params.enable);

				_isEdit |= Engine::EditorField::Field("Threshold", _params.threshold, 0.01f, 0.0f);
				_isEdit |= Engine::EditorField::Field("SoftKnee", _params.softKnee, 0.01f, 0.0f, 1.0f);

				// 強さは合成側で効く。抽出側は同じCBを使うので並びを合わせて持っている
				_isEdit |= Engine::EditorField::Field("Intensity", _params.intensity, 0.01f, 0.0f);

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		class BloomCompositeEditor : public PassEditor<BloomCompositePass>
		{
		protected:
			EPassEditResult OnDrawDetail(BloomCompositePass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = DrawEnableCheck(_params.enable);

				_isEdit |= Engine::EditorField::Field("Intensity", _params.intensity, 0.01f, 0.0f);

				// 抽出のしきい値は BloomExtractPass 側。合成では使わないが並びを合わせて持っている
				Engine::EditorField::HelpText("Threshold / SoftKnee は BloomExtractPass 側");

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		//==================================================================================
		//
		// Blur
		//
		//==================================================================================
		class GaussianBlurEditor : public PassEditor<GaussianBlurPass>
		{
		protected:
			EPassEditResult OnDrawDetail(GaussianBlurPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isParam = false;

				bool _isStructure = DrawResourceName(_params.resourceName);

				// 解像度が変わるとテクスチャを作り直すので組み直しが要る
				if (Engine::EditorField::Field("OutputScale", _params.outputScale, 0.01f, 0.01f, 1.0f)) _isStructure = true;

				_isParam |= Engine::EditorField::Field("Sigma", _params.sigma, 0.01f, 0.01f, 16.0f);
				_isParam |= Engine::EditorField::Field("TapRadius", _params.tapRadius, 1, 1, 8);

				if (_isStructure)
				{
					a_pass.ApplyOutputScale();
					return EPassEditResult::Structure;
				}
				return _isParam ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		class RadialBlurEditor : public PassEditor<RadialBlurPass>
		{
		protected:
			EPassEditResult OnDrawDetail(RadialBlurPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = DrawEnableCheck(_params.enable);

				_isEdit |= Engine::EditorField::Field("BlurCenter", _params.blurCenter, 0.01f);
				_isEdit |= Engine::EditorField::Field("Strength", _params.strength, 0.001f, 0.0f, 1.0f);
				_isEdit |= Engine::EditorField::Field("SampleCount", _params.sampleCount, 1, 1, 64);
				_isEdit |= Engine::EditorField::Field("Radius", _params.radius, 0.01f, 0.0f, 2.0f);
				_isEdit |= Engine::EditorField::Field("Falloff", _params.falloff, 0.01f, 0.0f, 8.0f);

				// 値が変わるだけなのでグラフは組み直さない
				Engine::EditorField::HelpText("カメラが RadialBlurComponent を持つあいだはそちらの値が優先される");

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		//==================================================================================
		//
		// Distortion / ScanLine / DoF / ToneMap
		//
		//==================================================================================
		class FishEyeEditor : public PassEditor<FishEyePass>
		{
		protected:
			EPassEditResult OnDrawDetail(FishEyePass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = DrawEnableCheck(_params.enable);

				_isEdit |= Engine::EditorField::Field("Center", _params.center, 0.01f);

				// 正で樽型、負で糸巻き型
				_isEdit |= Engine::EditorField::Field("Strength", _params.strength, 0.01f, -2.0f, 2.0f);

				Engine::EditorField::HelpText("カメラが FishEyeComponent を持つあいだはそちらの値が優先される");

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		class ScanLineEditor : public PassEditor<ScanLinePass>
		{
		protected:
			EPassEditResult OnDrawDetail(ScanLinePass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = DrawEnableCheck(_params.enable);

				// 効果ごとに掛けるかどうかを選べる
				_isEdit |= DrawEnableCheck(_params.enableScanLine, "ScanLine");
				_isEdit |= Engine::EditorField::Field("ScanLine Opacity", _params.opacityScanLine, 0.01f, 0.0f, 1.0f);

				_isEdit |= DrawEnableCheck(_params.enableNoise, "Noise");
				_isEdit |= Engine::EditorField::Field("Noise Opacity", _params.opacityNoise, 0.01f, 0.0f, 1.0f);

				_isEdit |= DrawEnableCheck(_params.enableFlickering, "Flickering");
				_isEdit |= Engine::EditorField::Field("Flickering Speed", _params.flickeringSpeed, 0.1f, 0.0f, 100.0f);
				_isEdit |= Engine::EditorField::Field("Flickering Strength", _params.flickeringStrength, 0.01f, 0.0f, 1.0f);

				Engine::EditorField::HelpText("Mask に UIPass の ScanLineMask を繋ぐと、ScanLine を選んだUIにだけ掛かる");
				Engine::EditorField::HelpText("Mask を繋がなければ画面全体に掛かる");

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		// CoC と DoF は同じ調整値を持つ(両方を合わせる必要がある)
		template<class TPass>
		class DoFParamEditor : public PassEditor<TPass>
		{
		public:
			explicit DoFParamEditor(const char* a_pPairNote) : m_pPairNote(a_pPairNote) {}

		protected:
			EPassEditResult OnDrawDetail(TPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = DrawEnableCheck(_params.enable);

				_isEdit |= Engine::EditorField::Field("FocusDistance", _params.focusDistance, 0.1f, 0.0f);
				_isEdit |= Engine::EditorField::Field("FocusRange", _params.focusRange, 0.1f, 0.0f);
				_isEdit |= Engine::EditorField::Field("NearRange", _params.nearRange, 0.1f, 0.0f);
				_isEdit |= Engine::EditorField::Field("FarRange", _params.farRange, 0.1f, 0.0f);
				_isEdit |= Engine::EditorField::Field("MaxBlurRadius", _params.maxBlurRadius, 0.1f, 0.0f);

				Engine::EditorField::HelpText("%s", m_pPairNote);
				Engine::EditorField::HelpText("カメラが FocusParamComponent を持つあいだはそちらの値が優先される");

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}

		private:
			const char* m_pPairNote = "";
		};

		class ToneMapEditor : public PassEditor<ToneMapPass>
		{
		protected:
			EPassEditResult OnDrawDetail(ToneMapPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = false;

				int _type = static_cast<int>(_params.type);
				if (Engine::EditorField::Field("Type", _type, 1, 0, 8)) { _params.type = static_cast<uint32_t>(_type); _isEdit = true; }

				_isEdit |= Engine::EditorField::Field("Exposure", _params.exposure, 0.01f, 0.0f);
				_isEdit |= Engine::EditorField::Field("WhitePoint", _params.whitePoint, 0.1f, 0.0f);

				Engine::EditorField::HelpText("HDR -> LDR。これより後ろにポストプロセスを置かないこと");

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		//==================================================================================
		//
		// Present
		//
		//==================================================================================
		class FinalOutputEditor : public PassEditor<FinalOutputPass>
		{
		protected:
			EPassEditResult OnDrawDetail(FinalOutputPass& a_pass) override
			{
				Engine::EditorField::HelpText("このノードの絵がカメラの最終出力になります");

				const Slot* _pInSlot = a_pass.FindInputSlot(Pass::MakeSlotID(FinalOutputPass::INPUT_NAME));
				if (_pInSlot && _pInSlot->IsConnected())	Engine::EditorField::Value("Input", "%s", _pInSlot->name.c_str());
				else										Engine::EditorField::HelpText("Input : (not connected)");

				// 触れる設定を持たない
				return EPassEditResult::None;
			}
		};

		//==================================================================================
		//
		// Utility
		//
		//==================================================================================
		class UpScaleEditor : public PassEditor<UpScalePass>
		{
		protected:
			EPassEditResult OnDrawDetail(UpScalePass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				bool _isEdit = false;

				Engine::EditorField::HelpText("ScaleRatio : %.2f (繋がれた解像度から自動)", _params.scaleRatio);
				_isEdit |= Engine::EditorField::Field("DepthSigma", _params.depthSigma, 0.001f, 0.0f);
				_isEdit |= Engine::EditorField::Field("NormalPower", _params.normalPower, 0.1f, 0.0f);

				return _isEdit ? EPassEditResult::Param : EPassEditResult::None;
			}
		};

		class CopyEditor : public PassEditor<CopyPass>
		{
		protected:
			EPassEditResult OnDrawDetail(CopyPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();

				bool _isStructure = DrawResourceName(_params.resourceName);

				if (Engine::EditorField::ComboScope _combo{ "Format", CopyPass::ToFormatName(_params.formatIndex) })
				{
					for (int _i = 0; _i < CopyPass::FORMAT_COUNT; ++_i)
					{
						const bool _isSelected = (_params.formatIndex == _i);
						if (ImGui::Selectable(CopyPass::ToFormatName(_i), _isSelected))
						{
							_params.formatIndex = _i;
							_isStructure = true;
						}
						if (_isSelected) ImGui::SetItemDefaultFocus();
					}
				}

				if (Engine::EditorField::Field("History", _params.isTemporal)) _isStructure = true;
				Engine::EditorField::Tooltip("入力と同じフォーマット・大きさにすること");

				if (!_isStructure) return EPassEditResult::None;

				// リソースを作り直すので組み直しが要る
				a_pass.ApplyOutput();
				return EPassEditResult::Structure;
			}
		};

		class MonitorEditor : public PassEditor<MonitorPass>
		{
		protected:
			EPassEditResult OnDrawDetail(MonitorPass& a_pass) override
			{
				auto& _params = a_pass.RefParams();
				EPassEditResult _result = EPassEditResult::None;

				if (DrawResourceName(_params.resourceName))
				{
					a_pass.ApplyOutput();

					// リソースを作り直すので組み直しが要る
					_result = EPassEditResult::Structure;
				}

				// 写しを取るかどうかは実行インスタンス側の振る舞いなので、値を配る必要がある
				if (Engine::EditorField::Field("Preview", _params.isPreview) && _result == EPassEditResult::None)
				{
					_result = EPassEditResult::Param;
				}

				// 表示の大きさは設計図側でしか使わないので、配らない
				Engine::EditorField::Field("PreviewWidth", _params.previewWidth, 1.0f, 64.0f, 1024.0f);

				Engine::EditorField::HelpText("フォーマットと大きさは入力から受け取る");

				return _result;
			}

			void OnDrawNode(MonitorPass& a_pass) override
			{
				const auto& _params = a_pass.RefParams();

				if (!_params.isPreview)
				{
					Engine::EditorField::HelpText("Preview : off");
					return;
				}

				// 設計図のパスは実行されないので、中身は実行インスタンスから借りる。
				// パスの中からシングルトンを引かせないよう、探し先はここから渡す
				const MonitorPass* _pView =
					a_pass.ResolveViewSource(MainEngine::Instance().RefGraphicsEngine());
				const Resource::Texture* _pTex = _pView ? _pView->GetPreviewTexture() : nullptr;

				if (!_pTex || !_pTex->GetImGuiSRV().IsValid())
				{
					// カメラがこのパイプラインを回していないあいだはここに来る
					Engine::EditorField::HelpText("表示するものがありません");
					return;
				}

				const D3D12_RESOURCE_DESC& _desc = _pTex->GetDesc();
				if (_desc.Width == 0 || _desc.Height == 0) return;

				const auto _gpuHandle =
					EditorHelper::GetImGuiTexHandle(_pTex->GetImGuiSRV());

				const float _aspect = static_cast<float>(_desc.Height) / static_cast<float>(_desc.Width);
				const ImVec2 _size(_params.previewWidth, _params.previewWidth * _aspect);

				ImGui::Image(static_cast<ImTextureID>(_gpuHandle.ptr), _size);

				Engine::EditorField::HelpText("%llu x %u", _desc.Width, _desc.Height);
			}
		};

		//==================================================================================
		//
		// Test
		//
		//==================================================================================
		class TestClearEditor : public PassEditor<TestClearPass>
		{
		protected:
			EPassEditResult OnDrawDetail(TestClearPass& a_pass) override
			{
				Engine::EditorField::HelpText("出力テクスチャを指定色で塗るだけのパスです");

				// 自分で塗るので、色を変えてもリソースの作り直しは要らない。
				// Param を返すと、カメラが回している実行インスタンスへ値だけが写る
				if (EditorField::Field("ClearColor", a_pass.RefParams().clearColor)) return EPassEditResult::Param;
				return EPassEditResult::None;
			}
		};

		class TestGBufferEditor : public PassEditor<TestGBufferPass>
		{
		protected:
			EPassEditResult OnDrawDetail(TestGBufferPass& a_pass) override
			{
				bool _isEdit = false;

				if (ImGui::TreeNodeEx("Output", ImGuiTreeNodeFlags_DefaultOpen))
				{
					for (Slot& _out : a_pass.RefOutputSlots())
					{
						ImGui::PushID(_out.pinID);
						Engine::EditorField::Text("%s : %s", _out.pinName.c_str(), _out.name.c_str());
						// どれもリソースの要件を変えるので、触られたら組み直しが要る
						_isEdit |= EditorField::Field("LoadOp", _out.loadOp);
						_isEdit |= EditorField::Field("Access", _out.accessType);
						_isEdit |= Engine::EditorField::Field("Scale", _out.scale, 0.01f, 0.01f, 4.0f);
						Engine::EditorField::Line();
						ImGui::PopID();
					}
					ImGui::TreePop();
				}

				if (ImGui::TreeNodeEx("Input", ImGuiTreeNodeFlags_DefaultOpen))
				{
					for (const Slot& _in : a_pass.GetInputSlots())
					{
						if (_in.IsConnected())	Engine::EditorField::Text("%s : %s", _in.pinName.c_str(), _in.name.c_str());
						else					Engine::EditorField::HelpText("%s : (not connected)", _in.pinName.c_str());
					}
					ImGui::TreePop();
				}

				// どれもリソースの要件が変わるので、組み直しが要る
				return _isEdit ? EPassEditResult::Structure : EPassEditResult::None;
			}
		};
	}

	//======================================================================================
	//
	// 登録
	//
	//======================================================================================
	void RegisterBuiltinPassEditors(PassEditorRegistry& a_registry)
	{
		// ---- Geometry ----
		a_registry.Register<ZPrePass, ZPreEditor>();
		a_registry.Register<GBufferPass, GBufferEditor>();
		a_registry.Register<ParticlePass, NoteOnlyEditor<ParticlePass>>(std::initializer_list<const char*>{ "発生と更新は GraphicsEngine 側で毎フレーム1回走ります", "このパスは描画だけを担当します" });
		a_registry.Register<DebugLinePass, NoteOnlyEditor<DebugLinePass>>(std::initializer_list<const char*>{ "当たり判定やレイのデバッグ線を描きます" });

		// ---- Lighting ----
		a_registry.Register<DeferredLightingPass, DeferredLightingEditor>();
		a_registry.Register<RaytracingGIPass, RaytracingEditor<RaytracingGIPass>>("レイを飛ばして間接光を求めます(ハーフ解像度)");
		a_registry.Register<RaytracingShadowPass, RaytracingEditor<RaytracingShadowPass>>("主光源へレイを1本飛ばして遮蔽を求めます");
		a_registry.Register<RaytracingVolumeShadowPass, RaytracingVolumeShadowEditor>();
		a_registry.Register<RaytracingReflectionPass, RaytracingReflectionEditor>();
		a_registry.Register<ShadowMapPass, ShadowMapEditor>();
		a_registry.Register<ShadowMapMaskPass, NoteOnlyEditor<ShadowMapMaskPass>>(std::initializer_list<const char*>{ "Shadow 入力にレイトレの影(デノイズ後)を繋いでください", "影の求め方が ShadowMap のときだけ上書きします" });

		// ---- Denoise ----
		a_registry.Register<GISpatialDenoisePass, SpatialDenoiseEditor<GISpatialDenoisePass>>();
		a_registry.Register<ShadowSpatialDenoisePass, SpatialDenoiseEditor<ShadowSpatialDenoisePass>>();
		a_registry.Register<GITemporalAccumulationPass, TemporalAccumulationEditor<GITemporalAccumulationPass>>();
		a_registry.Register<ShadowTemporalAccumulationPass, TemporalAccumulationEditor<ShadowTemporalAccumulationPass>>();

		// ---- PostEffect ----
		a_registry.Register<TAAPass, NoteOnlyEditor<TAAPass>>(std::initializer_list<const char*>{ "History 出力を History 入力へ繋いでください", "(Temporal なので前フレームのぶんが入ります)" });
		a_registry.Register<BloomExtractPass, BloomExtractEditor>();
		a_registry.Register<BloomCompositePass, BloomCompositeEditor>();
		a_registry.Register<KawaseBlurPass, NoteOnlyEditor<KawaseBlurPass>>(std::initializer_list<const char*>{ "縮小4段をまとめて1枚のブルームにします", "Down0 が一番大きい段(1/2)です" });
		a_registry.Register<GaussianBlurPass, GaussianBlurEditor>();
		a_registry.Register<RadialBlurPass, RadialBlurEditor>();
		a_registry.Register<FishEyePass, FishEyeEditor>();
		a_registry.Register<ScanLinePass, ScanLineEditor>();
		a_registry.Register<CoCPass, DoFParamEditor<CoCPass>>("DoFPass と同じ値にすること");
		a_registry.Register<DoFPass, DoFParamEditor<DoFPass>>("CoCPass と同じ値にすること");
		a_registry.Register<SceneVolumetricFogPass, SceneVolumetricFogEditor>();
		a_registry.Register<SceneFogCompositePass, NoteOnlyEditor<SceneFogCompositePass>>(std::initializer_list<const char*>{ "合成の強さと有効/無効はシーンの環境設定(SceneAmbientPanel)の持ち物です", "トーンマップ前(HDR)に置いてください", "SceneDepth にフォグのパスと同じ深度を繋ぐと、縮小したフォグを物体の縁でにじませずに引き伸ばします" });
		a_registry.Register<GroundFieldPass, NoteOnlyEditor<GroundFieldPass>>(std::initializer_list<const char*>{ "カメラを中心にした範囲の衝撃を、真上から見たテクスチャへ書きます", "Field を SceneVolumetricFogPass の GroundField へ繋いでください" });
		a_registry.Register<ToneMapPass, ToneMapEditor>();

		// ---- Sky / Present / UI ----
		a_registry.Register<SkyPass, NoteOnlyEditor<SkyPass>>(std::initializer_list<const char*>{ "空の設定はシーンの環境設定(SceneAmbientPanel)の持ち物です" });
		a_registry.Register<FinalOutputPass, FinalOutputEditor>();
		a_registry.Register<UIPass, NoteOnlyEditor<UIPass>>(std::initializer_list<const char*>{ "深度を持たないので、積んだ順がそのまま前後になります", "ScanLineMask を ScanLinePass の Mask へ繋ぐと、ScanLine を選んだUIにだけ掛かります" });

		// ---- Utility / Test ----
		a_registry.Register<UpScalePass, UpScaleEditor>();
		a_registry.Register<BlendPass, NoteOnlyEditor<BlendPass>>(std::initializer_list<const char*>{});
		a_registry.Register<CopyPass, CopyEditor>();
		a_registry.Register<MonitorPass, MonitorEditor>();
		a_registry.Register<TestClearPass, TestClearEditor>();
		a_registry.Register<TestGBufferPass, TestGBufferEditor>();
	}
}
