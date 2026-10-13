#include "MeshCommon.hlsli"

uint3 UnpackPrimitive(uint a_primitive)
{
	return uint3(a_primitive & 0x3FF, (a_primitive >> 10) & 0x3FF, (a_primitive >> 20) & 0x3FF);
}

// ルートシグネチャ定義
[RootSignature(MESHGLOBAL_ROOT_SIG)]

// 出力するトポロジーを指定
[outputtopology("triangle")]
// スレッドグループのサイズ
[numthreads(128, 1, 1)]
void MSMain(
	uint a_gtid : SV_GroupThreadID,			// スレッドグループ内のローカルID（0 ～ 127）
	uint3 a_gid : SV_GroupID,				// ASから起動されたグループのインデックス
	in payload PayloadStruct a_meshPayload,
	out indices uint3 primIndices[126],
	out vertices VertexOutput outVerts[64]
)
{
	
	//uint _arbitraryData = a_meshPayload.SurvivingMeshlets;
	
	// ---------------------------------------------------------
	// インスタンスとメッシュレットの情報取得
	// ---------------------------------------------------------
	//uint _instanceID = g_baseInstanceIndex + a_gid.y;
	uint _instanceID = a_meshPayload.instanceID;					// インスタンスIDはAS側で求めたものを使用
	MeshInstanceData _inst = g_instanceData[_instanceID];

	// 対象のメッシュレットを取得
	//uint _globalMeshletIndex = _inst.meshletOffset + _arbitraryData;
	uint _globalMeshletIndex = a_meshPayload.MeshletIndices[a_gid.x];
	Meshlet _m = g_meshletData[_globalMeshletIndex];

	// 上限を超えないように安全装置を入れる
	uint _vCount = min(_m.vertexCount,64);
	uint _pCount = min(_m.primitiveCount,126);

	// MSの出力を宣言
	SetMeshOutputCounts(_m.vertexCount, _m.primitiveCount);

	// ---------------------------------------------------------
	// 頂点の処理 : スレッド 0 ～ 63 が並行して担当
	// ---------------------------------------------------------
	if (a_gtid < _m.vertexCount)
	{
		// ユニーク頂点インデックスの取得 
		uint _globalUVI = _inst.uviOffset + _m.vertexOffset + a_gtid;
		uint _localVertexIndex = g_uniqueVertexIndices[_globalUVI];

		// 静的な頂点。アニメメッシュでも uv はここから読む(スキニングで変わらないため)
		MeshVertex _static = g_vertices[_inst.vertexOffset + _localVertexIndex];

		Vertex _v;
		if(_inst.isAnimated == 0)
		{
			_v = DecodeMeshVertex(_static);
		}
		else
		{
			// 座標・法線・接線はスキニング後のものを使う
			_v = DecodeAnimatedVertex(g_animatedVertices[_inst.animatedVertexStart + _localVertexIndex], _static);
		}

		// ---------------------------------------------------------
		// ワールド変換と出力の構築
		// ---------------------------------------------------------
		VertexOutput _vout;
		
		// 座標変換 (現在フレーム)
		// 深度だけのパス(DepthOnlyMS)と同じ関数を通し、ZPre の深度と1ビットもずれないようにする
		_vout.pos = TransformMeshPosition(_v.pos, _inst.worldMat, _vout.worldPos);

		// モーションベクター用の過去位置。
		// アニメメッシュは「前フレームのスキニング済み座標」を使わないと、
		// ボーン変形分の動きが速度から抜け落ちて残像(ゴースト)になる。
		// 静的メッシュは変形しないので現在のローカル座標のままでよい(剛体移動は prevWorldMat が担当)。
		float3 _prevLocalPos = _v.pos;
		if (_inst.isAnimated != 0)
		{
			_prevLocalPos = g_prevAnimatedPositions[_inst.animatedVertexStart + _localVertexIndex];
		}
		float4 _prevWorldPos = mul(float4(_prevLocalPos, 1.0f), _inst.prevWorldMat);
		_vout.curClipPos = _vout.pos;
		_vout.prevClipPos = mul(_prevWorldPos, g_camera.prevViewProj);

		// 法線と接線の回転
		//
		// 法線は逆転置(GetNormalMatrix)で移す。ワールド行列をそのまま掛けると、
		// 非等方スケールのモデルで法線が引き伸ばされた軸へ倒れて陰影が壊れる。
		// 接線は面に沿うベクトルなのでワールド行列のままでよい。
		_vout.normal = Normal_LocalToWorld(_v.normal, _inst.worldMat);
		_vout.tangent = Tangent_LocalToWorld(_v.tangent, _inst.worldMat);
		
		_vout.uv = _v.uv;
		_vout.instanceID = _instanceID;

		outVerts[a_gtid] = _vout;
	}
	
	// ---------------------------------------------------------
	// プリミティブの処理 : スレッド 0 ～ 125 が並行して担当
	// ---------------------------------------------------------
	if (a_gtid < _m.primitiveCount)
	{
		// プリミティブインデックスの取得 
		uint _globalPrimIndex = _inst.primitiveOffset + _m.primitiveOffset + a_gtid;
		uint3 _indices = UnpackPrimitive(g_primitiveIndices[_globalPrimIndex]);
		
		primIndices[a_gtid] = _indices;
	}
}
