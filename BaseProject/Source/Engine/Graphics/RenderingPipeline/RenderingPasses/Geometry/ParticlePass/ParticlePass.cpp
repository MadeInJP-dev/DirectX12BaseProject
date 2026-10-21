#include "Engine/Graphics/RenderingPipeline/RenderingPasses/Geometry/ParticlePass/ParticlePass.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"

#include "Engine/Graphics/Particle/ParticleBufferManager.h"
#include "Engine/Graphics/Particle/GPU/GPUParticlePool/GPUParticlePool.h"
#include "Engine/Graphics/Particle/GPU/EmitterSlotPool/EmitterSlotPool.h"
#include "Engine/Graphics/Particle/GPU/ParticleDrawTable/ParticleDrawTable.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Graphics/Frame/SceneView/CameraData.h"
#include "Engine/Graphics/D3D12/InputLayout.h"

namespace Engine::Graphics::Pipeline
{
	void ParticlePass::SetupSlots()
	{
		// 深度は読むだけ : 半透明なので書かず、手前のものには隠される。
		// 単体で置いた構成も作れるよう任意にしてある。
		// PS からも SRV で読む(奥の物との境目をぼかす)ので、番号をルートパラメータ 2 へ張らせる。
		// 繋がっていなければ無効値が届き、シェーダーはぼかしを飛ばす
		DeclareInput("Depth", EAccessType::DepthRead, EPassSlotType::Texture, false, 2);

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

		//----------------------------------------------------------
		// プリマルチプライドの合成 : 色 × 1 + 下地 × (1 - α)
		//
		// PS が色に α を掛けてから出す。重ね方は PS が出す α で変わる :
		//   半透明 (色 × α, α) : 下地を α のぶん隠して色を置く(煙・破片)
		//   加算   (色 × α, 0) : 下地を隠さず色を足す(炎・火花)
		// 1つの PSO で両方を描けるので、加算と半透明の粒を同じ一覧で遠い順に並べて描ける
		//----------------------------------------------------------
		SetupRasterShader(
			a_context, _vsPath, _psPath, D3D12::Input::PARTICLE_INPUT_LAYOUT,
			"ParticleDraw_Premultiplied",
			[&_setupCommon](D3D12::GraphicsPipelineDesc& a_pso)
			{
				a_pso.BlendEnable(true);
				a_pso.SrcBlend(D3D12_BLEND_ONE, 0);
				a_pso.DestBlend(D3D12_BLEND_INV_SRC_ALPHA, 0);
				a_pso.BlendOp(D3D12_BLEND_OP_ADD, 0);

				a_pso.SrcBlendAlpha(D3D12_BLEND_ONE, 0);
				a_pso.DestBlendAlpha(D3D12_BLEND_INV_SRC_ALPHA, 0);
				a_pso.BlendOpAlpha(D3D12_BLEND_OP_ADD, 0);

				_setupCommon(a_pso);
			},
			EPassHeapMode::Bindless,
			&m_particlePSO);
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

		//----------------------------------------------------------
		// 描画設定の表(全アセット共通の1本)
		//
		// 粒バッファ・絵・色などはアセットごとに違うので、VS はこの表を番号で引く。
		// シミュレーションがこのフレームに組んで送っていなければ、描くものが無い
		//----------------------------------------------------------
		const auto* _pDrawTable = _particleManager->GetDrawTable();
		if (!_pDrawTable || !_pDrawTable->IsRecordReady()) return;
		const UINT _recordSRVIndex = _pDrawTable->GetRecordSRVIndex();

		// 間接描画のコマンドシグネチャ(DrawIndexed 1件)。作れていなければ描きようがない
		ID3D12CommandSignature* _pDrawSignature = _pGE->GetDrawIndexedSignature();
		if (!_pDrawSignature) return;

		//----------------------------------------------------------
		// 描く一覧を1回描く
		//
		// a_listSRVIndex : 描く一覧(全アセットを並べた一覧。並べられなかったフレームはそのプールの生存リスト)
		// a_recordIndex  : 表の番号(並べた一覧は PARTICLE_DRAW_RECORD_FROM_LIST で、一覧の上位ビットから引く)
		// 引数(インスタンス数など)はシミュレーションが GPU 上に用意したものを読む
		// (INDIRECT_ARGUMENT 状態にして印を立ててある)。カメラが何台あっても同じ引数を読んで描くだけでよい
		//----------------------------------------------------------
		if (!m_particlePSO.IsValid()) return;

		auto _drawList = [&](
			UINT a_listSRVIndex,
			uint32_t a_recordIndex,
			ID3D12Resource* a_pArgs)
			{
				// ヒープとルートシグネチャはグラフが張ってあるので、PSOだけ選び直す
				_pGE->BindPSO(_pCtx, m_particlePSO);

				// カメラバインド
				CameraData _cbCam = _pGE->GetSceneView()->GetCameraData();
				_pCtx->GraphicsBindRootCBV(0, _cbCam);

				// 並びはシェーダーの PassDescriptorIndex0(表 → 席 → 描く一覧 → 表の番号)と同じ
				const UINT _vsIndices[] = {
					_recordSRVIndex,
					_slotSRVIndex,
					a_listSRVIndex,
					a_recordIndex,
				};
				_pCtx->GraphicsBindDescriptorIndices(1, _vsIndices);

				_pCtx->DrawPolygonIndirect(_pDrawSignature, a_pArgs);
			};

		//----------------------------------------------------------
		// アセットごとに描く(並べられなかったフレームだけ)
		//
		// 同じ重ね方どうしは、アセットの SortOrder が小さい順に描く(後に描いたものが上に乗る)。
		// プールの一覧は unordered_map なので、並べ替えないとアセットを足しただけで前後が入れ替わる。
		// SortOrder が同じなら名前順にして、毎回同じ並びになるようにしておく
		//----------------------------------------------------------
		auto _drawEachPool = [&](Particle::EParticleBlendMode a_mode)
			{
				struct DrawEntry
				{
					Handle<Resource::ParticlesAsset> handle = {};
					const Particle::GPUParticlePool* pPool = nullptr;
					const Resource::ParticlesAsset* pParticle = nullptr;
					uint32_t recordIndex = 0;
				};
				std::vector<DrawEntry> _entries;
				_entries.reserve(_particleManager->GetPoolMap().size());

				for (auto& [_handle, _upPool] : _particleManager->GetPoolMap())
				{
					if (!_upPool) continue;

					// このフレームの間接描画の引数が用意されているものだけ描く。
					// 用意していない(シミュレーションが回らなかった)プールの引数は COMMON のままで、
					// 中身も前のフレームのもの。それを ExecuteIndirect で読むと状態が食い違う。
					// 準備中・眠っているプールはシミュレーションが回さないので、ここで弾かれる
					if (!_upPool->IsArgsReady()) continue;

					// 表に載っていない(アセットか絵が引けなかった)ものは描けない
					const auto _recordIndex = _pDrawTable->FindRecordIndex(_handle);
					if (!_recordIndex) continue;

					auto* _pParticle = _resManager.Get(_handle);
					if (!_pParticle) continue;
					if (_pParticle->GetBlendMode() != a_mode) continue;

					_entries.push_back({ _handle, _upPool.get(), _pParticle, *_recordIndex });
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
					_drawList(_entry.pPool->GetAliveListSRV().GetIndex(), _entry.recordIndex, _entry.pPool->GetDrawArgsResource());
				}
			};

		//----------------------------------------------------------
		// 全部の粒を遠い順に1回で描く
		//
		// 粒は深度を書かないので、後から描いたものが上に乗る。奥から順に描かないと正しく重ならない。
		// シミュレーションが加算・半透明を問わず全アセットの粒を1本の一覧へ集め、
		// カメラから遠い順に並べてあるので、それを1回で描く
		// (アセットごとに描くと、違うアセットの粒どうしの前後が決められない)。
		// 加算も同じ一覧に入っているので、煙の奥にある炎は手前の煙に隠れ、煙の手前の炎は煙の上で光る。
		// 距離が同じ粒は SortOrder の小さいアセットが先(下)になる。
		//
		// 並べられなかったフレーム(並べ替えのシェーダーが読めていない)はアセットごとに、
		// 半透明 → 加算 の順で描く(煙を先に置いてから炎を足す。前後は合わないことがある)
		//----------------------------------------------------------
		if (_pDrawTable->IsArgsReady())
		{
			_drawList(_pDrawTable->GetDrawListSRV().GetIndex(), Particle::PARTICLE_DRAW_RECORD_FROM_LIST, _pDrawTable->GetDrawArgsResource());
		}
		else
		{
			_drawEachPool(Particle::EParticleBlendMode::AlphaBlend);
			_drawEachPool(Particle::EParticleBlendMode::Additive);
		}
	}



	void ParticlePass::Archive(Engine::Persistence::Archive& a_arch)
	{
		(void)a_arch;
	}
}
