#pragma once

#include "Engine/Common/Handle.h"

namespace Engine::Resource
{
	class ResourceManager;
}

namespace Engine::Graphics::Particle
{
	class GPUParticlePool;
	class ParticleDrawTable;
}

namespace Engine::Graphics
{
	class GraphicsEngine;
	class RenderContext;
	class PipelineStateManager;

	//======================================================================================
	// GPUパーティクルのシミュレーション
	//
	// 発生(Emit)と更新(Update)は、カメラに依存せずフレームに1回で足りる計算なので
	// レンダーグラフのパスにはしていない。GraphicsEngine が持ち、直接呼ぶ。
	//
	// 2つを1つの関数にまとめてあるのは、間に挟むUAVバリアを外せなくするため。
	// 両者は同じ deadList / counter を触るので、バリアが無いと
	// Dispatch がGPU上で並列に走り、空きスロットが少しずつ減ってエミットが先細りする。
	// パスに分かれていると「間に別のパスが入る」余地が残るが、
	// ここへまとめておけば順序とバリアが崩れようがない
	//======================================================================================
	class ParticleSimulation
	{
	public:

		// ルートシグネチャとPSOの用意(初期化時に1回)
		// a_resourceManager : シェーダーを読み込む先。更新でもアセットの値を引くので持っておく
		void Setup(PipelineStateManager* a_pPSOManager, Resource::ResourceManager& a_resourceManager);

		// 実行(毎フレーム1回) : 発生 -> UAVバリア -> 更新
		void Execute(GraphicsEngine* a_pGE, RenderContext* a_pCtx);

	private:

		// シェーダーからルートシグネチャとコンピュートPSOを起こす(Setup の中で、PSOの組ごとに呼ぶ)
		bool SetupComputeShader(
			const std::string& a_csPath,
			const std::string& a_psoName,
			Handle<ID3D12RootSignature>& a_outRootSig,
			Handle<ID3D12PipelineState>& a_outPSO);

		//------------------------------------------------------------------
		// 粒を加算・半透明を問わず全アセットぶん1本の一覧へ集め、カメラから遠い順に並べる
		//
		//   BeginGatherSortList : 集める先を UAV にして、集めた数(引数の[1])を 0 に戻す(更新の前に1回)
		//   GatherSortList      : 1プールぶんを集める(そのプールの Update の後)
		//   SortParticles        : 集めたものを並べて描く一覧へ書き出し、描ける状態にする(全プールの後に1回)
		//------------------------------------------------------------------
		void BeginGatherSortList(RenderContext* a_pCtx, Particle::ParticleDrawTable& a_table, UINT a_quadIndexCount);
		void GatherSortList(
			RenderContext* a_pCtx,
			Particle::ParticleDrawTable& a_table,
			Particle::GPUParticlePool& a_pool,
			uint32_t a_recordIndex,
			UINT a_slotSRVIndex,
			const Math::Vector3& a_cameraPos);
		void SortParticles(RenderContext* a_pCtx, Particle::ParticleDrawTable& a_table, const Math::Vector3& a_cameraPos);

	private:

		PipelineStateManager* m_pPSOManager = nullptr;
		Resource::ResourceManager* m_pResourceManager = nullptr;	// アセットの値を引く(借り物)

		// 各シェーダーに対応する組み合わせ
		Handle<ID3D12RootSignature> m_emitRootSig = {};
		Handle<ID3D12PipelineState> m_emitPSO = {};

		Handle<ID3D12RootSignature> m_updateRootSig = {};
		Handle<ID3D12PipelineState> m_updatePSO = {};

		Handle<ID3D12RootSignature> m_resetRootSig = {};
		Handle<ID3D12PipelineState> m_resetPSO = {};

		// プールを伸ばしたあと、増えた範囲を使えるようにする
		Handle<ID3D12RootSignature> m_growRootSig = {};
		Handle<ID3D12PipelineState> m_growPSO = {};

		// 粒を集めて遠い順に並べる
		Handle<ID3D12RootSignature> m_sortRootSig = {};
		Handle<ID3D12PipelineState> m_sortPSO = {};

		// このフレームの乱数の種
		uint32_t m_frameCounter = 0;
	};
}
