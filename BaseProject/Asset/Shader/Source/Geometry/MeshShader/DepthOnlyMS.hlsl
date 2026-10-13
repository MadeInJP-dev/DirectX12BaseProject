//==========================================================================================
//
// DepthOnlyMS
//
// 深度だけを書くパス(ZPrePass / ShadowMapPass / GroundDepthPass)のメッシュシェーダー。
// ピクセルシェーダーを持たないパスなので、頂点はクリップ座標だけを出す。
//
// UberMS は GBuffer 用に、法線・接線・UV・ワールド座標・モーションベクター用の
// 今と前のクリップ座標まで 1 頂点あたり約 24 float を書き出す。深度だけのパスでは
// どれも捨てられるので、ここでは 4 float(SV_Position)だけにして、
// 頂点の読み出し・出力の書き込みを減らす。
//
// ・増幅シェーダー(TestAS)、ルートシグネチャ、ペイロードは UberMS と共通
// ・座標の変換は TransformMeshPosition(MeshCommon.hlsli)を UberMS と同じく通す。
//   GBuffer は ZPre の深度と EQUAL で比べるので、深度がずれると面が抜ける
//
//==========================================================================================
#include "MeshCommon.hlsli"

// 深度だけのパスの頂点出力
struct DepthVertexOutput
{
	float4 pos : SV_Position;
};

uint3 UnpackPrimitive(uint a_primitive)
{
	return uint3(a_primitive & 0x3FF, (a_primitive >> 10) & 0x3FF, (a_primitive >> 20) & 0x3FF);
}

// ルートシグネチャ定義
[RootSignature(MESHGLOBAL_ROOT_SIG)]

// 出力するトポロジーを指定
[outputtopology("triangle")]
// スレッドグループのサイズ(UberMS と同じ)
[numthreads(128, 1, 1)]
void MSMain(
	uint a_gtid : SV_GroupThreadID,			// スレッドグループ内のローカルID（0 ～ 127）
	uint3 a_gid : SV_GroupID,				// ASから起動されたグループのインデックス
	in payload PayloadStruct a_meshPayload,
	out indices uint3 primIndices[126],
	out vertices DepthVertexOutput outVerts[64]
)
{
	// ---------------------------------------------------------
	// インスタンスとメッシュレットの情報取得
	// ---------------------------------------------------------
	uint _instanceID = a_meshPayload.instanceID;					// インスタンスIDはAS側で求めたものを使用
	MeshInstanceData _inst = g_instanceData[_instanceID];

	uint _globalMeshletIndex = a_meshPayload.MeshletIndices[a_gid.x];
	Meshlet _m = g_meshletData[_globalMeshletIndex];

	// MSの出力を宣言
	SetMeshOutputCounts(_m.vertexCount, _m.primitiveCount);

	// ---------------------------------------------------------
	// 頂点の処理 : 座標だけを読んで変換する
	// ---------------------------------------------------------
	if (a_gtid < _m.vertexCount)
	{
		// ユニーク頂点インデックスの取得。
		// 位置はペイロード(増幅シェーダーが選んだ LOD の段)から引く
		uint _globalUVI = a_meshPayload.uviOffset + _m.vertexOffset + a_gtid;
		uint _localVertexIndex = g_uniqueVertexIndices[_globalUVI];

		// アニメメッシュはスキニング後の座標、静的なメッシュは静的な頂点の座標
		float3 _localPos;
		if (_inst.isAnimated == 0)
		{
			_localPos = g_vertices[a_meshPayload.vertexOffset + _localVertexIndex].pos;
		}
		else
		{
			_localPos = g_animatedVertices[_inst.animatedVertexStart + _localVertexIndex].pos;
		}

		float3 _worldPos;
		DepthVertexOutput _vout;
		_vout.pos = TransformMeshPosition(_localPos, _inst.worldMat, _worldPos);

		outVerts[a_gtid] = _vout;
	}

	// ---------------------------------------------------------
	// プリミティブの処理 : スレッド 0 ～ 125 が並行して担当
	// ---------------------------------------------------------
	if (a_gtid < _m.primitiveCount)
	{
		uint _globalPrimIndex = a_meshPayload.primitiveOffset + _m.primitiveOffset + a_gtid;
		primIndices[a_gtid] = UnpackPrimitive(g_primitiveIndices[_globalPrimIndex]);
	}
}
