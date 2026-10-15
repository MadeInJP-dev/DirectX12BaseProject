// ルートパラメーターの構造体
#include "../../../Common/RootParameters/CameraData.hlsli"
#include "../../../Common/RootParameters/Particle.hlsli"

#include "../../../Common/RootSignatureLayout.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)         カメラ
//   1 : SRVの番号     粒バッファ(更新パスの出力をそのまま読む) + 発生源の席 + 生存リスト
//   2 : SRVの番号(t1) 絵
//   3 : CBV(b1)         アセット単位の描画設定(PS もぼかす幅を読む)
//   4 : SRVの番号     深度(グラフが張る。奥の物との境目をぼかすのに使う)
//==========================================================================================
#define PARTICLE_ROOT_SIG \
RS_FLAGS","\
"CBV(b0, visibility = SHADER_VISIBILITY_ALL),"\
"RootConstants(num32BitConstants=3, b100, visibility = SHADER_VISIBILITY_VERTEX),"\
"RootConstants(num32BitConstants=1, b101, visibility = SHADER_VISIBILITY_PIXEL),"\
"CBV(b1, visibility = SHADER_VISIBILITY_ALL),"\
"RootConstants(num32BitConstants=1, b102, visibility = SHADER_VISIBILITY_PIXEL),"\
RS_STATIC_SAMPLER

cbuffer CBCamera : register(b0)
{
	CameraData g_camera;
}

cbuffer CBParticleDraw : register(b1)
{
	ParticleDrawData g_draw;
}

// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_particleBufferIndex;
	uint g_emitterSlotIndex;		// 発生源の席(全アセット共通の1本)
	uint g_aliveListIndex;			// 生きている粒の番号の一覧(SV_InstanceID で引く)
}

StructuredBuffer<ParticleData> Get_particleBuffer() { StructuredBuffer<ParticleData> _r = ResourceDescriptorHeap[g_particleBufferIndex]; return _r; }
#define g_particleBuffer Get_particleBuffer()
StructuredBuffer<EmitterTransform> Get_emitterSlots() { StructuredBuffer<EmitterTransform> _r = ResourceDescriptorHeap[g_emitterSlotIndex]; return _r; }
#define g_emitterSlots Get_emitterSlots()
StructuredBuffer<uint> Get_aliveList() { StructuredBuffer<uint> _r = ResourceDescriptorHeap[g_aliveListIndex]; return _r; }
#define g_aliveList Get_aliveList()
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex1 : register(b101)
{
	uint g_mainTexIndex;
}

Texture2D Get_mainTex() { Texture2D _r = ResourceDescriptorHeap[g_mainTexIndex]; return _r; }
#define g_mainTex Get_mainTex()

// 深度の SRV の番号(グラフが張る)。深度が繋がっていないパイプラインでは無効値が来る
cbuffer PassDescriptorIndex2 : register(b102)
{
	uint g_depthTexIndex;
}

#define PARTICLE_INVALID_DESCRIPTOR_INDEX 0xFFFFFFFFu

Texture2D<float4> Get_depthTex() { Texture2D<float4> _r = ResourceDescriptorHeap[g_depthTexIndex]; return _r; }
#define g_depthTex Get_depthTex()

SamplerState g_samp : register(s0);

// ヘルパー関数 : 上で宣言した g_camera を使うので、必ずこの位置で読むこと
#include "../../../Common/Math/Transform.hlsli"
#include "../../../Common/Math/Normal.hlsli"

// 頂点入力
struct VSInput
{
	float4 pos	: POSITION;			// 頂点座標
	float2 uv	: TEXCOORD0;		// UV座標
	uint instID : SV_InstanceID;	// インスタンス番号
};

// 頂点出力
struct VSOutput
{
	float4 pos : SV_Position;		// 射影行列

	float2 uv : TEXCOORD0;
	float4 color : TEXCOORD1;

	// 板のその点のビュー空間の奥行き(m)。奥の物との境目をぼかすのに使う
	float viewDepth : TEXCOORD2;
};
