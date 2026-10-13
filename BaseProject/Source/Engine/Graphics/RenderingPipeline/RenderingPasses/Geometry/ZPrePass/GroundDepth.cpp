#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ZPrePass/GroundDepth.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

namespace Engine::Graphics::Pipeline
{
	void Engine::Graphics::Pipeline::GroundDepthPass::SetupSlots()
	{
		// 地面の描画キュー
		m_geometryQueue = EGeometryQueue::Ground;

		// 深度だけ書く
		Slot& _depth = DeclareOutput("Depth", "GroundDepth", DXGI_FORMAT_R32_TYPELESS, EAccessType::DepthWrite);
		_depth.loadOp = ELoadOp::Clear;
	}
	void GroundDepthPass::Compile(const PassContext & a_context)
	{
		if (!a_context.pGraphicsEngine) return;
		if (!a_context.pAssetDatabase || !a_context.pResourceManager) return;

		auto* _pPSOManager = a_context.pGraphicsEngine->RefPipelineStateManager();
		if (!_pPSOManager) return;
		auto& _assetDB = *a_context.pAssetDatabase;
		auto& _resManager = *a_context.pResourceManager;

		// MSセット
		// 深度だけを書くので、クリップ座標だけを出すメッシュシェーダーを使う
		const auto _guidMS = _assetDB.GetGUIDFromFilePath("Asset/Shader/Source/Geometry/MeshShader/DepthOnlyMS.cso");
		const auto _msHandle = _resManager.LoadImmediate<Resource::Shader>(_guidMS);
		m_pipelineBuilder.RegisterMeshShader(EShaderPermutationFlags::Static, _msHandle);

		// ASセット
		const auto _guidAS = _assetDB.GetGUIDFromFilePath("Asset/Shader/Source/Geometry/MeshShader/TestAS.cso");
		const auto _asHandle = _resManager.LoadImmediate<Resource::Shader>(_guidAS);
		m_pipelineBuilder.RegisterAmplificationShader(EShaderPermutationFlags::Static, _asHandle);
		
		// 深度だけ書くのでピクセルシェーダーは持たない
		m_defaultPSHandle = {};

		// ルートシグネチャセット
		m_rootSigHandle = _pPSOManager->Request("Asset/Shader/Source/Geometry/MeshShader/UberMS.cso");

		// 深度テスト有効・書き込み有効
		m_pipelineBuilder.SetDepthConfig(true, true, D3D12_COMPARISON_FUNC_LESS_EQUAL);
	}
	void GroundDepthPass::Update(const PassContext & a_context)
	{
		RenderContext* _pCtx = a_context.pRenderContext;
		if (!_pCtx) return;

		_pCtx->BindBindlessHeaps();
		_pCtx->SetGraphicsRootSignature(m_rootSigHandle);

		_pCtx->BindCamera();
		_pCtx->BindMeshInstance();
		_pCtx->BindMeshlet();

		_pCtx->DrawQueueDispatchMesh(GetPassIndex());
	}
	void GroundDepthPass::Archive(Engine::Persistence::Archive & a_arch)
	{
		(void)a_arch;
	}
}