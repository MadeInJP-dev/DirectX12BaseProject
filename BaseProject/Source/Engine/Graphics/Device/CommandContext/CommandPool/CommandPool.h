#pragma once

namespace Engine::Graphics
{

	// 使用中のコマンドリストと完了フェンス値をペアで持つ
	struct InFlightList
	{
		ComPtr<D3D12::GraphicsCommandList> cpList;
		UINT64 fenceValue;						// この値を超えたら返却
	};

	/// <summary>
	/// コマンドキューを持ち、そのキューと対応するコマンドリストのプールを持つクラス
	/// CPUの処理が止まることはないが、一つのコマンドリストはGPUの処理が終わるまでは使えない
	/// </summary>
	class CommandPool
	{
	public:

		CommandPool() = default;
		~CommandPool() { Release(); }

		/// <summary>
		/// 初期化 : コマンドキューの作成
		/// </summary>
		/// <param name="a_pDevice"></param>
		/// <param name="a_type"></param>
		void Init(D3D12::Device* a_pDevice,D3D12_COMMAND_LIST_TYPE a_type);

		/// <summary>
		/// 解放処理
		/// </summary>
		void Release();

		/// <summary>
		/// このキューに積まれたものがすべて終わるまで待つ : CPUが止まる。
		/// 新しくシグナルを打ってから待つので、フレームのフェンスより後に積まれた
		/// Present なども含めて終わっている
		/// </summary>
		void WaitIdle();

		/// <summary>
		/// コマンドリストの取得 : 内部のリセット処理が走る
		/// </summary>
		/// <returns>コマンドリストの生ポインタ</returns>
		D3D12::GraphicsCommandList* AcquireList(D3D12::Device* a_pDevice, ID3D12CommandAllocator* a_pAllocator);

		/// <summary>
		/// 記録が終わったリストを実行待ちへためる
		/// </summary>
		/// <param name="a_pList">記録が終わったリスト</param>
		void SubmitList(D3D12::GraphicsCommandList* a_pList);

		/// <summary>
		/// 実行待ちリストを一括でキューへ投げて実行 : CPU待機時間が発生する
		/// </summary>
		UINT64 ExecutePendingLists();

		/// <summary>
		/// コマンドリストの即時実行 : GPU完了まで待つためCPUが止まる
		/// 初期化時などランタイム外でGPU操作が必要なときなどに使う
		/// </summary>
		void ExecuteImmediate(D3D12::GraphicsCommandList* a_pList);

		/// <summary>
		/// リストをキューへ流す直前に呼ぶ処理を登録する(nullptr で外す)。
		/// 予約リソースへの実メモリの割り当てのように、流すリストより先にキューへ乗せたい操作に使う。
		/// 流すのと同じロックの中で呼ぶので、割り当てと実行の間に他のスレッドのリストは割り込まない
		/// </summary>
		void SetBeforeExecuteHook(std::function<void(D3D12::CommandQueue*)> a_hook);

		// ---- アクセサ ----
		D3D12::CommandQueue* RefCommandQueue() { return m_cpCmdQueue.Get(); }
		D3D12_COMMAND_LIST_TYPE GetQueueType() const { return m_type; }
		D3D12::Fence* RefFence() { return m_cpFence.Get(); }
	private:
		
		// コマンドキューとフェンス
		ComPtr<D3D12::CommandQueue>	m_cpCmdQueue	= nullptr;	// コマンドキュー
		ComPtr<D3D12::Fence>			m_cpFence		= nullptr;	// 使用終了待ち用フェンス
		UINT64					m_fenceValue	= 0;		// 現在のフェンス値

		// キューの種類
		D3D12_COMMAND_LIST_TYPE m_type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		// コマンドリストプール
		std::vector<ComPtr<D3D12::GraphicsCommandList>>	m_freeLists		= {};	// 使えるリスト
		std::vector<InFlightList>					m_inFlightLists = {};	// GPU実行中のリスト

		// 生ポインタからComPtrの追跡
		std::unordered_map<D3D12::GraphicsCommandList*, ComPtr<D3D12::GraphicsCommandList>> m_trackingMap = {};

		// 非同期用
		std::mutex m_mutex;

		// 実行待ちのコマンドリスト生ポインタ配列
		std::vector<D3D12::GraphicsCommandList*> m_pendingLists;

		// リストを流す直前に呼ぶ処理(m_mutex で守る)
		std::function<void(D3D12::CommandQueue*)> m_beforeExecuteHook = nullptr;
	};
}