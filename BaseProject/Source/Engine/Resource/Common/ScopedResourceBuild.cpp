#include "ScopedResourceBuild.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshBufferAllocator.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

namespace Engine::Resource
{
	ScopedResourceBuild::ScopedResourceBuild(ResourceManager* a_pResourceManager, bool a_useCopy, bool a_useCompute)
	{
		Open(a_pResourceManager, a_useCopy, a_useCompute);

		// 作ったものを登録する経路なので、登録先が無いのは呼び出し側の渡し忘れ
		if (!a_pResourceManager)
		{
			ENGINE_ERRLOG(false, "[Resource] ビルドの登録先(ResourceManager)が渡されていません");
		}
	}

	ScopedResourceBuild::ScopedResourceBuild(TransferOnly, bool a_useCopy, bool a_useCompute)
	{
		// 登録しないので登録先は持たない
		Open(nullptr, a_useCopy, a_useCompute);
	}

	void ScopedResourceBuild::Open(ResourceManager* a_pResourceManager, bool a_useCopy, bool a_useCompute)
	{
		// バッチを開くのも転送を流すのもグラフィックスエンジン
		auto* _pGE = MainEngine::Instance().RefGraphicsEngine();
		if (!_pGE)
		{
			ENGINE_ERRLOG(false, "リソースのビルドにグラフィックスエンジンがありません");
			return;
		}

		// バッチを開く
		m_batch = _pGE->RefRenderDevice()->BeginAsyncBuildBatch(a_useCopy, a_useCompute);

		// コンテキストの組み立て
		m_context.pDevice = _pGE->RefRenderDevice()->RefDevice();
		m_context.pCopyCmdList = m_batch.pCopyCmdList;
		m_context.pComputeCmdList = m_batch.pComputeCmdList;
		m_context.pKeepAliveUploads = &m_batch.keepAliveResources;
		m_context.pOnBuildComplete = &m_batch.onCompleteFuncs;

		m_context.pResourceManager = a_pResourceManager;
		m_context.pAssetDatabase = a_pResourceManager ? &a_pResourceManager->RefAssetDatabase() : nullptr;

		m_context.pGraphicsEngine = _pGE;
		m_context.pMeshBufferAllocator = _pGE->RefMeshBufferAllocator();
		m_context.pPassMetaRegistry = _pGE->RefPassMetaRegistry();

		// ビューの置き場。実体はグラフィックスエンジンの持ち物
		m_context.pHeapManager = _pGE->RefDescriptorHeapManager();
	}

	ScopedResourceBuild::~ScopedResourceBuild()
	{
		// スコープを抜けるところで実行
		if (!m_context.pGraphicsEngine) return;
		m_context.pGraphicsEngine->RefRenderDevice()->EndAsyncBuildBatch(m_batch);
	}
}
