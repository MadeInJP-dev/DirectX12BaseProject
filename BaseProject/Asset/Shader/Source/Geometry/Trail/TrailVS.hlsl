#include "Trail.hlsli"

// ルートシグネチャ定義
[RootSignature(TRAIL_ROOT_SIG)]

VSOutput VSMain(uint a_vertexID : SV_VertexID)
{
	// 頂点は CPU がカメラへ向けて組み終えているので、ここは写すだけ
	const TrailVertex _vertex = g_trailVertices[g_vertexStart + a_vertexID];

	VSOutput _out = (VSOutput)0;
	_out.svPos = mul(float4(_vertex.pos, 1.0f), g_camera.viewProj);
	_out.uv = _vertex.uv;
	_out.color = _vertex.color;
	return _out;
}
