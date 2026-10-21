#pragma once

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	//======================================================================================
	// ParticlePass
	//
	// GPUパーティクルを板ポリで描く。
	// 発生と更新はカメラに依存しないので GraphicsEngine の関数側にあり、
	// このパスは「描く」ところだけを持つ(カメラに向けて板を向けるのでカメラ依存)。
	//
	// 色の重ね方(加算・半透明)はアセット単位で選べる。
	// PSO はプリマルチプライドの合成(色 × 1 + 下地 × (1 - α))1つだけで、
	// PS が加算なら α = 0、半透明なら α を出して重ね方を変える。
	// おかげで加算と半透明の粒を同じ一覧で遠い順に並べ、1回で描ける
	//======================================================================================
	class ParticlePass : public Pass
	{
	public:
		~ParticlePass() override = default;

		void SetupSlots() override;
		void OnLinksResolved() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;

	private:

		// プリマルチプライドの合成の PSO(加算も半透明もこれ1つ)。
		// グラフには張らせず(m_psoIndex は無効のまま)、描くときに張る
		Handle<ID3D12PipelineState> m_particlePSO = {};
	};
}
