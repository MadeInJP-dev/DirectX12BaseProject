#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ZPrePass/ZPrePass.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

namespace Engine::Graphics::Pipeline
{
	void ZPrePass::SetupSlots()
	{
		// 不透明のモデルを受け取る
		m_geometryQueue = EGeometryQueue::Opaque;

		// 深度だけを書く。あとで SRV としても読めるように TYPELESS で確保する
		Slot& _depth = DeclareOutput(
			"Depth", "SceneDepth", DXGI_FORMAT_R32_TYPELESS, EAccessType::DepthWrite);
		_depth.loadOp = ELoadOp::Clear;
	}

	void ZPrePass::Compile(const PassContext& a_context)
	{
		if (!a_context.pGraphicsEngine) return;
		if (!a_context.pAssetDatabase || !a_context.pResourceManager) return;

		auto* _pPSOManager = a_context.pGraphicsEngine->RefPipelineStateManager();
		if (!_pPSOManager) return;

		auto& _assetDB = *a_context.pAssetDatabase;
		auto& _resManager = *a_context.pResourceManager;

		// 深度だけを書くので、クリップ座標だけを出すメッシュシェーダーを使う。
		// 座標の変換は UberMS と同じ関数を通すので、GBuffer の EQUAL 判定とずれない
		const auto _guidMS = _assetDB.GetGUIDFromFilePath("Asset/Shader/Source/Geometry/MeshShader/DepthOnlyMS.cso");
		const auto _msHandle = _resManager.LoadImmediate<Resource::Shader>(_guidMS);
		m_pipelineBuilder.RegisterMeshShader(EShaderPermutationFlags::Static, _msHandle);
		m_pipelineBuilder.RegisterMeshShader(EShaderPermutationFlags::Skinned, _msHandle);

		const auto _guidAS = _assetDB.GetGUIDFromFilePath("Asset/Shader/Source/Geometry/MeshShader/TestAS.cso");
		const auto _asHandle = _resManager.LoadImmediate<Resource::Shader>(_guidAS);
		m_pipelineBuilder.RegisterAmplificationShader(EShaderPermutationFlags::Static, _asHandle);
		m_pipelineBuilder.RegisterAmplificationShader(EShaderPermutationFlags::Skinned, _asHandle);

		// 深度だけを書くのでピクセルシェーダーは持たない
		m_defaultPSHandle = {};

		// フェード中の物(カメラに近い障害物・自機)だけは、GBuffer と同じディザーで画素を捨てる。
		// GBuffer は ZPre の深度と EQUAL で比べるので、こちらで捨てないと捨てた画素に深度が残り、
		// 後ろの物が描かれずに穴が開く
		const auto _guidFadePS = _assetDB.GetGUIDFromFilePath("Asset/Shader/Source/Geometry/MeshShader/DepthDitherFadePS.cso");
		m_pipelineBuilder.RegisterDitherFadePixelShader(_resManager.LoadImmediate<Resource::Shader>(_guidFadePS));

		m_rootSigHandle = _pPSOManager->Request("Asset/Shader/Source/Geometry/MeshShader/UberMS.cso");

		// 深度テスト有効・書き込み有効
		m_pipelineBuilder.SetDepthConfig(true, true, D3D12_COMPARISON_FUNC_LESS_EQUAL);
	}

	void ZPrePass::Update(const PassContext& a_context)
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



	void ZPrePass::Archive(Engine::Persistence::Archive& a_arch)
	{
		(void)a_arch;
	}
}
