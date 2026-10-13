#include "MeshCommon.hlsli"

// グローバル空間で共有メモリとして宣言
groupshared PayloadStruct s_payload;

// 可視メッシュレット数を数えるための共有カウンター
groupshared uint s_VisibleCount;

// 可視性チェック
bool IsVisible(MeshletCullData a_cullData, float4x4 a_worldMat)
{
	// ==========================================================
	// バックフェイスカリング
	// ==========================================================
	// 32bitの NormalCone から各8bitを取り出し、0～255 を -1.0f～1.0fにアンパック
	//float4 _unpackedCone = float4(
	//	a_cullData.NormalCone & 0xFF,
	//	(a_cullData.NormalCone >> 8) & 0xFF,
	//	(a_cullData.NormalCone >> 16) & 0xFF,
	//	(a_cullData.NormalCone >> 24) & 0xFF) / 125.5f - 1.0f;

	//float3 _axisLocal = _unpackedCone.xyz;		// メッシュレットが向いている基準の方向 : ローカル
	//float _cutoff = _unpackedCone.w;			// 許容される角度の閾値 : -cos(angle)に相当するらしい

	uint4 _rawCone = uint4(
    a_cullData.NormalCone & 0xFF,
    (a_cullData.NormalCone >> 8) & 0xFF,
    (a_cullData.NormalCone >> 16) & 0xFF,
    (a_cullData.NormalCone >> 24) & 0xFF);

	// 法線の広がりが 90 度以上あるメッシュレットは、コーンで裏表を判定できない。
	// DirectXMesh はその場合 NormalCone を (0,0,0,255) で書き出してくるので、
	// 判定に使わず素通しする。
	// (無視すると軸が normalize(-1,-1,-1)・閾値 1.0 の「ほぼ当たらないが、
	//  真正面から見たときだけ当たる」コーンとして効いてしまい、
	//  角度によってメッシュレットが1枚だけ消える)
	bool _isConeDegenerate = (_rawCone.w == 0xFF);

	// 非等方スケールが掛かっているとコーンは使えない。
	//
	// 軸だけなら逆転置で正しく移せるが、コーンの開き角(cutoff)は
	// スケールで潰れたり広がったりするので、ローカルで作った値のまま使うと
	// 実際より狭いコーンとして判定してしまい、見えているメッシュレットまで消える。
	// (glTFのノードに 50,1,50 のようなスケールが入った地形で顕著に出る)
	// 開き角を移す手立てが無いので、等方スケールのときだけ判定に使う。
	bool _canUseCone = !_isConeDegenerate && IsUniformScale(a_worldMat);

	if (_canUseCone)
	{
		// 軸(xyz)は -1.0～1.0 にアンパック
		float3 _axisLocal = (_rawCone.xyz / 255.0f) * 2.0f - 1.0f;

		// cutoff(w)は 0.0～1.0 にアンパック（バイアス不要）
		float _cutoff = _rawCone.w / 255.0f;

		// コーンの原点(Apex)をローカル座標で求める
		float3 _apexLocal = a_cullData.BoundingSphereCenter - (_axisLocal * a_cullData.ApexOffset);

		// ワールド空間に変換
		// 軸は法線と同じ扱い(逆転置)。ワールド行列をそのまま掛けると向きがずれる
		float3 _apexWorld = mul(float4(_apexLocal,1.0f),a_worldMat).xyz;
		float3 _axisWorld = Normal_LocalToWorld(_axisLocal, a_worldMat);

		// コーンの頂点からカメラのベクトル : 正規化
		// 描く視点ではなくカリング用のカメラで判定する(普段は同じ値)
		float3 _viewToApex = normalize(_apexWorld - g_camera.cullPos.xyz);

		// カメラから見て、コーンが完全に裏面を向いていたらカリング
		// 内積が閾値を超えたら、裏面を向いていると判定
		if (dot(_viewToApex,_axisWorld) > _cutoff)
		{
			return false;
		}
	}

	// ==========================================================
	// フラスタムカリング
	// ==========================================================
	// ローカル座標系の球の中心にワールド行列を掛けて、ワールド座標に変換する
	float3 _centerWorld = mul(float4(a_cullData.BoundingSphereCenter, 1.0f), a_worldMat).xyz;
	
	//float _radiusWorld = a_cullData.BoundingSphereRadius * a_worldMat._11_22_33_44;
	float _scaleX = length(a_worldMat[0].xyz);
	float _scaleY = length(a_worldMat[1].xyz);
	float _scaleZ = length(a_worldMat[2].xyz);
	float _radiusWorld = a_cullData.BoundingSphereRadius * max(_scaleX, max(_scaleY, _scaleZ));
	for (int _idx = 0; _idx < 6; ++_idx)
	{
		// 座標と平面との距離計算
		float4 _plane = g_camera.cullFrustumPlanes[_idx];
		// 平面の方程式 (dot(Normal, Point) + Distance)
		if (dot(_plane.xyz, _centerWorld) + _plane.w < -_radiusWorld)
		{
			return false;
		}
	}

	// すべてのテストを通過したら true
	return true;
}

[numthreads(32, 1, 1)]
void ASMain(
    uint a_gtid : SV_GroupThreadID,
    uint3 a_groupID : SV_GroupID)
{
	// --------------------------------------------------
	// ペイロードとカウンターの初期化（スレッド0が行う）
	// --------------------------------------------------
	// Y は同じメッシュをまとめて描いたときの何体目か(インスタンシング)。
	// インスタンスデータはパス間で共有していて描く順に並んでいないので、表を1枚挟んで引く
	uint _instanceID = g_drawInstanceIndices[g_baseInstanceIndex + a_groupID.y];

	MeshInstanceData _inst = g_instanceData[_instanceID];

	// このパスのカメラで、どの LOD の段で描くかを決める(段を持たなければ LOD0)。
	// グループの全スレッドが同じインスタンスを同じ式で見るので、結果はそろう
	const MeshLodRange _lod = SelectMeshLod(_inst);

	if (a_gtid == 0)
	{
		s_payload.instanceID = _instanceID;
		s_payload.vertexOffset = _lod.vertexOffset;
		s_payload.uviOffset = _lod.uviOffset;
		s_payload.primitiveOffset = _lod.primitiveOffset;
		s_VisibleCount = 0; // カウンターを0にリセット
	}

	// 初期化が終わるのを全スレッドで確実に待つ！
	GroupMemoryBarrierWithGroupSync();

	// メッシュレットIDを計算(選んだ段の中での番号)
	uint _localMeshletID = (a_groupID.x * 32) + a_gtid;
	uint _globalMeshletID = _lod.meshletOffset + _localMeshletID;

	// --------------------------------------------------
	// 可視性チェック
	// --------------------------------------------------
	bool _isVisible = false;
	if (_localMeshletID < _lod.meshletCount)
	{
		// ==========================================================
		// スキニングするメッシュはカリングしない
		// ----------------------------------------------------------
		// カリング情報(境界球・法線コーン)は DirectXMesh がバインドポーズの
		// 頂点から作ったもので、モデルローカル空間に固定されている。
		// 一方 MS が読むのはスキニング済みの頂点なので、ボーンが動くほど
		// 実際の位置と離れていく。手足のように大きく動く部分ほどズレが大きい。
		//
		// そのズレは「境界がフラスタム面をまたぐとき」だけ結果に出る。
		// 画面の中央で全身が収まっている間はどの面にも当たらないので誰も気付かないが、
		// カメラに近い(ニア面をまたぐ)ときや画面端(左右上下の面をまたぐ)では、
		// 実際には映っているメッシュレットがバインドポーズの位置で判定されて
		// 消えてしまう。法線コーンも同じ理由で当てにならない。
		//
		// 正しく間引くにはスキニング後の頂点から毎フレーム境界を作り直す必要があるので、
		// それまでは素通しする。
		// ==========================================================
		if (_inst.isAnimated != 0)
		{
			_isVisible = true;
		}
		else
		{
			uint _cullDataIndex = _lod.cullStart + _localMeshletID;
			_isVisible = IsVisible(g_cullData[_cullDataIndex], _inst.worldMat);
		}
	}
	
	// --------------------------------------------------
	// ペイロードへの格納（Wave関数ではなくInterlockedAddを使う）
	// --------------------------------------------------
	if (_isVisible)
	{
		uint _idx = 0;
		// s_VisibleCount に 1 を足し、足す前の値を _idx に取得する
		InterlockedAdd(s_VisibleCount, 1, _idx);
		
		s_payload.MeshletIndices[_idx] = _globalMeshletID;
	}

	// ペイロードへの書き込みが終わるのを全スレッドで待つ
	GroupMemoryBarrierWithGroupSync();

	// --------------------------------------------------
	// メッシュシェーダーの起動
	// --------------------------------------------------
	DispatchMesh(s_VisibleCount, 1, 1, s_payload);

}
