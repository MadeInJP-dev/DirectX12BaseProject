#pragma once
#include "Engine/Graphics/LightManager/LightManager.h"
#include "Engine/Graphics/Frame/DrawList/DrawList.h"

// 仕事ごとに分けた持ち物。呼び出し側は GraphicsEngine の Ref～() から引いて使うので、
// このヘッダー1つで引いた先まで使えるようにしておく
#include "Engine/Graphics/Device/RenderDevice.h"
#include "Engine/Graphics/Frame/SceneView/SceneView.h"
#include "Engine/Graphics/Frame/DrawSubmitter/DrawSubmitter.h"
#include "Engine/Graphics/RenderingPipeline/CameraPipelineManager/CameraPipelineManager.h"
#include "Engine/Graphics/Frame/SceneView/GroundImpulse.h"

namespace Engine
{
	namespace Graphics::D3D12
	{
		class DescriptorHeapManager;
	}

	namespace Graphics::Raytracing
	{
		class RayEngine;
		class BLASCompactor;
	}

	namespace Graphics::Particle
	{
		class ParticleBufferManager;
	}

	namespace Resource
	{
		class ResourceManager;
		class QuadPolygon;
	}
}

namespace Engine::Graphics
{
	// 前方宣言
	class RenderContext;
	class SkinningCompute;
	class ParticleSimulation;
	class MeshBufferAllocator;
	class DebugDraw;
	class BackBuffer;
	class PipelineStateManager;
	class GraphicsProfiler;

	namespace Pipeline
	{
		class PassMetaRegistry;
	}

	// グラフィックスエンジンの初期化に必要な情報
	struct GraphicsEngineDesc
	{
		UINT width = 0;						// ウィンドウの横幅
		UINT height = 0;					// ウィンドウの縦幅

		// リソースの持ち主(借り物)。
		// 描画アイテムの組み立てでメッシュやマテリアルを引くので、Init() で受け取って持っておく。
		// シェーダーのパスからGUIDを引くときは、この中のアセットデータベースを使う
		Resource::ResourceManager* pResourceManager = nullptr;
	};

	//==========================================================================================
	// グラフィックスエンジン
	//
	// 描画まわりの持ち主。自分で持つ仕事は「何をどの順で作り・回し・捨てるか」だけで、
	// 中身は仕事ごとのクラスに任せる。
	//
	//   RenderDevice          … デバイス・コマンドキュー・フレーム同期・非同期転送
	//   SceneView             … カメラ定数・TAAのジッター・カメラ発の画面効果・空・環境光
	//   DrawSubmitter         … モデル・UI・スキニングの描画要求を受け取って積む
	//   CameraPipelineManager … カメラごとの描画構成(組み直し・実行・画面への出力)
	//
	// 初期化と解放は段に分かれていて、順番は次のとおり。
	//
	//   InitDevice → InitDescriptorHeap → CreateBackBuffer → Init
	//   Release → (GPU待ち・遅延解放) → ReleaseBackBuffer → ReleaseDescriptorHeap → ReleaseDevice
	//
	// デバイス(とコマンドキュー・フレーム同期)は何よりも先に作って最後に捨てる。
	// ディスクリプタヒープはその内側で、ビューを預けているもの
	// (パーティクル・レイトレ・バックバッファ・遅延解放キュー)が片付くまで生かしておく。
	//
	// 1フレームの流れ
	//
	//   BeginFrame → (アプリが描画要求を積む) → Execute → EndFrame → (エディターの描画) → Present
	//==========================================================================================
	class GraphicsEngine
	{
	public:

		GraphicsEngine();
		~GraphicsEngine();

		//--------------------------------------------------------------------------------------------
		// デバイスの初期化・解放
		//
		// デバイスと、その上に乗るコマンドキュー・フレーム同期・非同期転送をまとめて用意する
		// (実体は RenderDevice)。他のD3Dオブジェクトはすべてデバイスの子なので、
		// 作るのは最初・捨てるのは最後
		//--------------------------------------------------------------------------------------------
		bool InitDevice(bool a_isDebug);
		void ReleaseDevice();

		//--------------------------------------------------------------------------------------------
		// ディスクリプタヒープの初期化・解放
		//
		// 他の初期化とは別段にしてある。
		// ・作るのが一番早い : バックバッファのRTVを取るのに要るので Init より前に通す
		// ・捨てるのが一番遅い : バックバッファ/遅延解放キューが
		//   Release() の後にディスクリプタを返してくるため、そこまで生かしておく
		//--------------------------------------------------------------------------------------------
		bool InitDescriptorHeap();
		void ReleaseDescriptorHeap();

		//--------------------------------------------------------------------------------------------
		// バックバッファの作成・解放
		//
		// スワップチェインは描画キューに紐づくので、InitDevice の後に作る。
		// RTVをディスクリプタヒープに預けているので、ヒープより先に捨てる
		//--------------------------------------------------------------------------------------------
		void CreateBackBuffer(HWND a_hWnd, UINT a_width, UINT a_height);
		void ReleaseBackBuffer();

		// 初期化・解放
		// パーティクル・レイトレワールドもここで作り、ここで捨てる
		void Init(D3D12::GraphicsCommandList* a_pCmdList, const GraphicsEngineDesc& a_desc);
		void Release();

		//--------------------------------------------------------------------------------------------
		// フレーム
		//--------------------------------------------------------------------------------------------
		// フレームの開始。このフレームのアロケーターが空くまで
		// (= 同じ番号を前回使ったフレームのGPU作業が終わるまで)待ってから始める
		void BeginFrame();

		// 積まれた描画要求を流す : カメラの確定・バッファ更新・ソート・計算・各カメラの描画・画面への写し
		void Execute();

		// フレームの終わり : 積まれなかったカメラと、今フレームだけの値を捨てる
		void EndFrame();

		// 画面へ出す : バックバッファを表示できる状態へ落として、フレームを閉じて切り替える。
		// エディターの描画もバックバッファへ載せるので、それが済んだ後に呼ぶ
		void Present(bool a_isVsync);

		//--------------------------------------------------------------------------------------------
		// 仕事ごとの持ち物
		//--------------------------------------------------------------------------------------------
		// デバイス・コマンドキュー・フレーム同期・非同期転送
		RenderDevice* RefRenderDevice() { return m_upRenderDevice.get(); }
		const RenderDevice* GetRenderDevice() const { return m_upRenderDevice.get(); }

		// カメラ定数・画面効果・空・環境光。パスは GetSceneView() から読む
		SceneView* RefSceneView() { return m_upSceneView.get(); }
		const SceneView* GetSceneView() const { return m_upSceneView.get(); }

		// 描画要求(モデル・UI・スキニング)の受け口
		DrawSubmitter* RefDrawSubmitter() { return m_upDrawSubmitter.get(); }

		// カメラごとの描画構成
		CameraPipelineManager* RefCameraPipelines() { return m_upCameraPipelines.get(); }
		const CameraPipelineManager* GetCameraPipelines() const { return m_upCameraPipelines.get(); }

		//--------------------------------------------------------------------------------------------
		// プレイヤーの画面設定
		//
		// 作り手の値(パイプラインの各パスの設定)へ重ねるもの。保存はアプリ側(ユーザーデータ)で、
		// 起動時と設定画面から流し込まれる。パスは定数を送るときにこれを掛ける
		//--------------------------------------------------------------------------------------------
		void SetUserExposureScale(float a_scale) { m_userExposureScale = a_scale; }	// 明るさ : 露出へ掛ける倍率
		float GetUserExposureScale() const { return m_userExposureScale; }
		void SetUserBloomEnable(bool a_isEnable) { m_isUserBloomEnable = a_isEnable; }	// ブルーム : 切ると作り手が入れていても掛けない
		bool IsUserBloomEnable() const { return m_isUserBloomEnable; }

		//--------------------------------------------------------------------------------------------
		// 描画に使う共有物
		//--------------------------------------------------------------------------------------------
		// 今フレームのレンダーコンテキスト
		const Graphics::RenderContext* GetRenderContext() const;
		Graphics::RenderContext* RefRenderContext();

		// PSOやルートシグネチャの管理
		PipelineStateManager* RefPipelineStateManager();
		const PipelineStateManager* GetPipelineStateManager() const { return m_upPipelineStateManager.get(); }

		// バックバッファ
		BackBuffer* RefBackBuffer() { return m_upBackBuffer.get(); }
		const BackBuffer* GetBackBuffer() const { return m_upBackBuffer.get(); }

		// 描画要求の配列。積むのは DrawSubmitter、読むのは Execute の中
		DrawLists* RefDrawLists() { return &m_drawLists; }
		const DrawLists* GetDrawLists() const { return &m_drawLists; }

		// ディスクリプタヒープ。
		// これを直接引くのはコンテキストを組み立てる側だけにして、
		// 使う側はコンテキスト経由で受け取ること
		D3D12::DescriptorHeapManager* RefDescriptorHeapManager();
		const D3D12::DescriptorHeapManager* GetDescriptorHeapManager() const;

		//--------------------------------------------------------------------------------------------
		// 生成できるパスの一覧
		//
		// パイプラインアセットは「パスの型ID」しか保存しないので、
		// 読み込むときに実体を作り直すためこの一覧が要る。
		// ResourceBuildContext 経由でローダーへ渡される(ScopedResourceBuild が詰める)
		//--------------------------------------------------------------------------------------------
		Pipeline::PassMetaRegistry* RefPassMetaRegistry();

		// リソースの持ち主(借り物) : 持ち主は MainEngine
		Resource::ResourceManager* RefResourceManager() const { return m_pResourceManager; }

		// 描画解像度(バックバッファと同じ大きさ)。
		// エンジンはオプションを直接引かず、Init で受け取ったこの値を使う
		UINT GetRenderWidth() const { return m_renderWidth; }
		UINT GetRenderHeight() const { return m_renderHeight; }

		// ライト
		//
		// ライトの実体はここのプールに置き、持ち主(シーンのオブジェクトなど)はハンドルだけ持つ。
		// GetFrameLightData() が返すのは今フレームぶんの GPU バッファで、
		// Execute() の中で詰め直されるのでレンダーパスからのみ引くこと。
		LightManager* RefLightManager();
		const FrameLightData& GetFrameLightData() const;

		// グラウンドフィールドの衝撃
		//
		// SceneView に積まれた衝撃を Execute() で今フレームぶんの構造体バッファへ詰め直したもの。
		// ライトと同じく、レンダーパスからのみ引くこと
		const D3D12::DynamicStructuredBuffer<GroundImpulse>& GetGroundImpulseBuffer() const;
		uint32_t GetGroundImpulseCount() const { return m_groundImpulseCount; }

		// パスの描画実行
		void BindPSO(Graphics::RenderContext* a_pCtx, uint8_t a_psoIndex);
		void BindPSO(Graphics::RenderContext* a_pCtx, const Handle<ID3D12PipelineState>& a_handle);

		// バッファ取得
		MeshBufferAllocator* RefMeshBufferAllocator() { return m_upMeshBufferAllocator.get(); }
		const MeshBufferAllocator* GetMeshBufferAllocator() const { return m_upMeshBufferAllocator.get(); }

		//--------------------------------------------------------------------------------------------
		// 描画用の板ポリ
		//
		// UIもパーティクルも同じ板ポリを使い回すので、フレームごとのレンダーコンテキストではなく
		// エンジンが1つずつ持つ(以前はコンテキストの数だけ同じ頂点バッファを作っていた)。
		//
		//   フラット … 4頂点の1枚板。曲げないものはすべてこれ
		//   湾曲用   … 横に kCurveDivision 分割した板。頂点が無いと曲げようがないので、
		//               UIの湾曲(UIData::IsCurved)が有効なものだけこちらで描く
		//--------------------------------------------------------------------------------------------

		// UIの湾曲用板ポリの横分割数
		static constexpr uint32_t CURVE_DIVISION = 32;

		Resource::QuadPolygon* RefQuadPolygon()			{ return m_upQuadPolygon.get(); }
		Resource::QuadPolygon* RefCurvedQuadPolygon()	{ return m_upCurvedQuadPolygon.get(); }

		//--------------------------------------------------------------------------------------------
		// 間接描画のコマンドシグネチャ
		//
		// 「GPU 上の引数バッファを、どういう描画命令として読むか」の型。
		// DrawIndexed 1件(D3D12_DRAW_INDEXED_ARGUMENTS = 20バイト)だけを読み、ルート引数は変えない。
		// パーティクルの生存数ぶんだけ描く(ExecuteIndirect)のに使う。
		// 型だけのものなので、使う側の数に関係なくエンジンが1つ持って配る
		//--------------------------------------------------------------------------------------------
		ID3D12CommandSignature* GetDrawIndexedSignature() const { return m_cpDrawIndexedSignature.Get(); }

		//--------------------------------------------------------------------------------------------
		// デバッグ用ワイヤー
		//
		// 積む場所はエンジン側(DebugDraw)。エディターは表示のオンオフを持つだけで、
		// エンジンやアプリからエディターを名指しすることはない。
		// 中身は EndFrame で捨てられるので、積んだフレームのうちに描かれる
		//--------------------------------------------------------------------------------------------
		DebugDraw* RefDebugDraw() { return m_upDebugDraw.get(); }
		const DebugDraw* GetDebugDraw() const { return m_upDebugDraw.get(); }

		// パーティクル
		Particle::ParticleBufferManager* RefParticleManager() { return m_upParticleManager.get(); }
		const Particle::ParticleBufferManager* GetParticleManager() const { return m_upParticleManager.get(); }

		// レイトレワールド
		Raytracing::RayEngine* RefRayEngine() { return m_upRayEngine.get(); }

		// 静的 BLAS の圧縮 : メッシュの読み込みで BLAS を作るときに頼む先
		Raytracing::BLASCompactor* RefBLASCompactor() { return m_upBLASCompactor.get(); }
		const Raytracing::BLASCompactor* GetBLASCompactor() const { return m_upBLASCompactor.get(); }

		//--------------------------------------------------------------------------------------------
		// 描画まわりのプロファイラ
		//
		// メガバッファ・ディスクリプタヒープなどの使われ方を取る。
		// 見る側(エディター)が RequestCapture で頼んだフレームだけ、Execute の終わりで取る
		//--------------------------------------------------------------------------------------------
		GraphicsProfiler* RefProfiler() { return m_upProfiler.get(); }
		const GraphicsProfiler* GetProfiler() const { return m_upProfiler.get(); }

	private:
		//--------------------------------------------------------------------------------------------
		// 仕事ごとの持ち物
		//--------------------------------------------------------------------------------------------
		// デバイス・コマンドキュー・フレーム同期・非同期転送。アプリに1つだけ存在する
		std::unique_ptr<RenderDevice> m_upRenderDevice = nullptr;

		// カメラ定数・画面効果・空・環境光
		std::unique_ptr<SceneView> m_upSceneView = nullptr;

		// カメラごとの描画構成
		std::unique_ptr<CameraPipelineManager> m_upCameraPipelines = nullptr;

		// 描画要求の受け口
		std::unique_ptr<DrawSubmitter> m_upDrawSubmitter = nullptr;

		//--------------------------------------------------------------------------------------------
		// 共有物
		//--------------------------------------------------------------------------------------------
		// ディスクリプタヒープ。アプリに1つだけ存在する
		std::unique_ptr<D3D12::DescriptorHeapManager> m_upDescriptorHeapManager = nullptr;

		// バックバッファ(スワップチェイン)。RTVをヒープに預けている
		std::unique_ptr<BackBuffer> m_upBackBuffer = nullptr;

		// PSOやルートシグネチャの管理
		std::unique_ptr<PipelineStateManager> m_upPipelineStateManager = nullptr;

		// リソースの持ち主(借り物) : Init() で受け取る
		Resource::ResourceManager* m_pResourceManager = nullptr;

		// 描画解像度(Init で受け取る。バックバッファと同じ大きさ)
		UINT m_renderWidth = 0;
		UINT m_renderHeight = 0;

		// レンダーコンテキスト : 一フレーム内の描画情報を扱う
		std::vector<std::unique_ptr<RenderContext>> m_upRenderContextVec = {};
		UINT m_currentFrameIndex = 0;

		//メッシュバッファ管理
		std::unique_ptr<MeshBufferAllocator> m_upMeshBufferAllocator = nullptr;

		// 生成できるパスの型情報。インスタンスは持たない。
		// パイプラインアセットのロード時に、型IDからパスを作り直すのに使う
		std::unique_ptr<Pipeline::PassMetaRegistry> m_upPassMetaRegistry = nullptr;

		// 描画用の板ポリ(UI・パーティクル共用)。
		// フラットは4頂点の1枚板、湾曲用は横に kCurveDivision 分割したもの
		std::unique_ptr<Resource::QuadPolygon> m_upQuadPolygon = nullptr;
		std::unique_ptr<Resource::QuadPolygon> m_upCurvedQuadPolygon = nullptr;

		// 間接描画のコマンドシグネチャ(DrawIndexed 1件)。
		// 実行中のコマンドリストから参照されるので、GPUの完了を待った後(ReleaseDevice)で捨てる
		ComPtr<ID3D12CommandSignature> m_cpDrawIndexedSignature = nullptr;

		// デバッグ用ワイヤーの置き場。
		// 積む側(システム・GameObject・エンジン内部)はここへ入れ、
		// DebugLinePass が RenderContext 経由で読む
		std::unique_ptr<DebugDraw> m_upDebugDraw = nullptr;

		// パーティクルのGPUバッファ。ディスクリプタヒープにハンドルを持つ
		std::unique_ptr<Particle::ParticleBufferManager> m_upParticleManager = nullptr;

		// カメラに依存しない、フレームに1回のGPU処理(スキニング / パーティクルの発生と更新)
		std::unique_ptr<SkinningCompute> m_upSkinningCompute = nullptr;
		std::unique_ptr<ParticleSimulation> m_upParticleSimulation = nullptr;

		// レイトレワールド(TLAS/BLAS・各種バッファ)
		std::unique_ptr<Raytracing::RayEngine> m_upRayEngine = nullptr;

		// 静的 BLAS の圧縮(ビルドの後で小さく作り直す)
		std::unique_ptr<Raytracing::BLASCompactor> m_upBLASCompactor = nullptr;

		// 描画まわりのプロファイラ(見るだけ。頼まれたフレームだけ取る)
		std::unique_ptr<GraphicsProfiler> m_upProfiler = nullptr;

		// ライト本体のプール
		LightManager m_lightManager = {};

		// GPUへ渡すライト配列。
		// UPLOADヒープへ直接書き込むので、GPUがまだ前フレームを読んでいる領域を
		// 上書きしないようフレームぶん持つ(レンダーコンテキストと同じ数)
		FrameLightData m_frameLightDataArr[CPU_FRAME_COUNT] = {};

		// GPUへ渡すグラウンドフィールドの衝撃。
		// ライトと同じ理由(UPLOADヒープへ直接書く)でフレームぶん持つ
		D3D12::DynamicStructuredBuffer<GroundImpulse> m_groundImpulseBufferArr[CPU_FRAME_COUNT] = {};
		uint32_t m_groundImpulseCount = 0;	// 今フレームで書き込んだ数

		//--------------------------------------------------------------------------------------------
		// 描画要求の配列(描画アイテム・メッシュシェーダー用データ・UI・スキニング・ボーンパレット)
		//
		// CPU側のデータでそのフレームのうちに使い切るので1つだけ持つ。
		// GPUへ上げたコピーはレンダーコンテキストがフレームぶん持っている
		//--------------------------------------------------------------------------------------------
		DrawLists m_drawLists = {};

		// プレイヤーの画面設定(作り手の値へ重ねる)
		float m_userExposureScale = 1.0f;
		bool m_isUserBloomEnable = true;
	};
}
