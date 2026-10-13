#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Lighting/Shadow/ShadowMapPass/ShadowMapPass.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"
#include "Engine/Graphics/RenderingPipeline/RenderGraph/RenderGraph.h"
#include "Engine/Graphics/RenderingPipeline/RenderGraph/Resource/VirtualResource/VirtualResource.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

namespace Engine::Graphics::Pipeline
{
	void ShadowMapPass::SetupSlots()
	{
		// 不透明のモデルを受け取る
		m_geometryQueue = EGeometryQueue::Opaque;

		// 深度だけを書く。後ろのパスが SRV として読むので TYPELESS で確保する。
		// 大きさは画面の解像度に依らないので、固定で宣言する(ApplyResolution で入れる)
		Slot& _map = DeclareOutput(
			"ShadowMap", "SunShadowMap", DXGI_FORMAT_R32_TYPELESS, EAccessType::DepthWrite);
		_map.loadOp = ELoadOp::Clear;

		ApplyResolution();
	}

	void ShadowMapPass::ApplyResolution()
	{
		Slot* _pOut = FindOutputSlot(MakeSlotID("ShadowMap"));
		if (!_pOut) return;

		// タイルに割り切れる大きさにそろえる
		m_params.resolution = std::clamp(m_params.resolution, 256u, 16384u);
		m_params.resolution -= m_params.resolution % ATLAS_TILES;

		_pOut->width = m_params.resolution;
		_pOut->height = m_params.resolution;
		_pOut->scale = 1.0f;
	}

	void ShadowMapPass::Compile(const PassContext& a_context)
	{
		if (!a_context.pGraphicsEngine) return;
		if (!a_context.pAssetDatabase || !a_context.pResourceManager) return;

		auto* _pPSOManager = a_context.pGraphicsEngine->RefPipelineStateManager();
		if (!_pPSOManager) return;

		auto& _assetDB = *a_context.pAssetDatabase;
		auto& _resManager = *a_context.pResourceManager;

		// 形の読み方とカリングは ZPre と同じ。
		// カメラの定数バッファにライトのカメラを張れば、そのまま光源から見た絵になる
		// 深度だけを書くので、クリップ座標だけを出すメッシュシェーダーを使う
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

		m_rootSigHandle = _pPSOManager->Request("Asset/Shader/Source/Geometry/MeshShader/UberMS.cso");

		// 深度テスト有効・書き込み有効
		m_pipelineBuilder.SetDepthConfig(true, true, D3D12_COMPARISON_FUNC_LESS_EQUAL);
	}

	void ShadowMapPass::Update(const PassContext& a_context)
	{
		RenderContext* _pCtx = a_context.pRenderContext;
		GraphicsEngine* _pGE = a_context.pGraphicsEngine;
		if (!_pCtx || !_pGE || !a_context.pCmdList || !a_context.pGraph) return;

		// レイトレで求めるフレーム・平行光が無いフレームはカスケードが 0 になっている
		const LightManager* _pLightManager = _pGE->RefLightManager();
		const uint32_t _cascadeCount = _pLightManager->GetShadowCascadeCount();
		if (_cascadeCount == 0) return;

		// タイルの大きさはリソースから引く(宣言どおりに作られているとは限らないため)
		const Slot* _pOut = FindOutputSlot(MakeSlotID("ShadowMap"));
		if (!_pOut) return;

		const VirtualResource* _pRes = a_context.pGraph->GetVirtualResource(_pOut->resourceID);
		if (!_pRes) return;

		const UINT _tileSize = static_cast<UINT>(_pRes->GetWidth()) / ATLAS_TILES;
		if (_tileSize == 0) return;

		// 深度バッファの切り替えとクリアはグラフが済ませてある
		_pCtx->BindBindlessHeaps();
		_pCtx->SetGraphicsRootSignature(m_rootSigHandle);

		_pCtx->BindMeshInstance();
		_pCtx->BindMeshlet();

		for (uint32_t _i = 0; _i < _cascadeCount; ++_i)
		{
			// このカスケードのタイルへだけ描く。
			// グラフが張ったビューポートは画面の大きさなので、ここで張り替える
			// (後ろのパスはレンダーターゲットを切り替えるときに張り直す)
			const UINT _left = (_i % ATLAS_TILES) * _tileSize;
			const UINT _top = (_i / ATLAS_TILES) * _tileSize;

			const D3D12_VIEWPORT _viewport = {
				static_cast<float>(_left), static_cast<float>(_top),
				static_cast<float>(_tileSize), static_cast<float>(_tileSize),
				0.0f, 1.0f
			};
			const D3D12_RECT _scissor = {
				static_cast<LONG>(_left), static_cast<LONG>(_top),
				static_cast<LONG>(_left + _tileSize), static_cast<LONG>(_top + _tileSize)
			};
			a_context.pCmdList->RSSetViewports(1, &_viewport);
			a_context.pCmdList->RSSetScissorRects(1, &_scissor);

			// カメラの代わりにこのカスケードのライトのカメラを張る。
			// 増幅シェーダーのカリング(視錐台・法線コーン)もこのカメラで行われる
			_pCtx->GraphicsBindRootCBV(0, _pLightManager->GetShadowCascadeCamera(_i));

			// 自分のパス番号で積まれた描画アイテムだけを引く
			_pCtx->DrawQueueDispatchMesh(GetPassIndex());
		}
	}



	void ShadowMapPass::Archive(Engine::Persistence::Archive& a_arch)
	{
		a_arch.Field("resolution", m_params.resolution);

		if (a_arch.IsLoading()) ApplyResolution();
	}
}
