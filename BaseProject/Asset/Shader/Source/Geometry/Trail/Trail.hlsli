// ルートパラメーターの構造体
#include "../../../Common/RootParameters/CameraData.hlsli"

#include "../../../Common/RootSignatureLayout.hlsli"

//==========================================================================================
// ルートパラメーター
//
//    0 : CBV(b0)            カメラ
//    1 : SRV(t0)            全トレイルの頂点(TrailVertex)
//    2 : RootConstants(b1)  帯1本ぶん : 頂点の先頭・絵の番号・加算か
//
// 頂点バッファは持たない。VS が SV_VertexID で頂点の配列を引く(三角形ストリップ)
// ※ CPU 側の番号(TransparentPass.cpp)と合わせること
//==========================================================================================
#define TRAIL_ROOT_SIG RS_FLAGS",""CBV(b0, visibility = SHADER_VISIBILITY_ALL),""SRV(t0, visibility = SHADER_VISIBILITY_ALL),""RootConstants(num32BitConstants=3, b1),"RS_STATIC_SAMPLER_CLAMP

cbuffer CBCamera : register(b0)
{
	CameraData g_camera;
}

// 帯1本ぶんの値
cbuffer TrailConstants : register(b1)
{
	uint g_vertexStart;		// 頂点の配列の何番目からがこの帯か
	uint g_textureIndex;	// 帯に貼る絵の SRV 番号。0xFFFFFFFF なら絵なし
	uint g_isAdditive;		// 1 なら加算で重ねる
};

// トレイルの頂点
// ※ CPU 側 Engine::Graphics::TrailVertex と並びを合わせること(詰めて 36 バイト)
struct TrailVertex
{
	float3 pos;		// ワールド座標
	float2 uv;		// U = 先頭 0 → 尾 1、V = 帯の端 0 → 端 1
	float4 color;	// 強さ込みの色(a は濃さ)
};

StructuredBuffer<TrailVertex> g_trailVertices : register(t0);

SamplerState g_samp : register(s0);

struct VSOutput
{
	float4 svPos : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
};
