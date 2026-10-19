#include "Engine/Graphics/FrameCompute/ParticleSimulation/ParticleSimulation.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/MainEngine.h"
#include "Engine/Graphics/Particle/ParticleBufferManager.h"
#include "Engine/Graphics/Particle/GPU/GPUParticlePool/GPUParticlePool.h"
#include "Engine/Graphics/Particle/GPU/EmitterSlotPool/EmitterSlotPool.h"
#include "Engine/Graphics/Particle/GPU/ParticleDrawTable/ParticleDrawTable.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"

#include "Engine/Resource/Data/Shader/IO/ShaderIO.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/QuadPolygon/QuadPolygon.h"

namespace
{
	//======================================================================================
	// 描画設定の表の1件をアセットから作る
	//======================================================================================
	Engine::Graphics::Particle::ParticleDrawRecord MakeDrawRecord(
		const Engine::Graphics::Particle::GPUParticlePool& a_pool,
		const Engine::Resource::ParticlesAsset& a_particle,
		UINT a_texIndex)
	{
		Engine::Graphics::Particle::ParticleDrawRecord _record = {};
		_record.particleBufferIndex = a_pool.GetParticlePoolSRV().GetIndex();
		_record.texIndex     = a_texIndex;
		_record.orientation  = static_cast<uint32_t>(a_particle.GetOrientation());
		_record.stretch      = a_particle.GetStretch();
		_record.endSizeScale = a_particle.GetEndSizeScale();
		_record.fadeInRatio  = a_particle.GetFadeInRatio();
		_record.fadeOutRatio = a_particle.GetFadeOutRatio();
		_record.softDepth    = a_particle.GetSoftDepth();
		_record.startColor   = a_particle.GetStartColor();
		_record.endColor     = a_particle.GetEndColor();
		return _record;
	}
}

namespace Engine::Graphics
{
	void ParticleSimulation::Setup(PipelineStateManager* a_pPSOManager, Resource::ResourceManager& a_resourceManager)
	{
		if (!a_pPSOManager) return;
		m_pPSOManager = a_pPSOManager;
		m_pResourceManager = &a_resourceManager;

		SetupComputeShader(
			"Asset/Shader/Source/Particle/Emit/EmitParticleShaeder.cso",
			"EmitParticleShader",
			m_emitRootSig, m_emitPSO);

		SetupComputeShader(
			"Asset/Shader/Source/Particle/Update/UpdateParticleShader.cso",
			"UpdateParticleShader",
			m_updateRootSig, m_updatePSO);

		SetupComputeShader(
			"Asset/Shader/Source/Particle/Update/ResetDrawArgs.cso",
			"ResetDrawArgsShader",
			m_resetRootSig, m_resetPSO);

		SetupComputeShader(
			"Asset/Shader/Source/Particle/Update/GrowParticlePool.cso",
			"GrowParticlePoolShader",
			m_growRootSig, m_growPSO);
	}

	bool ParticleSimulation::SetupComputeShader(
		const std::string& a_csPath,
		const std::string& a_psoName,
		Handle<ID3D12RootSignature>& a_outRootSig,
		Handle<ID3D12PipelineState>& a_outPSO)
	{
		auto _csHandle = Resource::ShaderIO::Load(*m_pResourceManager, a_csPath);
		auto* _pShader = m_pResourceManager->Ref(_csHandle);
		if (!_pShader || !_pShader->Get()) return false;

		a_outRootSig = m_pPSOManager->Request(_pShader->Get());
		if (!a_outRootSig.IsValid()) return false;

		D3D12::ComputePipelineDesc _desc = {};
		_desc.SetName(a_psoName);
		_desc.desc.CS.pShaderBytecode = _pShader->Get()->GetBufferPointer();
		_desc.desc.CS.BytecodeLength = _pShader->Get()->GetBufferSize();
		_desc.SetRootSignature(m_pPSOManager->GetRootSignature(a_outRootSig));

		a_outPSO = m_pPSOManager->RequestHandle(_desc);
		return a_outPSO.IsValid();
	}

	void ParticleSimulation::Execute(GraphicsEngine* a_pGE, RenderContext* a_pCtx)
	{
		if (!a_pGE || !a_pCtx) return;
		if (!m_pPSOManager) return;

		auto* _pCmd = a_pCtx->RefCurrentCmdList();
		if (!_pCmd) return;

		auto* _pParticleManager = a_pGE->RefParticleManager();
		if (!_pParticleManager) return;

		//----------------------------------------------------------------------------------
		// 容量を伸ばす(発生より前)
		//
		// UploadEmitData が「足りない」と見たプールだけ。
		// 作り直して古い中身を写し(BeginGrowPool)、増えた範囲を CS で使えるようにする :
		//   mode 0 : 増えた粒を 0 で埋め、その番号をデッドリストの上へ積む(カウンターは読むだけ)
		//   mode 1 : カウンターを増えたぶん足す(1スレッド)
		// 2つの間は UAV バリアで区切る(読み終わる前に足すと積む位置がずれる)。
		// 伸ばす CS が読めていなければ伸ばさない(出す数は空きが無いぶん減るだけで、壊れはしない)
		//----------------------------------------------------------------------------------
		const bool _isGrowReady = m_growRootSig.IsValid() && m_growPSO.IsValid();
		if (_isGrowReady)
		{
			for (auto& [_handle, _pool] : _pParticleManager->GetPoolMap())
			{
				if (!_pool) continue;
				if (!_pParticleManager->HasGrowTarget(_handle)) continue;
				if (!_pParticleManager->BeginGrowPool(_handle, _pCmd)) continue;

				const UINT _oldCapacity = _pool->GetGrowFromCapacity();
				const UINT _addCount = _pool->GetMaxCapacity() - _oldCapacity;

				a_pCtx->BindBindlessHeaps();
				a_pCtx->SetComputeRootSignature(m_growRootSig);
				a_pCtx->SetComputePSO(m_growPSO);

				// ※ HLSL 側 GrowParam(GrowParticlePool.hlsl)と並びを合わせること
				UINT _growParam[] = {
					_pool->GetParticlePoolUAV().GetIndex(),
					_pool->GetDeadListUAV().GetIndex(),
					_pool->GetCounterUAV().GetIndex(),
					_oldCapacity,
					_addCount,
					0u,		// mode 0 : 埋めて積む
				};
				a_pCtx->ComputeBindDescriptorIndices(0, _growParam);

				// シェーダーの numthreads(64) と合わせる
				constexpr UINT GROW_THREAD_GROUP_SIZE = 64u;
				a_pCtx->Dispatch((_addCount + GROW_THREAD_GROUP_SIZE - 1u) / GROW_THREAD_GROUP_SIZE, 1, 1);

				D3D12::UAVBarrier(
					_pCmd,
					{
						_pool->GetParticlePoolResource(),
						_pool->GetDeadListResource(),
						_pool->GetCounterResource()
					}
				);

				_growParam[5] = 1u;		// mode 1 : カウンターを足す
				a_pCtx->ComputeBindDescriptorIndices(0, _growParam);
				a_pCtx->Dispatch(1, 1, 1);

				// COMMON へ戻す(遷移が CS の書き込みの完了待ちも兼ねる)。
				// この下の発生と更新は今まで通り COMMON から使う
				_pool->EndGrow(_pCmd);
			}
		}

		//----------------------------------------------------------------------------------
		// 発生
		//----------------------------------------------------------------------------------
		// このフレームの乱数の種を進める
		++m_frameCounter;

		//----------------------------------------------------------------------------------
		// 発生命令は全プール共通の1本(UploadEmitData がつなげて送ったもの)。
		// プールごとに「どこからどこまでか」を引いて、そこだけを読ませる。
		// まだ一度も送っていなければ、出す命令はどこにも無い
		//----------------------------------------------------------------------------------
		const auto* _pEmitBuff = _pParticleManager->GetEmitterBuffer();
		const UINT _emitIndex = _pEmitBuff ? static_cast<UINT>(_pEmitBuff->GetSRVHandle().GetIndex()) : 0xFFFFFFFFu;

		for (auto& [_handle, _pool] : _pParticleManager->GetPoolMap())
		{
			if (!_pool || !_pEmitBuff) continue;
			// プールが読み込み済みかチェック
			if (!_pParticleManager->IsReady(_handle)) continue;

			// このフレームに出す粒が無いなら何もしない
			const auto _range = _pParticleManager->GetEmitRange(_handle);
			if (_range.count == 0 || _range.emitTotal == 0) continue;

			// ヒープとルートシグネチャ、PSOをセット
			a_pCtx->BindBindlessHeaps();
			a_pCtx->SetComputeRootSignature(m_emitRootSig);
			a_pCtx->SetComputePSO(m_pPSOManager->GetPSO(m_emitPSO));

			// 命令バインド(バインドレス : 番号をルート定数で渡す)
			a_pCtx->ComputeBindDescriptorIndices(1, std::span<const UINT>(&_emitIndex, 1));

			// ※ HLSL 側 ParticleEmitSetting(Common/RootParameters/Particle.hlsli)と並びを合わせること
			struct EmitCB
			{
				uint32_t requestOffset;	// 共通の1本の中で、このプールの命令が始まる位置
				uint32_t requestCount;	// このプールの命令の数
				uint32_t emitTotal;		// 今回出す粒の合計(= スレッド数。容量で頭打ち)
				uint32_t frameSeed;		// フレームごとに変わる乱数の種
			};
			EmitCB _cbEmit = {};

			// 範囲は UploadEmitData が実際に送ったぶんだけなので、そのまま渡してよい
			_cbEmit.requestOffset = _range.offset;
			_cbEmit.requestCount = _range.count;
			_cbEmit.emitTotal = _range.emitTotal;

			// プールごとにも種をずらす(同一フレームに複数プールが出しても被らないように)
			_cbEmit.frameSeed = m_frameCounter * 2654435761u + _handle.id;
			a_pCtx->BindCB()->BindAndAttachDataComputeRootCBV<EmitCB>(_pCmd, 0, _cbEmit);

			// GPUパーティクルプールバインド : 本体 / デッドリスト / カウンターの順(シェーダーの u0-u2 と同じ)
			const UINT _poolIndices[] = {
				_pool->GetParticlePoolUAV().GetIndex(),
				_pool->GetDeadListUAV().GetIndex(),
				_pool->GetCounterUAV().GetIndex(),
			};
			a_pCtx->ComputeBindDescriptorIndices(2, _poolIndices);

			// 実行
			// 1スレッド = 1粒。シェーダーの numthreads(64) と合わせる。
			// 大きなバースト(1命令で数百粒)を1スレッドで順番に作らずに済む
			constexpr UINT EMIT_THREAD_GROUP_SIZE = 64u;
			const UINT _dispatchNum = (_cbEmit.emitTotal + EMIT_THREAD_GROUP_SIZE - 1u) / EMIT_THREAD_GROUP_SIZE;
			a_pCtx->Dispatch(_dispatchNum, 1, 1);

			// ★UAVバリア必須。
			// この下の更新は同じ deadList / counter を触る。
			// バリアが無いと2つのDispatchがGPU上で並列に走り、
			// 更新側の「返却(counter++ してから deadList[count] へ書く)」と
			// 発生側の「取り出し(counter-- してから deadList[count-1] を読む)」が
			// 交錯する。書き込み前のスロットを読んでしまうと、
			// ・使用中インデックスを二重に掴む(生きている粒が上書きされて消える)
			// ・返却したインデックスがカウンターの上に取り残されて二度と拾われない
			// が起き、空きスロットが少しずつ減り続けてエミット数が先細りする
			D3D12::UAVBarrier(
				_pCmd,
				{
					_pool->GetParticlePoolResource(),
					_pool->GetDeadListResource(),
					_pool->GetCounterResource()
				}
			);
		}

		//----------------------------------------------------------------------------------
		// 更新
		//
		// 発生源の席は全プール共通の1本。まだ一度も転送されていなければ
		// 引けない番号で読ませないよう、更新ごと見送る
		// (席 0 は初期化で取っているので、最初のフレームの転送で必ずできる)
		//----------------------------------------------------------------------------------
		const auto* _pSlotPool = _pParticleManager->GetEmitterSlotPool();
		if (!_pSlotPool) return;
		const UINT _slotSRVIndex = _pSlotPool->GetSRVIndex();
		if (_slotSRVIndex == (std::numeric_limits<UINT>::max)()) return;

		// 間接描画の引数をリセットできるか。
		// 更新シェーダーは生き残った粒を数えて引数へ足すので、リセットできないと
		// 前のフレームの数に足し続けることになる。リセット用シェーダーが読めていないときは
		// 更新ごと見送る(印が立たないので描画もされない)
		const bool _isResetReady = m_resetRootSig.IsValid() && m_resetPSO.IsValid();
		if (!_isResetReady) return;

		// 描く板ポリのインデックス数。ParticlePass が張るもの(平らな1枚板)と合わせる
		const UINT _quadIndexCount = a_pGE->RefQuadPolygon() ? a_pGE->RefQuadPolygon()->GetIndexCount() : 0u;

		// 更新するプールか。
		// 眠っているプール(最後に出してから最大寿命が経った)には生きている粒が無い。
		// 更新は容量ぶん走るので、回さずに飛ばす。
		// 引数バッファと生存リストが無ければ、更新シェーダーの書き先が無い
		auto _isUpdatable = [_pParticleManager](const Handle<Resource::ParticlesAsset>& a_handle, const auto& a_upPool)
			{
				if (!a_upPool) return false;
				if (!_pParticleManager->IsReady(a_handle)) return false;
				if (!_pParticleManager->IsAwake(a_handle)) return false;
				return a_upPool->GetDrawArgsResource() && a_upPool->RefAliveList().GetResource();
			};

		//----------------------------------------------------------------------------------
		// 描画設定の表を組む
		//
		// 更新するプールのうち、アセットと絵が引けるもの(描けるもの)を全部積む。
		// ParticlePass はアセットごとに、この表の番号で設定を引いて描く
		//----------------------------------------------------------------------------------
		auto* _pDrawTable = _pParticleManager->RefDrawTable();
		if (!_pDrawTable || !m_pResourceManager) return;

		_pDrawTable->BeginBuild();
		for (auto& [_handle, _pool] : _pParticleManager->GetPoolMap())
		{
			if (!_isUpdatable(_handle, _pool)) continue;

			const auto* _pParticle = m_pResourceManager->Get(_handle);
			if (!_pParticle) continue;
			const auto* _pTex = m_pResourceManager->Get(_pParticle->GetTexHandle());
			if (!_pTex) continue;

			_pDrawTable->AddRecord(
				_handle,
				MakeDrawRecord(*_pool, *_pParticle, _pTex->GetSRV().GetIndex()),
				_pParticle->GetBlendMode() == Particle::EParticleBlendMode::AlphaBlend,
				_pool->GetMaxCapacity(),
				_pParticle->GetSortOrder(),
				_pParticle->GetName());
		}
		_pDrawTable->Commit(
			a_pGE->RefRenderDevice()->RefDevice(),
			a_pGE->RefDescriptorHeapManager(),
			_pCmd,
			a_pGE->RefRenderDevice()->GetCurrentFrameIndex());

		for (auto& [_handle, _pool] : _pParticleManager->GetPoolMap())
		{
			if (!_isUpdatable(_handle, _pool)) continue;

			// ヒープを先に張る。
			// ルートシグネチャが CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED のときは、
			// SetComputeRootSignature より前に SetDescriptorHeaps を済ませておく決まり
			// (このフレームに発生命令が無いと、上の発生ループで張られていない)
			a_pCtx->BindBindlessHeaps();

			//------------------------------------------------------------------
			// 間接描画の引数をリセットする
			//
			// フレームの頭は COMMON(前のフレームの終わりで FinishFrame が戻す)なので、書く前に UAV へ。
			// 生存リストも Update が書くので一緒に UAV へ。
			// インスタンス数は 0 から始め、Update が生き残った粒を数えて足す。
			// インデックス数は描く板ポリ(ParticlePass が張るもの)と合わせる
			//------------------------------------------------------------------
			_pool->RefDrawArgs().Barrier(_pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			_pool->RefAliveList().Barrier(_pCmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			a_pCtx->SetComputeRootSignature(m_resetRootSig);
			a_pCtx->SetComputePSO(m_resetPSO);
			const UINT _resetParam[] = {
				_pool->RefDrawArgs().GetUAV().GetIndex(),	// 引数バッファの UAV 番号
				_quadIndexCount,							// 板ポリのインデックス数(1枚板なら 6)
				0u,											// インスタンス数(Update が数えて足す)
			};
			a_pCtx->ComputeBindDescriptorIndices(0, _resetParam);
			a_pCtx->Dispatch(1, 1, 1);
			D3D12::UAVBarrier(_pCmd, { _pool->RefDrawArgs().GetResource() });

			// ルートシグネチャ、PSOをセット
			a_pCtx->SetComputeRootSignature(m_updateRootSig);
			a_pCtx->SetComputePSO(m_pPSOManager->GetPSO(m_updatePSO));

			// 更新設定バインド
			// ※ HLSL 側 UpdateCB(UpdateParticleShader.hlsl)と並びを合わせること
			struct UpdateCB
			{
				float deltaTime;
				Math::Vector3 gravity;

				float drag;
				Math::Vector3 pad;
			};
			// 固定値だと実フレームレートと寿命の減りが一致しない(重いほど長生きする)ため
			// 実際の経過時間を渡す
			UpdateCB _cbData = {};
			_cbData.deltaTime = MainEngine::Instance().GetDeltaTime();

			// 重力と減衰はアセット単位。プールごとに回しているのでここで引ける
			const auto* _pParticle = m_pResourceManager ? m_pResourceManager->Get(_handle) : nullptr;
			if (_pParticle)
			{
				// GravityPow は「重力をどれだけ受けるか」の倍率。
				// 1 で普通に落ち、0 で無重力、負にすると浮き上がる(煙向き)
				constexpr float GRAVITY = 9.81f;
				_cbData.gravity = { 0.0f, -GRAVITY * _pParticle->GetGravityPow(), 0.0f };

				_cbData.drag = _pParticle->GetDrag();
			}

			a_pCtx->BindCB()->BindAndAttachDataComputeRootCBV<UpdateCB>(_pCmd, 0, _cbData);

			// 命令と発生源の席のバインド。並びはシェーダーの PassDescriptorIndex0(命令 → 席)と同じ。
			// 更新シェーダーは発生命令を読まないが、ルートシグネチャの席は埋めておく(共通の1本の番号)。
			// 発生源の席はローカル空間の粒に重力を掛けるときに読む(席の回転で重力をローカルへ回す)
			{
				const UINT _updateIndices[] = {
					_emitIndex,
					_slotSRVIndex,
				};
				a_pCtx->ComputeBindDescriptorIndices(1, _updateIndices);
			}

			// GPUパーティクルプールバインド。並びはシェーダーの PassDescriptorIndex1 と同じ :
			// 本体 / デッドリスト / カウンター / 生存リスト / 間接描画の引数
			// (発生シェーダーは前の3つだけ。こちらは生き残りを積む先と数える先も渡す)
			const UINT _poolIndices[] = {
				_pool->GetParticlePoolUAV().GetIndex(),
				_pool->GetDeadListUAV().GetIndex(),
				_pool->GetCounterUAV().GetIndex(),
				_pool->GetAliveListUAV().GetIndex(),
				_pool->RefDrawArgs().GetUAV().GetIndex(),
			};
			a_pCtx->ComputeBindDescriptorIndices(2, _poolIndices);

			// 実行
			// 切り上げること。切り捨てると容量が32の倍数でない場合に
			// 末尾のパーティクルが一度も更新されず、寿命が減らないまま残り続ける。
			// (デッドリストは末尾から取り出すので、最初に発生した粒がまさにそこに入る)
			// シェーダー側は GetDimensions で範囲外スレッドを弾いているため多い分は安全
			const UINT _threadNum = _pool->GetMaxCapacity();
			const UINT _dispatchNum = (_threadNum + 31u) / 32u;
			a_pCtx->Dispatch(_dispatchNum, 1, 1);

			// ★UAVバリア必須。
			// 次フレームの発生が同じ deadList / counter から取り出す。
			// D3D12 は同一キューでもバリアが無ければ Dispatch 同士が重なって走れるため、
			// (コマンドリストをまたいでも)ここで区切らないと
			// このフレームの返却が終わる前に次の取り出しが走り、
			// 空きスロットが取りこぼされて減り続ける
			D3D12::UAVBarrier(
				_pCmd,
				{
					_pool->GetParticlePoolResource(),
					_pool->GetDeadListResource(),
					_pool->GetCounterResource()
				}
			);

			// 描画で読むので、引数は間接引数の状態へ、生存リストは VS が SRV で読む状態へ。
			// 遷移のバリアが Update の書き込みの完了待ちも兼ねる。
			// 印を立てたプールだけが描かれ、フレームの終わりで COMMON へ戻される
			_pool->RefDrawArgs().Barrier(_pCmd, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			_pool->RefAliveList().Barrier(_pCmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			_pool->SetArgsReady(true);
		}
	}
}
