#pragma once
namespace Engine::Resource
{
	//==========================================================
	// レイトレ用データ
	//==========================================================
	struct RaytracingMesh
	{
		/// <summary>
		/// BLASの構築コマンドをコンテキストのコンピュートリストへ積む
		/// 参照するメガバッファへの転送はコピーリスト側で積まれるため、
		/// 実行時にキュー間の待ちを張る必要がある(RenderDevice::EndAsyncBuildBatchが担当)
		/// </summary>
		void Create(
			const ResourceBuildContext& a_ctx,
			const std::vector<MeshSubset>& a_subset
		);

		// 解放
		void Release();

		Engine::Graphics::Raytracing::BLAS blas;
		RangeHandle<MeshVertexPacked> vertexHandle = {};
		RangeHandle<MeshSkinVertex> skinVertexHandle = {};		// ボーン割り当て(スキンメッシュだけ。それ以外は無効)
		RangeHandle<uint32_t> indexHandle = {};
	};
}