#include "Engine/Graphics/Device/CommandContext/CommandPool/CommandPool.h"

namespace Engine::Graphics
{
	void CommandPool::Init(D3D12::Device* a_pDevice, D3D12_COMMAND_LIST_TYPE a_type)
	{

		m_type = a_type;
		// コマンドキュー作成
		D3D12_COMMAND_QUEUE_DESC _queueDesc = {};
		_queueDesc.Type = a_type;
		_queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		_queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		_queueDesc.NodeMask = 0;

		// 作成
		HRESULT _hr = a_pDevice->CreateCommandQueue(
			&_queueDesc,
			IID_PPV_ARGS(m_cpCmdQueue.ReleaseAndGetAddressOf())
		);
		ENGINE_ERRLOG(SUCCEEDED(_hr), "CommandQueue の生成に失敗 HRESULT:%08X", _hr);

		// フェンス作成
		_hr = a_pDevice->CreateFence(
			0,
			D3D12_FENCE_FLAG_NONE,
			IID_PPV_ARGS(m_cpFence.ReleaseAndGetAddressOf())
		);
		ENGINE_ERRLOG(SUCCEEDED(_hr), "Fence の生成に失敗 HRESULT:%08X", _hr);

		m_fenceValue = 0;
	}

	void CommandPool::Release()
	{
		// GPU完了を待って解放
		WaitIdle();

		// 各配列のクリア
		m_freeLists.clear();
		m_inFlightLists.clear();

		// ComPtrの解放
		m_cpFence.Reset();
		m_cpCmdQueue.Reset();
	}

	void CommandPool::WaitIdle()
	{
		// キューとフェンスの存在チェック(解放済みなら待つものが無い)
		if (!m_cpCmdQueue || !m_cpFence) return;

		// 新しい値でシグナルを打つ。
		// 値を進めるのは ExecutePendingLists と同じ数え上げなので同じロックで守る
		UINT64 _waitValue = 0;
		{
			std::lock_guard<std::mutex> _lock(m_mutex);
			++m_fenceValue;
			_waitValue = m_fenceValue;
			m_cpCmdQueue->Signal(m_cpFence.Get(), _waitValue);
		}

		// まだ届いていなければ待つ
		if (m_cpFence->GetCompletedValue() < _waitValue)
		{
			HANDLE _event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			m_cpFence->SetEventOnCompletion(_waitValue, _event);
			WaitForSingleObject(_event, INFINITE);
			CloseHandle(_event);
		}
	}

	D3D12::GraphicsCommandList* CommandPool::AcquireList(D3D12::Device* a_pDevice, ID3D12CommandAllocator* a_pAllocator)
	{
		// ここは m_inFlightLists / m_freeLists / m_trackingMap をすべて書き換える。
		// ワーカースレッドからリソースをビルドすると同時に呼ばれるため、
		// SubmitList や ExecutePendingLists と同じロックで守る
		std::lock_guard<std::mutex> _lock(m_mutex);

		// 完了済みのものをフリーに戻す
		UINT64 _completed = m_cpFence->GetCompletedValue();
		for (auto it = m_inFlightLists.begin(); it != m_inFlightLists.end();)
		{
			// フェンス値が目標値を超えていたら
			if (it->fenceValue <= _completed)
			{
				// 使用済みになったものをフリーリストに返す
				m_freeLists.push_back(std::move(it->cpList));
				it = m_inFlightLists.erase(it);
			}
			// そうでなければ処理中なので次のリストを見に行く
			else
			{ 
				++it;
			}
		}

		// フリーがあれば返す、なければ新規作成
		if (!m_freeLists.empty())
		{
			// フリーリストから取得
			ComPtr<D3D12::GraphicsCommandList> _cpList = std::move(m_freeLists.back());
			m_freeLists.pop_back();			// 取得したのを配列から消す

			D3D12::GraphicsCommandList* _pRaw = _cpList.Get();

			// フリーリストの再利用時は、渡されたアロケーターでリセットする
			auto _hr = _pRaw->Reset(a_pAllocator, nullptr);
			ENGINE_ERRLOG(SUCCEEDED(_hr),"CommandListのリセットに失敗");

			// 生ポインタと ComPtrを紐づける
			m_trackingMap[_pRaw] = std::move(_cpList);
			return _pRaw;
		}
		

		// フリーがなければ新規作成
		ComPtr<D3D12::GraphicsCommandList> _cpNewList;
		HRESULT _hr = a_pDevice->CreateCommandList(
			0,
			m_type,
			a_pAllocator,
			nullptr,
			IID_PPV_ARGS(_cpNewList.ReleaseAndGetAddressOf())
		);
		ENGINE_ERRLOG(SUCCEEDED(_hr), "CommandList の生成に失敗 HRESULT:%08X", _hr);

		// 作成後にマップに登録して返す
		D3D12::GraphicsCommandList* _pRaw = _cpNewList.Get();
		m_trackingMap[_pRaw] = std::move(_cpNewList);
		return _pRaw;
	}

	void CommandPool::SubmitList(D3D12::GraphicsCommandList* a_pList)
	{
		// 待機状態にする
		a_pList->Close();

		std::lock_guard<std::mutex> _lock(m_mutex);
		m_pendingLists.push_back(a_pList);
	}

	UINT64 CommandPool::ExecutePendingLists()
	{
		std::lock_guard<std::mutex> _lock(m_mutex);

		// 実行待ちがなければリターン
		if (m_pendingLists.empty()) return m_fenceValue;

		// APIに渡すための配列を作成
		std::vector<ID3D12CommandList*> _executeLists;
		_executeLists.reserve(m_pendingLists.size());
		for (D3D12::GraphicsCommandList* _pList : m_pendingLists)
		{
			_executeLists.push_back(_pList);
		}

		// 流す前に積むもの(実メモリの割り当てなど)を先にキューへ乗せる
		if (m_beforeExecuteHook) m_beforeExecuteHook(m_cpCmdQueue.Get());

		// 一括実行
		m_cpCmdQueue->ExecuteCommandLists(static_cast<UINT>(_executeLists.size()), _executeLists.data());

		// フェンスを進める
		++m_fenceValue;
		m_cpCmdQueue->Signal(m_cpFence.Get(), m_fenceValue);

		// InFlight (実行中) リストへ移動
		for (D3D12::GraphicsCommandList* _pList : m_pendingLists)
		{
			auto _it = m_trackingMap.find(_pList);
			if (_it != m_trackingMap.end())
			{
				m_inFlightLists.push_back({ std::move(_it->second), m_fenceValue });
				m_trackingMap.erase(_it);
			}
		}

		// 実行待ちリストをクリア
		m_pendingLists.clear();

		return m_fenceValue;
	}

	void CommandPool::SetBeforeExecuteHook(std::function<void(D3D12::CommandQueue*)> a_hook)
	{
		std::lock_guard<std::mutex> _lock(m_mutex);
		m_beforeExecuteHook = std::move(a_hook);
	}

	void CommandPool::ExecuteImmediate(D3D12::GraphicsCommandList* a_pList)
	{
		// コマンドリストを閉じる
		a_pList->Close();

		//------------------------------------------------------------------------------------------
		// 実行とシグナルはロックの中で行う
		//
		// フェンス値の数え上げは ExecutePendingLists / WaitIdle と共有している。
		// ワーカースレッドからのビルドと同時に走ると、ロックの外で ++ した値が
		// 他のスレッドの値と重複し、まだ終わっていない作業を「完了」と見なして
		// 実行中のコマンドリストを使い回すことになる。
		// 待つ値もロックの中で控えておく(後で m_fenceValue を読むと他人の値になりうる)
		//------------------------------------------------------------------------------------------
		UINT64 _waitValue = 0;
		{
			std::lock_guard<std::mutex> _lock(m_mutex);

			// 流す前に積むもの(実メモリの割り当てなど)を先にキューへ乗せる
			if (m_beforeExecuteHook) m_beforeExecuteHook(m_cpCmdQueue.Get());

			ID3D12CommandList* _lists[] = { a_pList };
			m_cpCmdQueue->ExecuteCommandLists(1, _lists);

			++m_fenceValue;
			_waitValue = m_fenceValue;
			m_cpCmdQueue->Signal(m_cpFence.Get(), _waitValue);
		}

		// 完了まで待つ : 待つ間はロックを握らない(他のスレッドの積み込みを止めない)
		if (m_cpFence->GetCompletedValue() < _waitValue)
		{
			HANDLE _event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			m_cpFence->SetEventOnCompletion(_waitValue, _event);
			WaitForSingleObject(_event, INFINITE);
			CloseHandle(_event);
		}

		// プールに返却
		std::lock_guard<std::mutex> _lock(m_mutex);
		auto _it = m_trackingMap.find(a_pList);
		if (_it == m_trackingMap.end())
		{
			// Shipping では ENGINE_ERRLOG が消えるので、end() を触らないよう必ず抜ける
			ENGINE_ERRLOG(false, "ExecuteImmediate : 未知のコマンドリストが渡されました");
			return;
		}

		m_freeLists.push_back(std::move(_it->second));
		m_trackingMap.erase(_it);
	}
}
