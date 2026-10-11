#pragma once

namespace Engine::Graphics
{
	class GraphicsDevice;
	class CommandContext;
	class AsyncGPUManager;
	class FrameManager;
	struct AsyncBuildBatch;

	//==========================================================================================
	// GPUの土台
	//
	// デバイスと、その上に乗るコマンドキュー(描画・コピー・コンピュート)・フレーム同期・
	// 非同期転送をまとめて持つ。他のD3Dオブジェクトはすべてデバイスの子なので、
	// 作るのは最初・捨てるのは最後(GraphicsEngine::InitDevice / ReleaseDevice から呼ばれる)。
	//
	// 描画の中身(何を描くか)は一切知らない。コマンドリストの貸し出しと、
	// 「いつGPUが終わったか」を答えるのが仕事
	//==========================================================================================
	class RenderDevice
	{
	public:

		RenderDevice();
		~RenderDevice();
		NON_COPYABLE_NON_MOVABLE(RenderDevice);

		//--------------------------------------------------------------------------------------------
		// 初期化・解放
		//
		// 解放ではキューの完了を待ってから片付け、最後にデバイスを捨てる。
		// 残っているオブジェクトはここでリークとして報告される
		//--------------------------------------------------------------------------------------------
		bool Init(bool a_isDebug);
		void Release();

		// デバイス
		GraphicsDevice* RefGraphicsDevice() { return m_upGraphicsDevice.get(); }
		const GraphicsDevice* GetGraphicsDevice() const { return m_upGraphicsDevice.get(); }
		D3D12::Device* RefDevice();

		//--------------------------------------------------------------------------------------------
		// フレーム
		//--------------------------------------------------------------------------------------------
		// フレーム番号を進め、その番号を前回使ったフレームのGPU作業が終わるまで待つ。
		// 抜けた後は、このフレームのアロケーターとフレームごとのバッファを書き換えてよい
		void BeginFrame();

		// 積まれた描画キューのリストを流して、フレーム終了のシグナルを打つ(Present の直前)
		void EndFrame();

		// 今のCPUフレーム番号(0 ～ CPU_FRAME_COUNT-1)
		UINT GetCurrentFrameIndex() const;

		// すべてのフレームのGPU作業が終わるのを待つ。
		// 待つのはフレーム終了のシグナルまでで、その後に積まれる Present は含まない
		void WaitForFrame();

		// 全キュー(描画・コピー・コンピュート)を空にする : 終了処理用。
		// 新しくシグナルを打ってから待つので、最後の Present も終わっている。
		// スワップチェインやバックバッファを捨てる前はこちらを通すこと
		// (WaitForFrame では Present が走っている最中に捨てることになり、デバッグレイヤーが CORRUPTION で止まる)
		void WaitForGPUIdle();

		// フレームのフェンス値
		UINT64 GetCurrentFenceValue() const;	// 最後にシグナルを送った値
		UINT64 GetCompletedFenceValue() const;	// GPUが実際に完了させた値
		UINT64 GetNextFenceValue() const;		// 記録中フレームの終わりにシグナルされる値(遅延解放のタグに使う)

		// フレームのフェンスの持ち主(領域の遅延解放でフェンス値を引く側へ渡す)
		const FrameManager* GetFrameManager() const { return m_upFrameManager.get(); }

		//--------------------------------------------------------------------------------------------
		// コマンドキュー
		//--------------------------------------------------------------------------------------------
		// 描画キュー用のコマンドリストを取る。記録したら SubmitDirectCommandList で返すこと
		D3D12::GraphicsCommandList* AcquireDirectCommandList();
		void SubmitDirectCommandList(D3D12::GraphicsCommandList* a_pCmdList);

		// 初期化時など、フレームの外でGPU操作が必要なときに使う : 閉じて実行し、完了まで待つ
		void ExecuteImmediate(D3D12::GraphicsCommandList* a_pCmdList);

		// 描画キュー(スワップチェインや ImGui のバックエンドなど、外のライブラリへ渡すとき用)
		D3D12::CommandQueue* RefDirectCommandQueue();

		// どのキューでも、リストを流す直前に呼ぶ処理を登録する(nullptr で外す)。
		// 引数はキューの種類とキュー。予約リソースへの実メモリの割り当てを、使うリストより先に流すのに使う
		void SetBeforeExecuteHook(std::function<void(D3D12_COMMAND_LIST_TYPE, D3D12::CommandQueue*)> a_hook);

		//--------------------------------------------------------------------------------------------
		// 非同期転送(バックグラウンドロードなど)
		//--------------------------------------------------------------------------------------------
		/// <summary>
		/// 非同期コピーを実行する
		/// </summary>
		/// <param name="a_recordCmds">コマンドリストに積む処理（Upload->Defaultへの転送など）</param>
		/// <param name="a_onComplete">GPU転送完了時に裏で呼ばれるコールバック（Uploadヒープの解放など）</param>
		void ExecuteAsyncCopy(
			std::function<void(D3D12::GraphicsCommandList*)> a_recordCmds,
			std::function<void()> a_onComplete
		);

		/// <summary>
		/// まとまった単位でGPU転送を行うためのバッチを開く
		/// </summary>
		/// <param name="a_useCopy">アップロード転送用のコピーリストを確保するか</param>
		/// <param name="a_useCompute">BLAS構築用のコンピュートリストを確保するか</param>
		AsyncBuildBatch BeginAsyncBuildBatch(bool a_useCopy = true, bool a_useCompute = true);

		/// <summary>
		/// バッチを閉じて実行する
		/// コピー実行 → シグナル → コンピュートキューで待機 → コンピュート実行 の順に流す。
		/// BLASはコピーで転送したメガバッファを直接読むため、この順序でないと未転送のデータを掴む。
		/// </summary>
		/// <param name="a_batch">閉じるバッチ : 呼び出し後は空になる</param>
		/// <param name="a_onComplete">GPU処理完了時に裏で呼ばれるコールバック</param>
		void EndAsyncBuildBatch(AsyncBuildBatch& a_batch, std::function<void()> a_onComplete = nullptr);

	private:

		// デバイス(とファクトリ)。アプリに1つだけ存在する
		std::unique_ptr<GraphicsDevice> m_upGraphicsDevice = nullptr;

		// コマンドキュー(描画・コピー・コンピュート)と、それぞれのコマンドリストのプール
		std::unique_ptr<CommandContext> m_upCommandContext = nullptr;

		// 非同期転送の完了監視とアロケーターの使い回し
		std::unique_ptr<AsyncGPUManager> m_upAsyncGPUManager = nullptr;

		// フレーム番号・フェンス・フレームごとのアロケーター
		std::unique_ptr<FrameManager> m_upFrameManager = nullptr;
	};
}
