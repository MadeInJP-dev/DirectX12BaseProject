#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/BoundaryPass/BoundaryPass.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/D3D12/InputLayout.h"

namespace Engine::Graphics::Pipeline
{
	namespace
	{
		// ルートパラメーターの番号
		// 0 番はカメラ(b0)で、RenderContext::BindCamera が張る。
		// ※ HLSL 側(Asset/Shader/Source/Geometry/Boundary/Boundary.hlsli の BOUNDARY_ROOT_SIG)と合わせること
		constexpr UINT ROOT_BOUNDARY_CB = 1;	// CBV(b1) 面1枚ぶんの情報
	}

	void BoundaryPass::SetupSlots()
	{
		// 深度は読むだけ : 半透明なので書かず、手前のものには隠される。
		// 単体で置いた構成も作れるよう任意にしてある
		DeclareInput("Depth", EAccessType::DepthRead, EPassSlotType::Texture, false);

		// 描き足す先 : 「前段が描いた絵の上に重ねる」という順序をこの線で表す
		DeclareInput("Color", EAccessType::RTV, EPassSlotType::Texture, false);

		// すでに描かれている絵へ重ねるので Load。
		// HDR : ライティングと同じフォーマットで揃える
		Slot& _color = DeclareOutput("Color", "AfterBoundaryColor", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::RTV);
		_color.loadOp = ELoadOp::Load;
	}

	// 描き足す先が繋がっていれば、そのリソースへ重ねる
	void BoundaryPass::OnLinksResolved()
	{
		FollowInputToOutput("Color", "Color");
	}

	void BoundaryPass::Compile(const PassContext& a_context)
	{
		// 深度が繋がっていなければ深度テストごと切る。
		// PSOだけ深度ありにすると、グラフがDSVを張らないぶんと食い違って描画が落とされる
		const Slot* _pDepth = FindInputSlot(MakeSlotID("Depth"));
		const bool _isDepth = (_pDepth && _pDepth->IsConnected());

		SetupRasterShader(
			a_context,
			"Asset/Shader/Source/Geometry/Boundary/BoundaryVS.cso",
			"Asset/Shader/Source/Geometry/Boundary/BoundaryPS.cso",
			// VS は SV_VertexID だけで板を組み、頂点バッファを読まない。
			// レイアウトを宣言すると IA が直前のパスの残したバッファを読もうとして警告が出る
			D3D12::Input::EMPTY_LAYOUT,
			"BoundaryPSO",
			[_isDepth](D3D12::GraphicsPipelineDesc& a_pso)
			{
				// 半透明合成 : 加算にすると明るい空の前で見えなくなるので、背景を覆う方で重ねる
				a_pso.BlendEnable(true);
				a_pso.SrcBlend(D3D12_BLEND_SRC_ALPHA, 0);
				a_pso.DestBlend(D3D12_BLEND_INV_SRC_ALPHA, 0);
				a_pso.BlendOp(D3D12_BLEND_OP_ADD, 0);

				a_pso.SrcBlendAlpha(D3D12_BLEND_ONE, 0);
				a_pso.DestBlendAlpha(D3D12_BLEND_INV_SRC_ALPHA, 0);
				a_pso.BlendOpAlpha(D3D12_BLEND_OP_ADD, 0);

				a_pso.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

				// 裏も描く : プレイヤーは箱の内側に居るので、外向きの面を裏から見る
				a_pso.CullMode(D3D12_CULL_MODE_NONE);

				if (_isDepth)
				{
					a_pso.DepthEnable(true);
					a_pso.DepthWriteMask(D3D12_DEPTH_WRITE_MASK_ZERO);
					a_pso.DepthFunc(D3D12_COMPARISON_FUNC_LESS_EQUAL);
				}
				else
				{
					a_pso.DepthEnable(false);
					a_pso.StencilEnable(false);
				}
			},
			EPassHeapMode::Bindless);
	}

	void BoundaryPass::Update(const PassContext& a_context)
	{
		RenderContext* _pCtx = a_context.pRenderContext;
		if (!_pCtx) return;

		// ヒープ・ルートシグネチャ・PSO・レンダーターゲットはグラフが張り終えている
		_pCtx->SetPrimitive(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		_pCtx->BindCamera();
		_pCtx->DrawBoundary(ROOT_BOUNDARY_CB);
	}



	void BoundaryPass::Archive(Engine::Persistence::Archive& a_arch)
	{
		(void)a_arch;
	}
}
