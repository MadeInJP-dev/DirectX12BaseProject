#pragma once

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	//======================================================================================
	// TransparentPass
	//
	// 半透明のモデル(マテリアルの透明モードが Blend のもの)を、
	// ライティング済みの色の上へ奥から手前の順に重ねるパス。
	// 置き場所はライティングの後・ポストプロセス(TAA など)の前。
	//
	// GBuffer は1画素に1枚の面しか持てないので、半透明はディファードに乗せられない。
	// ここでピクセルシェーダーが光を計算して(前方ライティング)、色を直接合成する。
	//
	// 並び順はパスでは決めない。GraphicsEngine::Execute がパスを回す前に
	// DrawLists::SortTransparentItems で遠い順に並べ終えていて、ここは先頭から描くだけ。
	//
	// 深度は読むだけで書かない : 不透明の物には隠され、半透明同士は描く順で重なる
	//
	// エフェクトのトレイル(動いた跡の帯)もここで描く。半透明のモデルを描いた後に、
	// 帯どうしを遠い順に重ねる(モデルとトレイルの間の前後は並べない)
	//======================================================================================
	class TransparentPass : public Pass
	{
	public:
		~TransparentPass() override = default;

		// PSO の鍵はこの名前で引く(表示名とは別)
		const char* GetShadingPassName() const override { return "Transparent"; }

		void SetupSlots() override;
		void OnLinksResolved() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;

	private:

		// モデル用のルートシグネチャ : PSOマネージャーから引いたハンドルを持つ
		Handle<ID3D12RootSignature> m_meshRootSigHandle = {};

		// トレイル用 : VS/PS の PSO と、そのルートシグネチャ
		Handle<ID3D12PipelineState> m_trailPSOHandle = {};
		Handle<ID3D12RootSignature> m_trailRootSigHandle = {};
	};
}
