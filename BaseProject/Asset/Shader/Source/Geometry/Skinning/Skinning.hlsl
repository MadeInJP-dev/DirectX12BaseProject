#include "Skinning.hlsli"

[RootSignature(SKINNING_ROOT_SIG)]

[numthreads(64, 1, 1)]
void CSMain( uint3 DTid : SV_DispatchThreadID )
{
	// 範囲外アクセスチェック : 頂点数を超えたスレッドは終了
	if (DTid.x >= g_info.vertexCount) return;
	
	// メガバッファ上の絶対インデックス
	uint _globalVertIdx = g_info.vertexStart + DTid.x;

	// 頂点データの取得(詰めた形から戻す)
	Vertex _vert = DecodeMeshVertex(g_vertexfloatData[_globalVertIdx]);

	// ---- スキニング計算 ----
	// ボーン割り当てを持たない(スキンメッシュでない)ものは変形させず、元の形のまま書き出す
	row_major float4x4 _mBones = float4x4(
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1);
	if (g_info.skinVertexStart != NO_SKIN_VERTEX)
	{
		SkinVertex _skin = g_skinVertexData[g_info.skinVertexStart + DTid.x];

		_mBones = 0;
		[unroll]
		for (uint _i = 0; _i < 4; ++_i)
		{
			_mBones += g_bonePalletData[g_info.boneOffset + DecodeBoneIndex(_skin, _i)].mat * DecodeBoneWeight(_skin, _i);
		}
	}

	// 座標と法線
	float4 _skinnedPos = mul(float4(_vert.pos, 1), _mBones);
	float3 _skinnedNormal = mul(_vert.normal, (float3x3) _mBones);
	float3 _skinnedTangent = mul(_vert.tangent, (float3x3) _mBones);
	
	// 新たな頂点を入れる : 変わるのは座標・法線・接線だけ(uv は静的な頂点から読む)
	AnimatedVertex _outVert;
	_outVert.pos = _skinnedPos.xyz;
	_outVert.normal = EncodeOctNormal(_skinnedNormal);
	_outVert.tangent = EncodeOctNormal(_skinnedTangent);
	
	// 上書きする前の位置(=前フレームの結果)をモーションベクター用に残す。
	// 同じ頂点を触るのはこのスレッドだけなので、読んでから書くまでに他から壊されない
	uint _animatedVertIdx = g_info.animatedVertStart + DTid.x;
	g_outputPrevPosition[_animatedVertIdx] = g_outputVertex[_animatedVertIdx].pos;

	g_outputVertex[_animatedVertIdx] = _outVert;
}
