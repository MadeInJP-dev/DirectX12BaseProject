// ルートパラメーターの構造体
#include "../../../Common/RootParameters/CameraData.hlsli"
#include "../../../Common/RootParameters/LightData.hlsli"
#include "../../../Common/RootParameters/RaytracingData.hlsli"
#include "../../../Common/RootParameters/Vertex.hlsli"

// ヘルパー関数
#include "../Raytracing.hlsli"
#include "../../../Common/Math/CalcNormal.hlsli"

//==========================================================================================
// グローバルルートパラメーター
//
//   0 : CBV(b0)         カメラ
//   1 : SRV(t0)         TLAS
//   2 : UAVテーブル(u0) GI出力(ハーフ解像度)
//   3 : SRVテーブル(t1) インスタンス
//   4 : SRVテーブル(t2) マテリアル
//   5 : CBV(b1)         GBufferのSRVインデックス
//   6 : CBV(b10)        環境光
//   7 : SRVテーブル(t3) 頂点
//   8 : SRVテーブル(t4) インデックス
//   9 : SRVテーブル(t5) スキニング済み頂点
//
// 実体は C++ 側(RaytracingGIPass)で組む。並びを変えるときは両方を揃えること
//==========================================================================================
cbuffer CBCamera : register(b0)
{
	CameraData g_camera;
}

cbuffer CBGBufferIndex : register(b1)
{
	RayGIGBufferIndex g_gbuffer;
}

cbuffer CBSunLight : register(b10)
{
	SunLightData g_sun;
}

RaytracingAccelerationStructure	g_raytracingWorld	: register(t0);
StructuredBuffer<RayInstanceData>	g_instanceData		: register(t1);
StructuredBuffer<RayMaterial>		g_materialData		: register(t2);
StructuredBuffer<MeshVertex>		g_vertexfloatData	: register(t3);	// 静的な頂点(詰めた形)
StructuredBuffer<uint>				g_indexData			: register(t4);
StructuredBuffer<AnimatedVertex>	g_animatedVertexData: register(t5);	// スキニング後の座標・法線・接線

RWTexture2D<float4>	gOutPut	: register(u0);
sampler				gSamp	: register(s0);
