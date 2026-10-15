#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ParticlePass/ParticlePass.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"

#include "Engine/Graphics/Particle/ParticleBufferManager.h"
#include "Engine/Graphics/Particle/GPU/GPUParticlePool/GPUParticlePool.h"
#include "Engine/Graphics/Particle/GPU/EmitterSlotPool/EmitterSlotPool.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Graphics/Frame/SceneView/CameraData.h"
#include "Engine/Graphics/D3D12/InputLayout.h"

namespace Engine::Graphics::Pipeline
{
	void ParticlePass::SetupSlots()
	{
		// 深度は読むだけ : 半透明なので書かず、手前のものには隠される。
		// 単体で置いた構成も作れるよう任意にしてある。
		// PS からも SRV で読む(奥の物との境目をぼかす)ので、番号をルートパラメータ 4 へ張らせる。
		// 繋がっていなければ無効値が届き、シェーダーはぼかしを飛ばす
		DeclareInput("Depth", EAccessType::DepthRead, EPassSlotType::Texture, false, 4);

		// 描き足す先 : 「前段が描いた絵の上に重ねる」という順序をこの線で表す
		DeclareInput("Color", EAccessType::RTV, EPassSlotType::Texture, false);

		// すでに描かれている絵へ重ねるので Load。
		// HDR : ライティングと同じフォーマットで揃える
		Slot& _color = DeclareOutput("Color", "AfterLighting", DXGI_FORMAT_R16G16B16A16_FLOAT,
			EAccessType::RTV);
		_color.loadOp = ELoadOp::Load;
	}

	// 描き足す先が繋がっていれば、そのリソースへ重ねる
	void ParticlePass::OnLinksResolved()
	{
		FollowInputToOutput("Color", "Color");
	}

	void ParticlePass::Compile(const PassContext& a_context)
	{
		// 深度が繋がっていなければ深度テストごと切る。
		// PSOだけ深度ありにすると、グラフがDSVを張らないぶんと食い違って描画が落とされる
		const Slot* _pDepth = FindInputSlot(MakeSlotID("Depth"));
		const bool _isDepth = (_pDepth && _pDepth->IsConnected());

		// 深度は共通 : 半透明なので深度は書かず、手前のものには隠される
		auto _setupCommon = [_isDepth](D3D12::GraphicsPipelineDesc& a_pso)
			{
				a_pso.desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

				// 裏も描く。カメラ正面のビルボードは常に表を向くが、
				// 発生源に合わせる向き(EmitterAxis / EmitterFacing)の板は裏から見ることがある
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
			};

		const std::string _vsPath = "Asset/Shader/Source/Particle/Draw/ParticleVS.cso";
		const std::string _psPath = "Asset/Shader/Source/Particle/Draw/ParticlePS.cso";

		// ---- 加算合成 : 光り物。重ねるほど明るくなり、描く順番に依存しない ----
		SetupRasterShader(
			a_context, _vsPath, _psPath, D3D12::Input::PARTICLE_INPUT_LAYOUT,
			"ParticleDraw_Additive",
			[&_setupCommon](D3D12::GraphicsPipelineDesc& a_pso)
			{
				a_pso.BlendEnable(true);
				a_pso.SrcBlend(D3D12_BLEND_SRC_ALPHA, 0);
				a_pso.DestBlend(D3D12_BLEND_ONE, 0);
				a_pso.BlendOp(D3D12_BLEND_OP_ADD, 0);

				a_pso.SrcBlendAlpha(D3D12_BLEND_ONE, 0);
				a_pso.DestBlendAlpha(D3D12_BLEND_ZERO, 0);
				a_pso.BlendOpAlpha(D3D12_BLEND_OP_ADD, 0);

				_setupCommon(a_pso);
			},
			EPassHeapMode::Bindless,
			&m_additivePSO);

		// ---- 半透明合成 : 煙や破片。背景を明るくせず、前のものが後ろを隠す ----
		SetupRasterShader(
			a_context, _vsPath, _psPath, D3D12::Input::PARTICLE_INPUT_LAYOUT,
			"ParticleDraw_AlphaBlend",
			[&_setupCommon](D3D12::GraphicsPipelineDesc& a_pso)
			{
				a_pso.BlendEnable(true);
				a_pso.SrcBlend(D3D12_BLEND_SRC_ALPHA, 0);
				a_pso.DestBlend(D3D12_BLEND_INV_SRC_ALPHA, 0);
				a_pso.BlendOp(D3D12_BLEND_OP_ADD, 0);

				a_pso.SrcBlendAlpha(D3D12_BLEND_ONE, 0);
				a_pso.DestBlendAlpha(D3D12_BLEND_INV_SRC_ALPHA, 0);
				a_pso.BlendOpAlpha(D3D12_BLEND_OP_ADD, 0);

				_setupCommon(a_pso);
			},
			EPassHeapMode::Bindless,
			&m_alphaBlendPSO);
	}

	void ParticlePass::Update(const PassContext& a_context)
	{
		RenderContext* _pCtx = a_context.pRenderContext;
		GraphicsEngine* _pGE = a_context.pGraphicsEngine;
		if (!_pCtx || !_pGE || !a_context.pResourceManager) return;

		auto* _particleManager = a_context.pParticleManager;
		if (!_particleManager) return;

		auto& _resManager = *a_context.pResourceManager;

		//----------------------------------------------------------
		// 発生源の席(全アセット共通の1本)
		//
		// ローカル空間で回した粒は、ここから自分の席の行列を引いてワールドへ戻す。
		// 席 0 は単位行列なので、ワールド空間の粒も同じ経路を素通りする。
		// まだ一度も転送されていなければ、引けない番号で読ませないよう描画ごと見送る
		// (席 0 は初期化で取っているので、最初のフレームの転送で必ずできる)
		//----------------------------------------------------------
		const auto* _pSlotPool = _particleManager->GetEmitterSlotPool();
		if (!_pSlotPool) return;
		const UINT _slotSRVIndex = _pSlotPool->GetSRVIndex();
		if (_slotSRVIndex == (std::numeric_limits<UINT>::max)()) return;

		// 間接描画のコマンドシグネチャ(DrawIndexed 1件)。作れていなければ描きようがない
		ID3D12CommandSignature* _pDrawSignature = _pGE->GetDrawIndexedSignature();
		if (!_pDrawSignature) return;

		//----------------------------------------------------------
		// 1アセット分を描く
		//----------------------------------------------------------
		auto _drawPool = [&](
			const Handle<Resource::ParticlesAsset>& /*a_handle*/,
			const auto& a_upPool,
			const Resource::ParticlesAsset& a_particle,
			const Handle<ID3D12PipelineState>& a_psoHandle)
			{
				// ヒープとルートシグネチャはグラフが張ってあるので、PSOだけ選び直す
				_pGE->BindPSO(_pCtx, a_psoHandle);

				// カメラバインド
				CameraData _cbCam = _pGE->GetSceneView()->GetCameraData();
				_pCtx->GraphicsBindRootCBV(0, _cbCam);

				// パーティクルデータと発生源の席と生存リスト(バインドレス : 番号をルート定数で渡す)。
				// 並びはシェーダーの PassDescriptorIndex0(粒 → 席 → 生存リスト)と同じ。
				// 生存リストはシミュレーションが NON_PIXEL_SHADER_RESOURCE にしてある(印が立っているプールだけ描く)
				const UINT _vsIndices[] = {
					a_upPool->GetParticlePoolSRV().GetIndex(),
					_slotSRVIndex,
					a_upPool->GetAliveListSRV().GetIndex(),
				};
				_pCtx->GraphicsBindDescriptorIndices(1, _vsIndices);

				// パーティクル画像
				auto* _pTex = _resManager.Get(a_particle.GetTexHandle());
				if (!_pTex) return;
				const UINT _texIndex = _pTex->GetSRV().GetIndex();
				_pCtx->GraphicsBindDescriptorIndices(2, std::span<const UINT>(&_texIndex, 1));

				// 描画設定バインド : 板ポリの向きと、寿命に沿った見た目の変化はアセット単位
				Particle::ParticleDrawData _cbDraw = {};
				_cbDraw.orientation  = static_cast<uint32_t>(a_particle.GetOrientation());
				_cbDraw.stretch      = a_particle.GetStretch();
				_cbDraw.endSizeScale = a_particle.GetEndSizeScale();
				_cbDraw.fadeInRatio  = a_particle.GetFadeInRatio();
				_cbDraw.fadeOutRatio = a_particle.GetFadeOutRatio();
				_cbDraw.softDepth    = a_particle.GetSoftDepth();
				_cbDraw.startColor   = a_particle.GetStartColor();
				_cbDraw.endColor     = a_particle.GetEndColor();

				_pCtx->GraphicsBindRootCBV(3, _cbDraw);

				// 描画 : 引数(インスタンス数など)はシミュレーションが GPU 上に用意したものを読む。
				// 引数は INDIRECT_ARGUMENT 状態になっている(シミュレーションが遷移させ、印を立てた)。
				// カメラが何台あっても、同じ引数を読んで描くだけでよい
				_pCtx->DrawPolygonIndirect(_pDrawSignature, a_upPool->GetDrawArgsResource());
			};

		//----------------------------------------------------------
		// 半透明 → 加算 の順で回す
		//
		// どちらも深度を書かないので、後から描いたものが上に乗る。
		// 煙(半透明)を先に置いてから炎(加算)を足すと、爆発の芯が煙の手前で光る。
		// 逆にすると煙が炎を覆い隠してしまう。
		//
		// 同じ重ね方どうしは、アセットの SortOrder が小さい順に描く(後に描いたものが上に乗る)。
		// プールの一覧は unordered_map なので、並べ替えないとアセットを足しただけで前後が入れ替わる。
		// SortOrder が同じなら名前順にして、毎回同じ並びになるようにしておく。
		// (粒どうしの前後までは並べ替えていない)
		//----------------------------------------------------------
		const Particle::EParticleBlendMode _drawOrder[] =
		{
			Particle::EParticleBlendMode::AlphaBlend,
			Particle::EParticleBlendMode::Additive,
		};

		for (const auto _mode : _drawOrder)
		{
			const Handle<ID3D12PipelineState>& _psoHandle = (_mode == Particle::EParticleBlendMode::Additive)
				? m_additivePSO
				: m_alphaBlendPSO;

			if (!_psoHandle.IsValid()) continue;

			// この重ね方で描くプールを集める
			struct DrawEntry
			{
				Handle<Resource::ParticlesAsset> handle = {};
				const Particle::GPUParticlePool* pPool = nullptr;
				const Resource::ParticlesAsset* pParticle = nullptr;
			};
			std::vector<DrawEntry> _entries;
			_entries.reserve(_particleManager->GetPoolMap().size());

			for (auto& [_handle, _upPool] : _particleManager->GetPoolMap())
			{
				if (!_upPool) continue;

				// プールが読み込み済みかチェック
				if (!_particleManager->IsReady(_handle)) continue;

				// 眠っているプール(最後に出してから最大寿命が経った)には生きている粒が無い。
				// 描画は容量ぶん走るので、描かずに飛ばす
				if (!_particleManager->IsAwake(_handle)) continue;

				// このフレームの間接描画の引数が用意されているものだけ描く。
				// 用意していない(シミュレーションが回らなかった)プールの引数は COMMON のままで、
				// 中身も前のフレームのもの。それを ExecuteIndirect で読むと状態が食い違う
				if (!_upPool->IsArgsReady()) continue;

				auto* _pParticle = _resManager.Get(_handle);
				if (!_pParticle) continue;
				if (_pParticle->GetBlendMode() != _mode) continue;

				_entries.push_back({ _handle, _upPool.get(), _pParticle });
			}

			// SortOrder の小さい順(同じなら名前順、それも同じならハンドル順)
			std::stable_sort(_entries.begin(), _entries.end(),
				[](const DrawEntry& a_l, const DrawEntry& a_r)
				{
					if (a_l.pParticle->GetSortOrder() != a_r.pParticle->GetSortOrder())
					{
						return a_l.pParticle->GetSortOrder() < a_r.pParticle->GetSortOrder();
					}
					if (a_l.pParticle->GetName() != a_r.pParticle->GetName())
					{
						return a_l.pParticle->GetName() < a_r.pParticle->GetName();
					}
					return a_l.handle.id < a_r.handle.id;
				});

			for (const auto& _entry : _entries)
			{
				_drawPool(_entry.handle, _entry.pPool, *_entry.pParticle, _psoHandle);
			}
		}
	}



	void ParticlePass::Archive(Engine::Persistence::Archive& a_arch)
	{
		(void)a_arch;
	}
}
