//==========================================================================================
//
// DepthDitherFadePS
//
// 深度だけを書くパス(ZPrePass)で、フェード中の物(MeshInstanceData.fade < 1)だけに張る
// ピクセルシェーダー。GBuffer(MeshGBufferPS)と同じディザーで画素を捨てる。
//
// ふだんの深度だけのパスはピクセルシェーダーを持たない。フェード中の物だけ
// DitherFade の PSO になり(DrawSubmitter)、そのときだけこれが張られる
// (ShadingPipelineBuilder::RegisterDitherFadePixelShader)。
//
// 頂点は DepthOnlyMS が出す(位置とインスタンス番号)。
//
//==========================================================================================
#include "MeshCommon.hlsli"
#include "../../../Common/Math/DitherFade.hlsli"

// DepthOnlyMS の出力と並びを合わせる
struct DepthFadeInput
{
	float4 pos : SV_Position;
	uint instanceID : INSTANCE_ID;
};

// ルートシグネチャ定義
[RootSignature(MESHGLOBAL_ROOT_SIG)]
void PSMain(DepthFadeInput a_input)
{
	const MeshInstanceData _inst = g_instanceData[a_input.instanceID];
	if (IsDitherFadeDiscard(_inst.fade, a_input.pos.xy))
	{
		discard;
	}
}
