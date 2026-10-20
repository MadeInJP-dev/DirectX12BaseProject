#pragma once

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	//======================================================================================
	// DeferredLightingPass
	//
	// GBuffer と影・GI を合わせて、最終的な色(AfterLighting)を作る。
	//
	// ライトの配列はグラフのリソースではないので、スロットには乗らない。
	// GraphicsEngine が毎フレーム詰め直したものをここで直接張る
	//======================================================================================
	class DeferredLightingPass : public Pass
	{
	public:
		~DeferredLightingPass() override = default;

		void SetupSlots() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;

		struct LightingOptionCB
		{
			float giIntensity;			// 間接光の強さ
			float directionalIntensity;	// 平行光の強さ
			float dielectricF0;			// 非金属の基準反射率
			float reflectionIntensity;	// 鏡面反射(間接光の鏡面ぶん)の強さ

			// 粗さに応じた鏡面反射の扱い。
			// レイトレの反射は鏡なので、粗い面ほどぼかし、
			// 粗さ Start → End で周りの間接光(GI)へ置き換える
			float reflectionRoughnessStart;
			float reflectionRoughnessEnd;
			float reflectionBlurRadius;	// 粗さ End のときのぼかし半径(反射テクスチャの画素数)
			float pad;

			// リムライト(主光源の逆光で輪郭を光らせる)
			Math::Vector3 rimColor;		// 光の色に掛ける色
			float rimIntensity;			// 強さ
			float rimPower;				// 輪郭の細さ(大きいほど縁だけに寄る)
			float rimBackLightStart;	// 逆光の度合いがこの値から効き始める(-1..1)
			float rimBackLightEnd;		// この値で効き切る
			float rimShadowRate;		// 影で消す割合(0 = 影を無視, 1 = 影で消す)。光に背を向けた面には効かない
		};

		// 編集対象の値 : エディターはここだけを触る。
		// 実体は下の m_cb で、シェーダーへはそのまま送られる
		using Params = LightingOptionCB;
		Params& RefParams() { return m_cb; }

	private:

		// ライティングの調整値
		// ※ HLSL の LightingOptionData と並びを合わせること
		LightingOptionCB m_cb = {
			1.0f, 1.0f, 0.04f, 1.0f,
			0.4f, 0.8f, 12.0f, 0.0f,
			{ 1.0f, 1.0f, 1.0f }, 0.5f,
			3.0f, 0.0f, 0.8f, 1.0f,
		};
	};
}
