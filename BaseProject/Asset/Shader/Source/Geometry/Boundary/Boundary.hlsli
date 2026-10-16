// ルートパラメーターの構造体
#include "../../../Common/RootParameters/CameraData.hlsli"
#include "../../../Common/RootParameters/BoundaryData.hlsli"

#include "../../../Common/RootSignatureLayout.hlsli"

//==========================================================================================
// ルートパラメーター
//
//    0 : CBV(b0)            カメラ
//    1 : CBV(b1)            面1枚ぶんの情報(画像の番号もここに入っている)
//
// 頂点バッファは持たない。板ポリは VS が SV_VertexID から組み立てる(6頂点)
// ※ CPU 側の番号(BoundaryPass.cpp)と合わせること
//==========================================================================================
#define BOUNDARY_ROOT_SIG \
RS_FLAGS","\
"CBV(b0, visibility = SHADER_VISIBILITY_ALL),"\
"CBV(b1, visibility = SHADER_VISIBILITY_ALL),"\
RS_STATIC_SAMPLER

cbuffer CBCamera : register(b0)
{
	CameraData g_camera;
}

cbuffer CBBoundary : register(b1)
{
	BoundaryData g_boundary;
}

// 面に貼る画像(番号は面の情報に入っている)
Texture2D Get_mainTex() { Texture2D _r = ResourceDescriptorHeap[g_boundary.texIndex]; return _r; }
#define g_mainTex Get_mainTex()

SamplerState g_samp : register(s0);

// ヘルパー関数 : 上で宣言した g_camera を使うので、必ずこの位置で読むこと
#include "../../../Common/Math/Transform.hlsli"

// 頂点シェーダー入力
struct VSInput
{
	uint vertexID : SV_VertexID;
};

struct VSOutput
{
	float4 svPos : SV_Position;
	float3 worldPos : TEXCOORD0;	// 基準点との距離を測る
	float2 uv : TEXCOORD1;			// 面の上の位置(0〜1)
};
