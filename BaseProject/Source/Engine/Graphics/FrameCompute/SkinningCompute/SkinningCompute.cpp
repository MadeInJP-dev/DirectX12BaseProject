#include "Engine/Graphics/FrameCompute/SkinningCompute/SkinningCompute.h"

#include "Engine/Graphics/GraphicsEngine.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"


#include "Engine/Graphics/D3D12/CBAllocator/CBAllocator.h"

#include "Engine/Option/OptionManager.h"
#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshBufferAllocator.h"
#include "Engine/Resource/Data/Shader/IO/ShaderIO.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

void Engine::Graphics::SkinningCompute::Setup(PipelineStateManager* a_pPSOManager, Resource::ResourceManager& a_resourceManager)
{
	if (!a_pPSOManager) return;
	m_pPSOManager = a_pPSOManager;

	// シェーダーからルートシグネチャとコンピュートPSOを起こす
	auto _csHandle = Resource::ShaderIO::Load(a_resourceManager, "Asset/Shader/Source/Geometry/Skinning/Skinning.cso");
	auto* _pShader = a_resourceManager.Ref(_csHandle);
	if (!_pShader || !_pShader->Get()) return;

	m_rootSigHandle = a_pPSOManager->Request(_pShader->Get());

	D3D12::ComputePipelineDesc _desc = {};
	_desc.SetName("SkinningCS");
	_desc.desc.CS.pShaderBytecode = _pShader->Get()->GetBufferPointer();
	_desc.desc.CS.BytecodeLength = _pShader->Get()->GetBufferSize();
	_desc.SetRootSignature(a_pPSOManager->GetRootSignature(m_rootSigHandle));

	m_psoHandle = a_pPSOManager->RequestHandle(_desc);
}

// 中身は旧 SkinningPass の実行関数をそのまま移したもの
void Engine::Graphics::SkinningCompute::Execute(GraphicsEngine* a_pGE, RenderContext* a_pCtx) const
{
	if (!a_pGE || !a_pCtx) return;
	if (!m_pPSOManager) return;


	// このフレームに積まれたスキニング命令
	const auto& _skinningItems = a_pGE->GetDrawLists()->GetSkinningItems();
	{
			auto* _pCmdList = a_pCtx->RefCurrentCmdList();
			auto* _pPso = m_pPSOManager->GetPSO(m_psoHandle);

			auto* _pMA = a_pGE->RefMeshBufferAllocator();
			if (!_pMA) return;

			a_pCtx->BindBindlessHeaps();
			a_pCtx->SetComputeRootSignature(m_rootSigHandle);
			a_pCtx->SetComputePSO(_pPso);

			// =====================================================================
			// モーションベクター用の前フレームの位置は、スキニングのシェーダーが
			// 上書きする前の位置(=前フレームの結果)を prev へ書き写して残す。
			// 以前はスキニングの前に頂点まるごとをコピーしていたが、
			// 読まれるのは位置だけなので、prev は位置だけの小さなバッファにしてある。
			//
			// 同じ領域を 1 フレームに 2 回スキニングすると、2 回目は 1 回目の結果を
			// 「前フレーム」として写してしまう。命令はインスタンスのメッシュごとに 1 つで、
			// 領域も重ならない(ディスパッチ間に UAV バリアを張っていないのも同じ前提)
			// =====================================================================
			auto& _animatedBuf = _pMA->RefAnimatedVertexBuffer();
			auto& _prevBuf = _pMA->RefPrevAnimatedPositionBuffer();
			_animatedBuf.Barrier(_pCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			_prevBuf.Barrier(_pCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			// メッシュ情報バインド
			a_pCtx->ComputeBindBonePaletteBuffer(1);
			// バインドレス : 各バッファの番号をルート定数で渡す
			const UINT _vertexIndex   = _pMA->GetStaticVertexBuffer().GetSRV().GetIndex();
			const UINT _indexIndex    = _pMA->GetIndexBuffer().GetSRV().GetIndex();
			const UINT _animatedIndex = _animatedBuf.GetUAV().GetIndex();
			const UINT _prevIndex     = _prevBuf.GetUAV().GetIndex();
			const UINT _skinIndex     = _pMA->GetSkinVertexBuffer().GetSRV().GetIndex();
			a_pCtx->ComputeBindDescriptorIndices(2, std::span<const UINT>(&_vertexIndex, 1));
			a_pCtx->ComputeBindDescriptorIndices(3, std::span<const UINT>(&_indexIndex, 1));
			a_pCtx->ComputeBindDescriptorIndices(4, std::span<const UINT>(&_animatedIndex, 1));
			a_pCtx->ComputeBindDescriptorIndices(5, std::span<const UINT>(&_prevIndex, 1));
			a_pCtx->ComputeBindDescriptorIndices(6, std::span<const UINT>(&_skinIndex, 1));

			for (auto& _item : _skinningItems)
			{

				// SkinningInfo.hlsli と同じ並び
				struct Info
				{
					UINT vertexStart;			// 頂点のスタートインデックス
					UINT animatedVertStart;
					UINT vertexCount;			// キャラの頂点数
					UINT boneOffset;			// このキャラのボーンの開始場所
					UINT skinVertexStart;		// ボーン割り当ての開始位置(無ければ NO_SKIN_VERTEX)
				} _info;

				// ボーン割り当てを持たないメッシュ(スキンメッシュでないもの)は、
				// 変形させずに元の形のまま書き出させる
				constexpr UINT NO_SKIN_VERTEX = 0xFFFFFFFF;

				_info.vertexStart = _item.staticVertexHandle.startIndex;
				_info.skinVertexStart = _item.skinVertexHandle.IsValid() ? _item.skinVertexHandle.startIndex : NO_SKIN_VERTEX;
				_info.animatedVertStart = _item.animatedHandle.startIndex;
				_info.vertexCount = _item.staticVertexHandle.count;
				// プールの添字ではなくボーンパレット(GPU)上の位置。
				// ワールドごとの土台が足してあるので、シーンを重ねても他人のボーンを踏まない
				_info.boneOffset = _item.boneBufferStart;
				a_pCtx->ComputeBindRootCBV(0, _info);

				UINT _x = (_info.vertexCount + 63) / 64;
				a_pCtx->Dispatch(_x, 1, 1);
			}

			// prev は GBuffer/ZPre のメッシュシェーダが SRV として読むので遷移させておく
			_prevBuf.Barrier(_pCmdList,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
}
