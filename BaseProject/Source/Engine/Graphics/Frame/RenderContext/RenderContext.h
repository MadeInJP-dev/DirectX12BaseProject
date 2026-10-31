#pragma once
#include "Engine/Graphics/D3D12/CBAllocator/CBAllocator.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/Graphics/Frame/DrawList/MeshDrawData.h"
#include "Engine/Graphics/Frame/DrawList/UIData.h"
#include "Engine/Graphics/Frame/DrawList/TrailData.h"

namespace Engine::Resource
{
	class QuadPolygon;
}

namespace Engine::Graphics::D3D12
{
	class RootSignature;
	class DescriptorHeapManager;
}


namespace Engine::Graphics
{
	// 前方宣言
	class GraphicsEngine;
	class PipelineStateManager;
	class DrawLists;
	class BackBuffer;
	struct LightWeightDrawItem;

	// レンダーコンテキスト作成時に必要な情報
	struct RenderContextDesc
	{
		// D3Dオブジェクトのキャッシュ
		D3D12::Device* pDevice = nullptr;

		// 以下はすべて借り物。実体は GraphicsEngine が持っている
		D3D12::DescriptorHeapManager* pHeapManager = nullptr;		// ビューの置き場
		PipelineStateManager* pPipelineStateManager = nullptr;		// ハンドルからPSO・ルートシグネチャを引く
		const DrawLists* pDrawLists = nullptr;						// 描画アイテムとUIの配列
		const BackBuffer* pBackBuffer = nullptr;					// ビューポート・シザー矩形
		Resource::ResourceManager* pResourceManager = nullptr;		// テクスチャの実体を引く

		// アロケーターのメモリ容量
		size_t cbAllocatorMemSize = 32 * 1024 * 1024;

		// ボーン用行列数
		UINT boneElementNum = 0;	// ボーンパレットの要素数(重ねたシーンぶんを連結するので、1ワールド分では足りない)
	};

	// 毎フレーム詰め直す構造体バッファの容量(要素数)。計測用
	struct FrameBufferCapacity
	{
		size_t meshInstance = 0;
		size_t meshMaterial = 0;
		size_t meshLod = 0;
		size_t drawInstanceIndex = 0;
		size_t bone = 0;
		size_t ui = 0;
		size_t debugLine = 0;
	};
	


	// 現在のフレームの描画管理クラス
	class RenderContext
	{
	public:

		//--------------------------------------------------------------------------------------------
		// クラス基盤
		//--------------------------------------------------------------------------------------------
		RenderContext();
		~RenderContext();

		// 初期化・解放
		void Init(
			GraphicsEngine* a_pOwner,
			D3D12::GraphicsCommandList* a_pCmdList,
			const RenderContextDesc& a_desc
		);
		void Release();

		// フレームの初めに呼ぶ
		void Clear();

		// 現在のコマンドリストを取得
		D3D12::GraphicsCommandList* RefCurrentCmdList();

		void SetDirectCommandList(D3D12::GraphicsCommandList* a_pCmdList);

		//--------------------------------------------------------------------------------------------
		// バッファ関係
		//--------------------------------------------------------------------------------------------
		// 現在のフレームの定数バッファアロケーターにアクセス
		D3D12::CBAllocator* BindCB();

		// 計測用 : 定数バッファアロケーター(読むだけ)と、毎フレームの構造体バッファの容量
		const D3D12::CBAllocator* GetCB() const { return m_upCBAllocator.get(); }
		FrameBufferCapacity GetFrameBufferCapacity() const;

		// ---- 定数バッファをルートでバインド ----
		// グラフィック版
		template<typename T>
		void GraphicsBindRootCBV(
			int a_descIndex,
			const T& a_data
		);
		// コンピュート版
		template<typename T>
		void ComputeBindRootCBV(
			int a_descIndex,
			const T& a_data
		);

		// レンダーターゲットの切り替え
		void SetRenderTargets(
			const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& a_rtvHandleVec,
			const D3D12_CPU_DESCRIPTOR_HANDLE* a_pDsvHandle
		);

		//--------------------------------------------------------------------------------------------
		// ディスクリプタテーブルを、シェーダー可視ヒープ上のそのビュー自身の席へ向ける。
		// コピーはしない(ビューは作った時点でシェーダー可視ヒープへ写してある)。
		// レイトレーシングのように、ルートシグネチャがテーブルで受けるシェーダー用
		//--------------------------------------------------------------------------------------------
		void ComputeBindSRVBindLess(UINT a_rootIdx, Handle<D3D12::SRV> a_srvHandle);
		void BindUAVBindLess(UINT a_rootIdx, Handle<D3D12::UAV> a_handle);

		//--------------------------------------------------------------------------------------
		// ビューの置き場(借り物)
		//
		// 実体は GraphicsEngine が持っている。
		// フレームの描画中にハンドルからCPU/GPUハンドルを引きたいものは、
		// シングルトンではなくここから借りること
		//--------------------------------------------------------------------------------------
		D3D12::DescriptorHeapManager* RefDescriptorHeapManager() const { return m_pHeapManager; }
		D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandleBindLess(Handle<D3D12::SRV> a_handle) const;

		// レンダーターゲットのクリア
		void ClearRenderTarget(const Handle<Resource::Texture>& a_texHandle);

		// 色を渡さないと既定の不透明黒(0,0,0,1)で塗る。
		// 後段でアルファ合成される中間ターゲットは、必ず透明黒を渡すこと
		void ClearRenderTarget(
			const D3D12_CPU_DESCRIPTOR_HANDLE& a_rtvHandle,
			const Math::Color& a_clearColor = { 0.f, 0.f, 0.f, 1.f });

		// 深度値バッファのクリア
		void ClearDSV( const Handle<D3D12::DSV>& a_DSVHandle);
		void ClearDSV( const D3D12_CPU_DESCRIPTOR_HANDLE& a_DSVHandle);

		// UAVのテクスチャを塗りつぶす。
		//
		// ClearUnorderedAccessViewFloat は「シェーダー可視ヒープ上のGPUハンドル」と
		// 「非シェーダー可視のCPUハンドル」の両方を要求する。
		// どちらも同じ番号で両方のヒープに席があるので、ハンドルから引くだけでよい
		void ClearUAV(
			Handle<D3D12::UAV> a_uavHandle,
			ID3D12Resource* a_pResource,
			const float a_color[4]);

		//--------------------------------------------------------------------------------------------
		// バインドレス
		//
		// シェーダー可視の CBV/SRV/UAV ヒープ(DescriptorHeapManager が1本持つ)とサンプラーヒープを張る。
		// コピーはしない : シェーダーはグローバルな番号で ResourceDescriptorHeap[i] を引く
		//--------------------------------------------------------------------------------------------
		void BindBindlessHeaps();

		// ビューの番号を 32bit ルート定数として渡す(シェーダー側は cbuffer の uint で受ける)
		void GraphicsBindDescriptorIndices(UINT a_rootIdx, std::span<const UINT> a_indices);
		void ComputeBindDescriptorIndices(UINT a_rootIdx, std::span<const UINT> a_indices);

		void Dispatch(UINT a_x,UINT a_y,UINT a_z);
		void DispatchMesh(UINT a_x,UINT a_y,UINT a_z);

		//--------------------------------------------------------------------------------------------
		// 描画コマンド
		//--------------------------------------------------------------------------------------------


		// 描画命令の実行
		// a_boneMatVec : このフレームに描くワールドすべてのボーン行列を連結したもの。
		// シーンから直接引かず渡してもらう(重ねて描くときに描く側しか全体を知らないため)
		void UpdateBuffer(
			const std::vector<MeshInstanceData>& a_mesInstance,
			const std::vector<MeshMaterial>& a_mesMaterial,
			const std::vector<MeshLodData>& a_meshLod,
			const std::vector<Resource::BoneMatrix>& a_boneMatVec
		);
		void UpdateUIBuffer(const std::vector<UIData>& a_uiInstanceVec);

		// トレイルの頂点を上げる(DrawLists::GetTrailVertexVec)。上げ切れなければそのフレームは描かない
		void UpdateTrailBuffer(const std::vector<TrailVertex>& a_vertexVec);

		// 描く順のインスタンス番号の表を上げる(DrawLists::GetDrawInstanceIndexVec)。
		// 並びはソートで決まるので、DrawLists::SortItems() の後で呼ぶこと
		void UpdateDrawInstanceIndexBuffer(const std::vector<uint32_t>& a_indexVec);

		// バッファバインド
		void ComputeBindBonePaletteBuffer(UINT a_rootIndex);
		void BindGraphicsDebugLineBuffer(UINT a_rootIndex);

		// カメラ・メッシュシェーダー関連
		void BindCamera();
		void BindMeshInstance();
		void BindMeshlet();

		// UI関連
		//
		// SV_InstanceID は DrawIndexedInstanced の StartInstanceLocation を含まない
		// (必ず0から数え直す)ので、途中から描きたいときはバッファの先頭アドレス自体を
		// ずらして張り直す。a_startInstance はそのための「何番目から張るか」
		void BindUIBuffer(UINT a_rootIndex, UINT a_startInstance = 0);
		void DrawUI(UINT a_rootIndex);

		// 境界面 : 積まれた面を1枚ずつ、a_rootIndex の定数バッファへ詰めて描く。
		// 板ポリは VS が SV_VertexID で組む(頂点バッファは張らない)。トポロジは呼ぶ側が設定すること
		void DrawBoundary(UINT a_rootIndex);

		// トレイル : 積まれた帯を遠い順に1本ずつ描く(三角形ストリップ)。
		// a_vertexRootIndex に頂点の構造体バッファ、a_constRootIndex に1本ぶんのルート定数
		// (頂点の先頭・絵の番号・加算か)を張る。PSO とルートシグネチャは呼ぶ側が張ること
		void DrawTrails(UINT a_vertexRootIndex, UINT a_constRootIndex);

		// 自分のパス番号で積まれたモデルを描く。
		// 不透明は PSO・メッシュでまとめた並び、半透明は奥から手前の並び(別の配列)から引く
		void DrawQueueDispatchMesh(uint8_t a_passIndex);
		void DrawTransparentQueueDispatchMesh(uint8_t a_passIndex);

		//--------------------------------------------------------------------------------------------
		// 描画パス構築
		//--------------------------------------------------------------------------------------------

		// リソースのコピー
		void ResourceCopy(ID3D12Resource* a_pSrc,ID3D12Resource* a_pDst);

		// ルートシグネチャをセット
		void SetGraphicsRootSignature(ID3D12RootSignature* a_pRootSig);
		void SetComputeRootSignature(ID3D12RootSignature* a_pRootSig);

		// ハンドル版 : PipelineStateManager から実体を引いて張る。
		// 保持側はハンドルのまま持ち、張る直前にここで解決する
		void SetGraphicsRootSignature(const Handle<ID3D12RootSignature>& a_handle);
		void SetComputeRootSignature(const Handle<ID3D12RootSignature>& a_handle);

		// パイプラインステートをセット、前回と変更がない場合はスキップ
		// PSOの指定は実体かハンドルで行う。
		// 生の8bit添字を受ける入口は、PSOが256個を超えると黙って
		// 別のPSOを引くため置かない
		void SetGraphicPSO(ID3D12PipelineState* a_pPSO);
		void SetComputePSO(ID3D12PipelineState* a_pPSO);

		// ハンドルで張る版(PSOの通し番号が8bitに収まらなくなっても壊れない)
		void SetGraphicPSO(const Handle<ID3D12PipelineState>& a_handle);
		void SetComputePSO(const Handle<ID3D12PipelineState>& a_handle);

		// プリミティブトポロジーセット
		void SetPrimitive(D3D12_PRIMITIVE_TOPOLOGY a_pri);

		// パーティクルやUIなどの描画用
		// 板ポリを指定しない側はフラットな1枚板(4頂点)で描く
		void DrawPolygonInstancing(UINT a_count);
		void DrawPolygonInstancing(Resource::QuadPolygon* a_pPolygon, UINT a_count);

		//--------------------------------------------------------------------------------------------
		// 板ポリの間接描画
		//
		// インスタンス数などの描画引数を、CPU ではなく GPU 上のバッファから読んで描く。
		// GPU が数えた数(生きているパーティクルの数など)で描けるので、CPU へ読み戻さずに済む。
		//
		// a_pArgs は D3D12_DRAW_INDEXED_ARGUMENTS(uint ×5 = 20バイト)が a_argsOffset から並んだバッファ。
		// 描く時点で D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT にしておくこと(遷移は呼ぶ側の仕事)。
		// 引数のインデックス数は、張る板ポリの GetIndexCount() と合わせること
		//--------------------------------------------------------------------------------------------
		void DrawPolygonIndirect(ID3D12CommandSignature* a_pSignature, ID3D12Resource* a_pArgs, UINT64 a_argsOffset = 0);
		void DrawPolygonIndirect(Resource::QuadPolygon* a_pPolygon, ID3D12CommandSignature* a_pSignature, ID3D12Resource* a_pArgs, UINT64 a_argsOffset = 0);

		// 形状描画用
		void DrawShape();


		//リソースバリア設定
		void Transition(
			ID3D12Resource* a_pResource,
			D3D12_RESOURCE_STATES a_before,
			D3D12_RESOURCE_STATES a_after
		);

	private:

		// 板ポリの頂点・インデックスバッファを張る(直接描画と間接描画で共通)
		void BindPolygonBuffers(Resource::QuadPolygon* a_pPolygon);

		// 並んだ描画アイテムをメッシュシェーダーで描く(不透明・半透明で共通)。
		// a_firstIndex : a_itemVec の先頭が、描く順のインスタンス番号の表の何番目か
		void DispatchMeshItems(std::span<const LightWeightDrawItem> a_itemVec, UINT a_firstIndex);

	private:
		//--------------------------------------------------------------------------------------------
		// 参照
		//--------------------------------------------------------------------------------------------
		D3D12::Device* m_pDevice = nullptr;							// デバイス
		D3D12::DescriptorHeapManager* m_pHeapManager = nullptr;		// ビューの置き場(借り物)
		PipelineStateManager* m_pPipelineStateManager = nullptr;	// PSO・ルートシグネチャ(借り物)
		const DrawLists* m_pDrawLists = nullptr;					// 描画要求の配列(借り物)
		const BackBuffer* m_pBackBuffer = nullptr;					// バックバッファ(借り物)
		Resource::ResourceManager* m_pResourceManager = nullptr;	// テクスチャの実体を引く(借り物)
		GraphicsEngine* m_pGraphicsEngine = nullptr;				// グラフィックスエンジン

		//--------------------------------------------------------------------------------------------
		// フレーム限定リソース
		//--------------------------------------------------------------------------------------------
		std::unique_ptr<D3D12::CBAllocator> m_upCBAllocator = nullptr;	// 定数バッファアロケーター
		D3D12::GraphicsCommandList* m_pCmdList = nullptr;				// 現在フレームのグラフィックスコマンドリスト

		// ボーン用データ
		D3D12::DynamicStructuredBuffer<Resource::BoneMatrix> m_boneBuffer;

		// デバッグライン用頂点
		D3D12::StaticStructuredBuffer<DebugLineData> m_debugLineBuffer;

		// メッシュシェーダー用データ
		D3D12::StaticStructuredBuffer<MeshInstanceData>		m_meshInstanceBuffer;
		D3D12::StaticStructuredBuffer<MeshMaterial>			m_meshMaterialBuffer;
		D3D12::StaticStructuredBuffer<MeshLodData>			m_meshLodBuffer;			// LOD の表(MeshLodData)

		// 描く順のインスタンス番号の表(増幅シェーダーが「土台 + SV_GroupID.y」で引く)
		D3D12::DynamicStructuredBuffer<uint32_t>			m_drawInstanceIndexBuffer;
		UINT m_drawInstanceIndexCount = 0;	// このフレームに上げ切れた数。これより先のアイテムは描かない

		// UIデータ
		D3D12::DynamicStructuredBuffer<UIData> m_uiInstanceBuffer;

		// トレイルの頂点
		D3D12::DynamicStructuredBuffer<TrailVertex> m_trailVertexBuffer;
		bool m_isTrailUploaded = false;	// このフレームの頂点を上げ切れたか
	};
	template<typename T>
	inline void RenderContext::GraphicsBindRootCBV(int a_descIndex, const T& a_data)
	{
		m_upCBAllocator->BindAndAttachDataRootCBV(m_pCmdList, a_descIndex, a_data);
	}
	template<typename T>
	inline void RenderContext::ComputeBindRootCBV(int a_descIndex, const T & a_data)
	{
		m_upCBAllocator->BindAndAttachDataComputeRootCBV(m_pCmdList, a_descIndex, a_data);
	}
}