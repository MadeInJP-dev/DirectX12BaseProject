#include "Boundary.hlsli"

// 板の4隅(三角形2枚 = 6頂点)。原点を中心にした 1x1、XY 平面で表は +Z
static const float2 QUAD_CORNERS[6] =
{
	float2(-0.5f,  0.5f), float2( 0.5f,  0.5f), float2(-0.5f, -0.5f),
	float2(-0.5f, -0.5f), float2( 0.5f,  0.5f), float2( 0.5f, -0.5f),
};

// ルートシグネチャ定義
[RootSignature(BOUNDARY_ROOT_SIG)]

VSOutput VSMain(VSInput a_input)
{
	const float2 _corner = QUAD_CORNERS[a_input.vertexID % 6];

	VSOutput _out = (VSOutput)0;

	const float4 _worldPos = Transform_LocalToWorld(float3(_corner, 0.0f), g_boundary.worldMat);
	_out.svPos = mul(_worldPos, g_camera.viewProj);
	_out.worldPos = _worldPos.xyz;

	// 左上が (0,0)。v は下向き
	_out.uv = float2(_corner.x + 0.5f, 0.5f - _corner.y);

	return _out;
}
