#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/TransparentPass/TransparentPass.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/Frame/SceneView/AmbientData.h"
#include "Engine/Graphics/D3D12/InputLayout.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

namespace Engine::Graphics::Pipeline
{
	namespace
	{
		// 前方ライティングの値を渡すルートパラメーターの番号。
		// ※ HLSL 側(MeshCommon.hlsli の MESHGLOBAL_ROOT_SIG の 13 番)と合わせること
		constexpr UINT ROOT_FORWARD_LIGHT = 13;	// RootConstants(b2)

		// ROOT_FORWARD_LIGHT へ詰める値。
		// ※ HLSL 側(MeshTransparentPS.hlsl の ForwardLightConstants)と並びを合わせること
		struct ForwardLightConstants
		{
			UINT pointLightsIndex = 0;			// ポイントライト配列の SRV 番号
			UINT directionalLightsIndex = 0;	// 平行光配列の SRV 番号
			UINT directionalNum = 0;			// 平行光の数
			UINT pointNum = 0;					// ポイントライトの数
			Math::Vector3 ambientColor = {};	// 環境光
			float pad = 0.0f;
		};
		static_assert(sizeof(ForwardLightConstants) == sizeof(UINT) * 8, "MESHGLOBAL_ROOT_SIG の RootConstants(b2) と大きさを合わせること");

		// トレイルのルートパラメーターの番号。
		// ※ HLSL 側(Asset/Shader/Source/Geometry/Trail/Trail.hlsli の TRAIL_ROOT_SIG)と合わせること
		constexpr UINT ROOT_TRAIL_VERTEX = 1;	// SRV(t0) 全トレイルの頂点
		constexpr UINT ROOT_TRAIL_CONST = 2;	// RootConstants(b1) 帯1本ぶん
	}

	void TransparentPass::SetupSlots()
	{
		// 半透明のモデルを受け取る
		m_geometryQueue = EGeometryQueue::Transparent;

		// 深度は読むだけ : 不透明の物の後ろに回った半透明を隠す。書かないので半透明同士は描く順で重なる。
		// 単体で置いた構成も作れるよう任意にしてある
		DeclareInput("Depth", EAccessType::DepthRead, EPassSlotType::Texture, false);

		// 描き足す先 : 「ライティングが描いた絵の上に重ねる」という順序をこの線で表す
		DeclareInput("Color", EAccessType::RTV, EPassSlotType::Texture, false);

		// すでに描かれている絵へ重ねるので Load。
		// HDR : ライティングと同じフォーマットで揃える
		Slot& _color = DeclareOutput("Color", "AfterTransparentColor", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::RTV);
		_color.loadOp = ELoadOp::Load;
	}

	// 描き足す先が繋がっていれば、そのリソースへ重ねる
	void TransparentPass::OnLinksResolved()
	{
		FollowInputToOutput("Color", "Color");
	}

	void TransparentPass::Compile(const PassContext& a_context)
	{
		if (!a_context.pGraphicsEngine) return;
		if (!a_context.pAssetDatabase || !a_context.pResourceManager) return;

		auto* _pPSOManager = a_context.pGraphicsEngine->RefPipelineStateManager();
		if (!_pPSOManager) return;

		auto& _assetDB = *a_context.pAssetDatabase;
		auto& _resManager = *a_context.pResourceManager;

		// ---- メッシュシェーダー / 増幅シェーダー : GBuffer と同じもの ----
		const auto _guidMS = _assetDB.GetGUIDFromFilePath("Asset/Shader/Source/Geometry/MeshShader/UberMS.cso");
		const auto _msHandle = _resManager.LoadImmediate<Resource::Shader>(_guidMS);
		m_pipelineBuilder.RegisterMeshShader(EShaderPermutationFlags::Static, _msHandle);
		m_pipelineBuilder.RegisterMeshShader(EShaderPermutationFlags::Skinned, _msHandle);

		const auto _guidAS = _assetDB.GetGUIDFromFilePath("Asset/Shader/Source/Geometry/MeshShader/TestAS.cso");
		const auto _asHandle = _resManager.LoadImmediate<Resource::Shader>(_guidAS);
		m_pipelineBuilder.RegisterAmplificationShader(EShaderPermutationFlags::Static, _asHandle);
		m_pipelineBuilder.RegisterAmplificationShader(EShaderPermutationFlags::Skinned, _asHandle);

		// ---- ピクセルシェーダー : 光を計算して色を直接出す ----
		const auto _guidPS = _assetDB.GetGUIDFromFilePath("Asset/Shader/Source/Geometry/Transparent/MeshTransparentPS.cso");
		m_defaultPSHandle = _resManager.LoadImmediate<Resource::Shader>(_guidPS);

		// ---- ルートシグネチャ ----
		m_meshRootSigHandle = _pPSOManager->Request("Asset/Shader/Source/Geometry/MeshShader/UberMS.cso");

		//----------------------------------------------------------------------------------
		// 深度テスト
		//
		// 深度が繋がっていれば読むだけ(書かない)。
		// 繋がっていなければ深度テストごと切る : PSO だけ深度ありにすると、
		// グラフが DSV を張らないぶんと食い違って描画が落とされる
		//----------------------------------------------------------------------------------
		const Slot* _pDepth = FindInputSlot(MakeSlotID("Depth"));
		const bool _isDepth = (_pDepth && _pDepth->IsConnected());

		if (_isDepth)	m_pipelineBuilder.SetDepthConfig(true, false, D3D12_COMPARISON_FUNC_LESS_EQUAL);
		else			m_pipelineBuilder.SetDepthConfig(false, false, D3D12_COMPARISON_FUNC_ALWAYS);

		// 下地を α のぶん隠して色を置く
		m_pipelineBuilder.SetAlphaBlend(true);

		//----------------------------------------------------------------------------------
		// トレイル : 頂点は CPU が組んだものを VS が SV_VertexID で引く
		//
		// 合成はプリマルチプライド(1 × 色 + (1 - α) × 下地)。
		// PS が α を出せば半透明、0 を出せば加算になるので、1つの PSO で両方を描ける。
		// 帯は裏からも見えるので裏も描く
		//----------------------------------------------------------------------------------
		SetupRasterShader(
			a_context,
			"Asset/Shader/Source/Geometry/Trail/TrailVS.cso",
			"Asset/Shader/Source/Geometry/Trail/TrailPS.cso",
			D3D12::Input::EMPTY_LAYOUT,
			"TrailPSO",
			[_isDepth](D3D12::GraphicsPipelineDesc& a_pso)
			{
				a_pso.BlendEnable(true);
				a_pso.SrcBlend(D3D12_BLEND_ONE, 0);
				a_pso.DestBlend(D3D12_BLEND_INV_SRC_ALPHA, 0);
				a_pso.BlendOp(D3D12_BLEND_OP_ADD, 0);

				a_pso.SrcBlendAlpha(D3D12_BLEND_ONE, 0);
				a_pso.DestBlendAlpha(D3D12_BLEND_INV_SRC_ALPHA, 0);
				a_pso.BlendOpAlpha(D3D12_BLEND_OP_ADD, 0);

				a_pso.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

				a_pso.CullMode(D3D12_CULL_MODE_NONE);

				if (_isDepth)
				{
					a_pso.DepthEnable(true);
					a_pso.DepthWriteMask(false);
					a_pso.DepthFunc(D3D12_COMPARISON_FUNC_LESS_EQUAL);
				}
				else
				{
					a_pso.DepthEnable(false);
					a_pso.StencilEnable(false);
				}
			},
			EPassHeapMode::Bindless,
			&m_trailPSOHandle);

		// SetupRasterShader はルートシグネチャをパス共通の欄へ入れるので、トレイル用として控えておく
		m_trailRootSigHandle = Pass::m_rootSigHandle;
	}

	void TransparentPass::Update(const PassContext& a_context)
	{
		RenderContext* _pCtx = a_context.pRenderContext;
		GraphicsEngine* _pGE = a_context.pGraphicsEngine;
		if (!_pCtx || !_pGE) return;

		// レンダーターゲットの切り替えはグラフが済ませてある
		_pCtx->BindBindlessHeaps();
		_pCtx->SetGraphicsRootSignature(m_meshRootSigHandle);

		_pCtx->BindCamera();
		_pCtx->BindMeshInstance();
		_pCtx->BindMeshlet();

		//----------------------------------------------------------------------------------
		// 前方ライティングの値
		//
		// ライト配列は LightManager が今フレームぶん詰め直したもの(ライティングパスと同じ)。
		// 環境光は GI の代わりに一様な間接光として使う
		//----------------------------------------------------------------------------------
		const auto& _frameLight = _pGE->GetFrameLightData();

		ForwardLightConstants _constants = {};
		_constants.pointLightsIndex = _frameLight.plBuffer.GetSRV().GetIndex();
		_constants.directionalLightsIndex = _frameLight.dlBuffer.GetSRV().GetIndex();
		_constants.directionalNum = _frameLight.dlCount;
		_constants.pointNum = _frameLight.plCount;
		_constants.ambientColor = _pGE->GetSceneView()->GetAmbientData().ambientColorScale;

		const auto _words = std::bit_cast<std::array<UINT, 8>>(_constants);
		_pCtx->GraphicsBindDescriptorIndices(ROOT_FORWARD_LIGHT, _words);

		// 自分のパス番号で積まれた半透明のアイテムを、奥から手前の順に描く
		_pCtx->DrawTransparentQueueDispatchMesh(GetPassIndex());

		//----------------------------------------------------------------------------------
		// トレイル : 半透明のモデルの後に、帯を遠い順に描く
		//
		// ルートシグネチャが変わるとそれまでのバインドは消えるので、カメラから張り直す
		//----------------------------------------------------------------------------------
		if (m_trailPSOHandle.IsValid() && m_trailRootSigHandle.IsValid())
		{
			_pCtx->SetGraphicsRootSignature(m_trailRootSigHandle);
			_pCtx->SetGraphicPSO(m_trailPSOHandle);
			_pCtx->BindCamera();
			_pCtx->DrawTrails(ROOT_TRAIL_VERTEX, ROOT_TRAIL_CONST);
		}
	}



	void TransparentPass::Archive(Engine::Persistence::Archive& a_arch)
	{
		(void)a_arch;
	}
}
