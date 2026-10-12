#include "Engine/Graphics/RenderingPipeline/RenderingPasses/UI/UIPass/UIPass.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/D3D12/InputLayout.h"

namespace Engine::Graphics::Pipeline
{
	void UIPass::SetupSlots()
	{
		// 描き足す先 : 「前段が描いた絵の上に重ねる」という順序をこの線で表す
		DeclareInput("Color", EAccessType::RTV, EPassSlotType::Texture, false);

		// すでに描かれている絵へ重ねるので Load
		Slot& _color = DeclareOutput("Color", "AfterUI", DXGI_FORMAT_R16G16B16A16_FLOAT,EAccessType::RTV);
		_color.loadOp = ELoadOp::Load;

		// スキャンラインを掛けるUIのマスク(r : 1 = 掛ける)。ScanLinePass の Mask へ繋ぐ。
		// 毎フレーム 0(どこにも掛けない)で消してから描く。
		// RTV の並び = シェーダーの SV_Target の番号なので、Color より後に宣言すること
		Slot& _scanLineMask = DeclareOutput("ScanLineMask", "UIScanLineMask", DXGI_FORMAT_R8_UNORM, EAccessType::RTV);
		_scanLineMask.loadOp = ELoadOp::Clear;
		_scanLineMask.clearColor = { 0.f, 0.f, 0.f, 0.f };
	}

	// 描き足す先が繋がっていれば、そのリソースへ重ねる
	void UIPass::OnLinksResolved()
	{
		FollowInputToOutput("Color", "Color");
	}

	void UIPass::Compile(const PassContext& a_context)
	{
		SetupRasterShader(
			a_context,
			"Asset/Shader/Source/UI/UIVS.cso",
			"Asset/Shader/Source/UI/UIPS.cso",
			D3D12::Input::PARTICLE_INPUT_LAYOUT,
			"UIPso",
			[](D3D12::GraphicsPipelineDesc& a_pso)
			{
				// UIは深度を使わない。DSVを張らないので深度フォーマットもUNKNOWNのまま。
				// (でないと #615 DEPTH_STENCIL_FORMAT_MISMATCH_PIPELINE_STATE になる)
				a_pso.DepthEnable(false);
				a_pso.StencilEnable(false);

				// UIクアッドの巻き順は表裏が混在しており、裏面カリングだと三角形が消える。
				// スクリーン向きの板ポリなのでカリング自体を無効化する
				a_pso.CullMode(D3D12_CULL_MODE_NONE);

				// 半透明UIをシーンへアルファ合成する。
				// ターゲットごとのブレンドは使わないので、マスクも同じ式で重なる
				a_pso.BlendEnable(true);
				a_pso.SrcBlend(D3D12_BLEND_SRC_ALPHA);
				a_pso.DestBlend(D3D12_BLEND_INV_SRC_ALPHA);
				a_pso.BlendOp(D3D12_BLEND_OP_ADD);
				a_pso.SrcBlendAlpha(D3D12_BLEND_ONE);
				a_pso.DestBlendAlpha(D3D12_BLEND_INV_SRC_ALPHA);
				a_pso.BlendOpAlpha(D3D12_BLEND_OP_ADD);
			},
			EPassHeapMode::Bindless);
	}

	void UIPass::Update(const PassContext& a_context)
	{
		RenderContext* _pCtx = a_context.pRenderContext;
		if (!_pCtx) return;

		// ヒープ・ルートシグネチャ・PSO・レンダーターゲットはグラフが張り終えている。
		// DrawPolygonInstancing はトポロジを設定しないので、ここで明示する
		_pCtx->SetPrimitive(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// UIインスタンスバッファ(t0)を張るのは DrawUI の中。
		// 湾曲するUIは板ポリが変わるぶんドローが分かれ、その都度張り直す必要がある
		_pCtx->DrawUI(0);
	}



	void UIPass::Archive(Engine::Persistence::Archive& a_arch)
	{
		(void)a_arch;
	}
}
