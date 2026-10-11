#pragma once

#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshAllocationHandle.h"

namespace Engine::Graphics::D3D12
{
	class DescriptorHeapManager;
}

namespace Engine::Graphics
{
	class FrameManager;

	struct BufferSizeDesc
	{
		size_t staticVertexBufferSize = 0;
		size_t indexBufferSize = 0;
		UINT animatedVertexBufferSize = 0;
	};

	/// <summary>
	/// メッシュシェーダーのためのメガバッファを管理するためのクラス
	/// メッシュシェーダーで描画するものはここからハンドルをもらい受ける必要がある
	/// 静的なオブジェクトのみで、毎フレーム更新されるようなものはレンダーコンテキスト側が保持
	/// </summary>
	class MeshBufferAllocator
	{
	public:
		/// <summary>
		/// ゲーム初期化時にメッシュシェーダー用のメモリ領域を確保する
		/// </summary>
		/// <param name="a_pDevice">デバイスポインタ</param>
		/// <param name="a_pFrameManager">
		/// フレームのフェンス(借り物)。領域を返すとき、今フレームが読み終わるまで空けないよう
		/// 終わりにシグナルされる値でタグ付けするのに使う
		/// </param>
		void Init(
			D3D12::Device* a_pDevice,
			D3D12::DescriptorHeapManager* a_pHeapManager,
			const FrameManager* a_pFrameManager,
			D3D12::GraphicsCommandList* a_pCmdList,
			const BufferSizeDesc& a_bufferSizes
		);

		/// <summary>
		/// 解放処理 : ゲーム終了時に呼び出し
		/// </summary>
		void Release();

		
		/// <summary>
		/// 毎フレーム呼ばれる : 遅延開放処理
		/// </summary>
		/// <param name="currentFenceValue">フェンス値</param>
		void UpdateFrame(D3D12::GraphicsCommandList* a_pCmdList,uint64_t a_completedFenceValue);

		/// <summary>
		/// 確保で足した実メモリの割り当て・返すための外しをキューへ流す。
		/// コマンドプールがリストを流す直前に呼ぶ(そのキューで割り当てるバッファのぶんだけ流れる)
		/// </summary>
		/// <param name="a_pFence">流した直後にそのキューがシグナルするフェンス</param>
		/// <param name="a_signalValue">そのときの値 : 外したヒープはこの値を越えてから手放す</param>
		/// <returns>何か流したか</returns>
		bool FlushTileMappings(D3D12_COMMAND_LIST_TYPE a_queueType, D3D12::CommandQueue* a_pQueue, D3D12::Fence* a_pFence, UINT64 a_signalValue);

		//--------------------------------------------------------------------------------------------
		// バッファにアロケート
		//
		// 転送を伴うものはビルドコンテキストを受け取り、そのコピーコマンドリストへ積むだけにする。
		// キューへの実行はバッチを開いた側(モデルのインポート処理など)がまとめて行う。
		//--------------------------------------------------------------------------------------------
		RangeHandle<Resource::MeshVertexFloat> AllocateVertex(const Resource::ResourceBuildContext& a_ctx, const std::vector<Resource::MeshVertexFloat>& a_vertex);		// 静的頂点
		RangeHandle<uint32_t> AllocateIndex(const Resource::ResourceBuildContext& a_ctx, const std::vector<uint32_t>& a_indices);										// インデックス

		RangeHandle<Resource::Meshlet> AllocateMeshlet(const Resource::ResourceBuildContext& a_ctx, const std::vector<Resource::Meshlet>& a_meshlets);					// メッシュレット
		RangeHandle<uint32_t> AllocateUniqueVertIndices(const Resource::ResourceBuildContext& a_ctx, const std::vector<uint32_t>& a_uniqueVertIndices);					// 頂点インデックス
		RangeHandle<DirectX::MeshletTriangle> AllocateTriangles(const Resource::ResourceBuildContext& a_ctx, const std::vector<DirectX::MeshletTriangle>& a_triangles);	// 三角形インデックス
		RangeHandle<DirectX::CullData> AllocateCullData(const Resource::ResourceBuildContext& a_ctx, const std::vector<DirectX::CullData>& a_cullData);					// カリングデータ

		// 転送を伴わないもの : GPU側で書き込むだけの領域確保
		RangeHandle<Resource::MeshVertexFloat> AllocateAnimatedVertex(UINT a_size);											// 動的頂点
		//--------------------------------------------------------------------------------------------
		// ハンドルの返却
		//--------------------------------------------------------------------------------------------
		void ReserveFreeStaticVertex(const RangeHandle<Resource::MeshVertexFloat>& a_handle);
		void ReserveFreeIndex(const RangeHandle<uint32_t>& a_handle);
		void ReserveFreeAnimatedVertex(const RangeHandle<Resource::MeshVertexFloat>& a_handle);

		void ReserveFreeMeshlet(const RangeHandle<Resource::Meshlet>& a_handle);					// メッシュレット
		void ReserveFreeUniqueVertIndices(const RangeHandle<uint32_t>& a_handle);					// 頂点インデックス
		void ReserveFreeTriangles(const RangeHandle<DirectX::MeshletTriangle>& a_handle);			// 三角形インデックス
		void ReserveFreeMeshletCullData(const RangeHandle<DirectX::CullData>& a_handle);			// カリングデータ
		//--------------------------------------------------------------------------------------------
		// バッファアクセス
		//--------------------------------------------------------------------------------------------
		const D3D12::MegaStructuredBuffer<Resource::MeshVertexFloat>& GetStaticVertexBuffer() const { return m_staticVerticesBuffer; }
		const D3D12::MegaStructuredBuffer<uint32_t>& GetIndexBuffer() const { return m_indexBuffer; }
		const D3D12::MegaRWStructuredBuffer<Resource::MeshVertexFloat>& GetAnimatedVertexBuffer() const { return m_animatedVertexBuffer; }
		// モーションベクター用 : 前フレームのスキニング済み頂点(今フレームの値をスキニング前にコピーして保持)
		const D3D12::MegaRWStructuredBuffer<Math::Vector3>& GetPrevAnimatedPositionBuffer() const { return m_prevAnimatedPositionBuffer; }

		D3D12::MegaStructuredBuffer<Resource::MeshVertexFloat>& RefStaticVertexBuffer() { return m_staticVerticesBuffer; }
		D3D12::MegaStructuredBuffer<uint32_t>& RefIndexBuffer() { return m_indexBuffer; }
		D3D12::MegaRWStructuredBuffer<Resource::MeshVertexFloat>& RefAnimatedVertexBuffer() { return m_animatedVertexBuffer; }
		D3D12::MegaRWStructuredBuffer<Math::Vector3>& RefPrevAnimatedPositionBuffer() { return m_prevAnimatedPositionBuffer; }

		const D3D12::MegaStructuredBuffer<Resource::Meshlet>& GetMeshletBuffer() const { return m_meshletBuffer; }
		const D3D12::MegaStructuredBuffer<uint32_t>& GetUniqueVertexIndicesBuffer() const { return m_uniqueVertexIndicesBuffer; }
		const D3D12::MegaStructuredBuffer<DirectX::MeshletTriangle>& GetMeshletTriangleBuffer() const { return m_meshTriangleBuffer; }
		const D3D12::MegaStructuredBuffer<DirectX::CullData>& GetMeshletCullDataBuffer() const { return m_meshletCullDataBuffer; }

		D3D12::MegaStructuredBuffer<Resource::Meshlet>& RefMeshletBuffer() { return m_meshletBuffer; }
		D3D12::MegaStructuredBuffer<uint32_t>& RefUniqueVertexIndicesBuffer() { return m_uniqueVertexIndicesBuffer; }
		D3D12::MegaStructuredBuffer<DirectX::MeshletTriangle>& RefMeshletTriangleBuffer() { return m_meshTriangleBuffer; }
		D3D12::MegaStructuredBuffer<DirectX::CullData>& RefMeshletCullDataBuffer() { return m_meshletCullDataBuffer; }

	private:

		// 返した領域を再利用してよくなるフェンス値(今フレームの終わり)
		uint64_t GetReleaseFenceValue() const;

		// フレームのフェンス(借り物)。実体は GraphicsEngine が持っている
		const FrameManager* m_pFrameManager = nullptr;

		//--------------------------------------------------------------------------------------------
		// メッシュデータ
		//--------------------------------------------------------------------------------------------
		D3D12::MegaStructuredBuffer<Resource::MeshVertexFloat>		m_staticVerticesBuffer;		// 静的な頂点データ
		D3D12::MegaStructuredBuffer<uint32_t>						m_indexBuffer;				// インデックスバッファ
		D3D12::MegaRWStructuredBuffer<Resource::MeshVertexFloat>	m_animatedVertexBuffer;		// スキニング後の頂点バッファ(今フレーム)
		// 前フレームのスキニング後の位置。m_animatedVertexBuffer と同じ要素数・同じオフセットで運用する。
		// モーションベクターが読むのは位置だけなので、頂点まるごとではなく位置(12バイト)だけを持つ。
		// スキニングのコンピュートが、上書きする前の位置(=前フレームの結果)をここへ書き写す
		D3D12::MegaRWStructuredBuffer<Math::Vector3>				m_prevAnimatedPositionBuffer;

		D3D12::MegaStructuredBuffer<Resource::Meshlet>				m_meshletBuffer;			// メッシュレットバッファ
		D3D12::MegaStructuredBuffer<uint32_t>						m_uniqueVertexIndicesBuffer;// メッシュ頂点インデックスバッファ
		D3D12::MegaStructuredBuffer<DirectX::MeshletTriangle>		m_meshTriangleBuffer;		// メッシュのインデックス情報
		D3D12::MegaStructuredBuffer<DirectX::CullData>				m_meshletCullDataBuffer;	// メッシュレットごとのカリング判定
	};
}