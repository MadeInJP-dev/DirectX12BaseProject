#include "Engine/Graphics/Device/RenderDevice.h"

#include "Engine/Graphics/Device/GraphicsDevice/GraphicsDevice.h"
#include "Engine/Graphics/Device/CommandContext/CommandContext.h"
#include "Engine/Graphics/Device/CommandContext/CommandPool/CommandPool.h"
#include "Engine/Graphics/Device/FrameManager/FrameManager.h"
#include "Engine/Graphics/Device/AsyncGPUManager/AsyncGPUManager.h"

namespace Engine::Graphics
{
	// unique_ptr の中身が完全型として見えるここで生成・破棄を定義する
	RenderDevice::RenderDevice() = default;
	RenderDevice::~RenderDevice() = default;

	bool RenderDevice::Init(bool a_isDebug)
	{
		m_upGraphicsDevice = std::make_unique<GraphicsDevice>();
		m_upGraphicsDevice->Create(a_isDebug);

		auto* _pDevice = m_upGraphicsDevice->RefDevice();
		if (!_pDevice)
		{
			ENGINE_ERRLOG(false, "デバイスの作成に失敗しました");
			return false;
		}

		// コマンドキュー(描画・コピー・コンピュート)
		m_upCommandContext = std::make_unique<CommandContext>();
		m_upCommandContext->Init(_pDevice);

		// 非同期転送の完了監視
		m_upAsyncGPUManager = std::make_unique<AsyncGPUManager>();
		m_upAsyncGPUManager->Init();

		// フレーム同期
		m_upFrameManager = std::make_unique<FrameManager>();
		m_upFrameManager->Init(_pDevice);

		ENGINE_LOG("デバイスとコマンドキューを作成");
		return true;
	}

	void RenderDevice::Release()
	{
		// GPUの完了を待ってからフレーム同期を片付ける
		if (m_upFrameManager)
		{
			m_upFrameManager->Release();
			m_upFrameManager.reset();
		}

		// 非同期転送の監視スレッドを止める
		if (m_upAsyncGPUManager)
		{
			m_upAsyncGPUManager->Release();
			m_upAsyncGPUManager.reset();
		}

		// コマンドキューとコマンドリスト(各プールがキューを空にしてから手放す)
		if (m_upCommandContext)
		{
			m_upCommandContext->RefDirectPool()->Release();
			m_upCommandContext->RefCopyPool()->Release();
			m_upCommandContext->RefComputePool()->Release();
			m_upCommandContext.reset();
		}

		if (!m_upGraphicsDevice) return;

		// 最後にデバイス。残っているオブジェクトはここでリークとして報告される
		m_upGraphicsDevice->Release();
		m_upGraphicsDevice.reset();
	}

	D3D12::Device* RenderDevice::RefDevice()
	{
		return m_upGraphicsDevice ? m_upGraphicsDevice->RefDevice() : nullptr;
	}

	//==========================================================================================
	//
	// コマンドキュー・フレーム同期
	//
	//==========================================================================================
	D3D12::GraphicsCommandList* RenderDevice::AcquireDirectCommandList()
	{
		// このフレームのアロケーターで記録を始める
		return m_upCommandContext->RefDirectPool()->AcquireList(
			RefDevice(),
			m_upFrameManager->GetCurrentAllocator()
		);
	}

	void RenderDevice::SubmitDirectCommandList(D3D12::GraphicsCommandList* a_pCmdList)
	{
		m_upCommandContext->RefDirectPool()->SubmitList(a_pCmdList);
	}

	void RenderDevice::ExecuteImmediate(D3D12::GraphicsCommandList* a_pCmdList)
	{
		m_upCommandContext->RefDirectPool()->ExecuteImmediate(a_pCmdList);
	}

	D3D12::CommandQueue* RenderDevice::RefDirectCommandQueue()
	{
		return m_upCommandContext ? m_upCommandContext->RefDirectPool()->RefCommandQueue() : nullptr;
	}

	void RenderDevice::SetBeforeExecuteHook(std::function<void(D3D12_COMMAND_LIST_TYPE, D3D12::CommandQueue*)> a_hook)
	{
		if (!m_upCommandContext) return;

		CommandPool* _pPools[] = {
			m_upCommandContext->RefDirectPool(),
			m_upCommandContext->RefCopyPool(),
			m_upCommandContext->RefComputePool(),
		};
		for (CommandPool* _pPool : _pPools)
		{
			if (!_pPool) continue;

			if (!a_hook)
			{
				_pPool->SetBeforeExecuteHook(nullptr);
				continue;
			}

			// プールは自分のキューしか知らないので、種類はここで添えて渡す
			const D3D12_COMMAND_LIST_TYPE _type = _pPool->GetQueueType();
			_pPool->SetBeforeExecuteHook([a_hook, _type](D3D12::CommandQueue* a_pQueue) { a_hook(_type, a_pQueue); });
		}
	}

	UINT RenderDevice::GetCurrentFrameIndex() const
	{
		return m_upFrameManager ? m_upFrameManager->GetCPUFrameIndex() : 0;
	}

	void RenderDevice::WaitForFrame()
	{
		m_upFrameManager->WaitForAll();
	}

	void RenderDevice::WaitForGPUIdle()
	{
		// フレームのフェンスは Present より前に打たれているので、それを待っても
		// Present は終わっていない。キューごとに新しくシグナルを打って待つ
		m_upCommandContext->RefDirectPool()->WaitIdle();
		m_upCommandContext->RefCopyPool()->WaitIdle();
		m_upCommandContext->RefComputePool()->WaitIdle();
	}

	UINT64 RenderDevice::GetCurrentFenceValue() const
	{
		return m_upFrameManager->GetCurrentFenceValue();
	}
	UINT64 RenderDevice::GetCompletedFenceValue() const
	{
		return m_upFrameManager->GetCompletedFenceValue();
	}
	UINT64 RenderDevice::GetNextFenceValue() const
	{
		return m_upFrameManager->GetNextFenceValue();
	}

	//==========================================================================================
	//
	// 非同期転送
	//
	//==========================================================================================
	void RenderDevice::ExecuteAsyncCopy(std::function<void(D3D12::GraphicsCommandList*)> a_recordCmds, std::function<void()> a_onComplete)
	{
		auto* _pDevice = RefDevice();
		auto* _pCopyPool = m_upCommandContext->RefCopyPool();

		// 非同期マネージャーからアロケーターをもらう
		auto* _allocator = m_upAsyncGPUManager->AcquireAllocator(_pDevice, EAsyncCommandType::Copy);

		// コピー用のコマンドプールからリストをもらう (内部で_allocatorを使ってResetされる)
		D3D12::GraphicsCommandList* _cmdList = _pCopyPool->AcquireList(_pDevice, _allocator);

		// 外部から渡された「コマンドを積む処理」を実行
		if (a_recordCmds)
		{
			a_recordCmds(_cmdList);
		}

		// プールにリストを返し、即座に実行する(戻り値のフェンス値を受け取る)
		_pCopyPool->SubmitList(_cmdList);
		UINT64 _fenceValue = _pCopyPool->ExecutePendingLists();

		// 非同期マネージャーに監視を依頼する（キュー管理と寿命監視の連携）
		m_upAsyncGPUManager->RegisterTask(
			EAsyncCommandType::Copy,
			_allocator,
			_pCopyPool->RefFence(),
			_fenceValue,
			a_onComplete
		);
	}

	AsyncBuildBatch RenderDevice::BeginAsyncBuildBatch(bool a_useCopy, bool a_useCompute)
	{
		auto* _pDevice = RefDevice();
		AsyncBuildBatch _batch = {};

		// コピー用
		if (a_useCopy)
		{
			_batch.pCopyAllocator = m_upAsyncGPUManager->AcquireAllocator(_pDevice, EAsyncCommandType::Copy);
			_batch.pCopyCmdList = m_upCommandContext->RefCopyPool()->AcquireList(_pDevice, _batch.pCopyAllocator);
		}

		// コンピュート用
		if (a_useCompute)
		{
			_batch.pComputeAllocator = m_upAsyncGPUManager->AcquireAllocator(_pDevice, EAsyncCommandType::Compute);
			_batch.pComputeCmdList = m_upCommandContext->RefComputePool()->AcquireList(_pDevice, _batch.pComputeAllocator);
		}

		return _batch;
	}

	void RenderDevice::EndAsyncBuildBatch(AsyncBuildBatch& a_batch, std::function<void()> a_onComplete)
	{
		auto* _pCopyPool = m_upCommandContext->RefCopyPool();
		auto* _pComputePool = m_upCommandContext->RefComputePool();

		const bool _hasCopy = (a_batch.pCopyCmdList != nullptr);
		const bool _hasCompute = (a_batch.pComputeCmdList != nullptr);

		// 完了したら呼ぶものを 1 つにまとめる : 呼び出し側から渡されたものと、
		// ビルドの途中で積まれたもの(BLAS の圧縮の依頼など)
		std::function<void()> _onComplete = nullptr;
		if (a_onComplete || !a_batch.onCompleteFuncs.empty())
		{
			_onComplete = [_first = std::move(a_onComplete), _funcs = std::move(a_batch.onCompleteFuncs)]()
				{
					if (_first) _first();
					for (const auto& _func : _funcs)
					{
						if (_func) _func();
					}
				};
		}

		// ---- コピーの実行 ----
		UINT64 _copyFenceValue = 0;
		if (_hasCopy)
		{
			_pCopyPool->SubmitList(a_batch.pCopyCmdList);
			_copyFenceValue = _pCopyPool->ExecutePendingLists();
		}

		// ---- コンピュートの実行 ----
		if (_hasCompute)
		{
			// BLASはコピーで転送したメガバッファを直接読むため、
			// コピーの完了をコンピュートキュー側で待たせる
			if (_hasCopy)
			{
				_pComputePool->RefCommandQueue()->Wait(_pCopyPool->RefFence(), _copyFenceValue);
			}

			_pComputePool->SubmitList(a_batch.pComputeCmdList);
			UINT64 _computeFenceValue = _pComputePool->ExecutePendingLists();

			// 完了通知と中間バッファの解放は、GPU処理の最後になるコンピュート側に載せる。
			// コンピュートはコピーの完了を待ってから走るので、ここが終われば両方とも終わっている。
			// (コピー側で解放すると、まだ走っている BLAS のビルドからスクラッチが消える)
			m_upAsyncGPUManager->RegisterTask(
				EAsyncCommandType::Compute,
				a_batch.pComputeAllocator,
				_pComputePool->RefFence(),
				_computeFenceValue,
				[_keepAlive = std::move(a_batch.keepAliveResources), _onComplete = std::move(_onComplete)]()
				{
					// _keepAlive のデストラクタで中間バッファとスクラッチが解放される
					if (_onComplete) _onComplete();
				}
			);

			// コンピュート側で消化したので、コピー側では呼ばない
			_onComplete = nullptr;
			a_batch.keepAliveResources.clear();
		}

		// ---- コピー側のアロケーター返却と中間バッファの解放 ----
		if (_hasCopy)
		{
			m_upAsyncGPUManager->RegisterTask(
				EAsyncCommandType::Copy,
				a_batch.pCopyAllocator,
				_pCopyPool->RefFence(),
				_copyFenceValue,
				[_keepAlive = std::move(a_batch.keepAliveResources), _onComplete = std::move(_onComplete)]()
				{
					// _keepAlive のデストラクタで中間のUploadバッファが解放される
					if (_onComplete) _onComplete();
				}
			);
		}

		// 使い終わったバッチを空にする
		a_batch = {};
	}

	//==========================================================================================
	//
	// フレーム
	//
	//==========================================================================================
	void RenderDevice::BeginFrame()
	{
		m_upFrameManager->BeginFrame();
	}

	void RenderDevice::EndFrame()
	{
		auto* _pDirectPool = m_upCommandContext->RefDirectPool();

		// 積んだリストを流して、フレーム終了のシグナルを打つ
		_pDirectPool->ExecutePendingLists();
		m_upFrameManager->EndFrame(_pDirectPool->RefCommandQueue());
	}
}
