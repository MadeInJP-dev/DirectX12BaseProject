#include "RayGI.hlsli"

//------------------------------------------------------------------------------------------
// アニメーションするインスタンスはスキニング済み頂点から取る。
// 当たり判定(BLAS)だけでなくシェーディング属性(座標/法線/接線)も揃えないと、
// バインドポーズの静的頂点と食い違って影や反射がずれる
//------------------------------------------------------------------------------------------
Vertex FetchVertex(RayInstanceData a_inst, uint a_localIndex)
{
	// uv はスキニングで変わらないので、アニメメッシュでも静的な頂点から読む
	MeshVertex _static = g_vertexfloatData[a_inst.vertexStart + a_localIndex];
	if (a_inst.isAnimated == 0)
	{
		return DecodeMeshVertex(_static);
	}
	return DecodeAnimatedVertex(g_animatedVertexData[a_inst.animatedVertexStart + a_localIndex], _static);
}

// UV座標を取得
float2 GetUV(BuiltInTriangleIntersectionAttributes a_attribs, RayInstanceData instance, uint primID, RayMaterial material)
{
	float3 _barycentrics = float3(1.0 - a_attribs.barycentrics.x - a_attribs.barycentrics.y, a_attribs.barycentrics.x, a_attribs.barycentrics.y);

	// サブメッシュ番号とインスタンスのオフセットを加算
	uint _baseIndexLocation = instance.indexStart + material.startIndexLocation + (primID * 3);

	// メガバッファからローカルのインデックス番号を取得
	uint _v0 = g_indexData[_baseIndexLocation];
	uint _v1 = g_indexData[_baseIndexLocation + 1];
	uint _v2 = g_indexData[_baseIndexLocation + 2];

	// オフセットを足してメガバッファからUV取得(アニメメッシュはアニメ済みバッファから)
	float2 _uv0 = FetchVertex(instance, _v0).uv;
	float2 _uv1 = FetchVertex(instance, _v1).uv;
	float2 _uv2 = FetchVertex(instance, _v2).uv;

	float2 _uv = _barycentrics.x * _uv0 + _barycentrics.y * _uv1 + _barycentrics.z * _uv2;
	return _uv;
}
// 法線の取得
float3 GetNormal(BuiltInTriangleIntersectionAttributes a_attribs, float2 a_uv, RayInstanceData instance, uint primID, RayMaterial material)
{

	float3 _barycentrics = float3(1.0 - a_attribs.barycentrics.x - a_attribs.barycentrics.y, a_attribs.barycentrics.x, a_attribs.barycentrics.y);
	
	// サブメッシュ番号とインスタンスのオフセットを加算
	uint _baseIndexLocation = instance.indexStart + material.startIndexLocation + (primID * 3);
	
	uint _v0 = g_indexData[_baseIndexLocation];
	uint _v1 = g_indexData[_baseIndexLocation + 1];
	uint _v2 = g_indexData[_baseIndexLocation + 2];
	
	// 法線取得 (アニメメッシュはスキニング済み法線を使う)
	float3 _n0 = FetchVertex(instance, _v0).normal;
	float3 _n1 = FetchVertex(instance, _v1).normal;
	float3 _n2 = FetchVertex(instance, _v2).normal;
	float3 _normal = normalize(_barycentrics.x * _n0 + _barycentrics.y * _n1 + _barycentrics.z * _n2);

	// タンジェント (アニメメッシュはスキニング済み接線を使う)
	float3 _t0 = FetchVertex(instance, _v0).tangent;
	float3 _t1 = FetchVertex(instance, _v1).tangent;
	float3 _t2 = FetchVertex(instance, _v2).tangent;
	float3 _tangent = normalize(_barycentrics.x * _t0 + _barycentrics.y * _t1 + _barycentrics.z * _t2);

	// ビノーマルを計算
	float3 _binormal = normalize(cross(_tangent, _normal));

	// 法線マップもマテリアルに仕込んだベースインデックスから取得
	Texture2D normalTex = ResourceDescriptorHeap[material.normalIndex];
	float3 _binSpaceNormal = normalTex.SampleLevel(gSamp, a_uv, 0).rgb;
	_binSpaceNormal = (_binSpaceNormal * 2.0f) - 1.0f;

	// タンジェント空間からワールド空間に変換
	_normal = _tangent * _binSpaceNormal.x + _binormal * _binSpaceNormal.y + _normal * _binSpaceNormal.z;
	_normal = normalize(mul(_normal, (float3x3) WorldToObject3x4()));
	return _normal;
}

// レイ
struct RayPayload
{
	float3 color;
	int hit;
	int depth;
	uint seed; // 乱数状態を引き継ぐ
	float hitDistance;
};

// 光源に向かってレイを飛ばす
void TraceLightRay(inout RayPayload a_rayPayload, float3 a_normal, float3 a_geoNormal)
{
	float _hitT = RayTCurrent(); // レイが当たった距離
	float3 _rayDirW = WorldRayDirection(); // レイのワールド空間での方向
	float3 _rayOriginW = WorldRayOrigin(); // レイのワールド空間での開始位置
	float3 _posW = _rayOriginW + _hitT * _rayDirW; // レイが当たった位置を計算
	float3 _ligDir = normalize(-g_sun.dir); // 光の方向

	// 光源の方向にレイを飛ばす
	RayDesc _ray;
	// 法線方向に少し浮かせる。押し出し量はカメラからの距離に比例させる
	// (遠景ほどワールド座標の精度が粗くなるため、固定値だとアクネが出る)
	float _dist = length(g_camera.cameraPos.xyz - _posW);
	float _normalBias = max(0.005f, _dist * 0.001f);
	_ray.Origin = _posW + a_geoNormal * _normalBias;
	_ray.Direction = _ligDir;
	_ray.TMin = _normalBias;
	_ray.TMax = 50;

	// 遮蔽されているかどうかの初期値。
	// RAY_FLAG_SKIP_CLOSEST_HIT_SHADER を付けているので「当たった場合は何も実行されない」。
	// つまりヒット側では誰も hit を書けないので、"遮蔽されている(1)" を初期値にして、
	// 何にも当たらなかったときだけ ShadowMiss が 0 を書く、という形にする必要がある。
	// ここを 0 で初期化すると当たっても外れても hit == 0 になり、影が一切出なくなる。
	a_rayPayload.hit = 1;

	TraceRay(
		g_raytracingWorld, // TLAS
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE, // RayFlags
		0xFF, // InstanceInclusionMask
		1, // RayContributionToHitGroupIndex
		0, // MultiplierForGeometryContributionToHitGroupIndex
		1, // MissShaderIndex
		_ray, // Ray
		a_rayPayload // RayPayload
	);
}

// 反射レイを飛ばす
void TraceIndirectRay(inout RayPayload a_rayPayload, float3 a_normal)
{
	// ※現状このガードは常に成立して即returnする(=GIは1バウンス)。
	//   ClosestHit の先頭で depth を ++ してから呼ぶので、ここに来る時点で depth は必ず 1 以上。
	//   2バウンス目を有効にするには RayPSODesc::maxRecursionDepth を 3 に上げたうえで
	//   (バウンス先の ClosestHit がさらにシャドウレイを飛ばすため) このしきい値を緩める必要がある。
	//   1sppのままバウンスを増やすとノイズが増えるので、デノイズが安定してから触ること。
	if (a_rayPayload.depth >= 1)
	{
		return;
	}
	
	float _hitT = RayTCurrent(); // レイが当たった距離
	float3 _rayDirW = WorldRayDirection(); // レイのワールド空間での方向
	float3 _rayOriginW = WorldRayOrigin(); // レイのワールド空間での開始位置
	float3 _posW = _rayOriginW + _hitT * _rayDirW;

	// ペイロードからシードを取り出し、乱数を２つ作って次のシードを更新する
	float2 _rand = float2(Random01(a_rayPayload.seed), Random01(a_rayPayload.seed * 17 + 3));
	a_rayPayload.seed = Hash(a_rayPayload.seed); // シードを更新

	// 法線ベースの半球ランダム方向を取得
	float3 _diffuseDir = SampleHemisphereCosine(a_normal, _rand);

	// ペイロードを作成
	RayPayload _refPayload;
	_refPayload.color = float3(0, 0, 0);
	_refPayload.depth = a_rayPayload.depth;
	_refPayload.hit = 0;
	_refPayload.seed = a_rayPayload.seed; // 更新したシードを渡す
	
	// 反射レイを飛ばす
	RayDesc _ray;
	_ray.Origin = _posW + a_normal * 0.0001f;
	_ray.Direction = _diffuseDir; // ランダムな方向に飛ばす
	_ray.TMin = 0.001f;
	_ray.TMax = 1000;

	TraceRay(
			g_raytracingWorld,
			0,
			0xFF,
			0,
			0,
			0,
			_ray,
			_refPayload
		);

	a_rayPayload.color = _refPayload.color;
}


// フル解像度のピクセル座標とデバイス深度からワールド座標を復元する
float3 ReconstructWorldPos(int2 a_fullResId, float2 a_fullResDim, float a_depth)
{
	float2 _uv = (float2(a_fullResId) + 0.5f) / a_fullResDim;
	float4 _clip = float4(_uv.x * 2.0f - 1.0f, 1.0f - _uv.y * 2.0f, a_depth, 1.0f);
	float4 _world = mul(_clip, g_camera.invViewProj);
	return _world.xyz / _world.w;
}

// 深度バッファからポリゴン平面の法線(ジオメトリ法線)を復元する
//
// GBufferの法線は法線マップ適用後のシェーディング法線なので、
// これをレイの押し出し方向や半球の基準に使うと、ポリゴン平面より下に
// レイが潜り込んで自分自身に当たる(自己交差)。
// 当たった先は自分の面なので暗い色を持ち帰り、しかも方向が毎フレーム
// 乱数で変わるため、位置の変わる黒い斑点＝Zファイティングのような
// チカチカとして出る。
//
// 前方差分と後方差分のうち深度の変化が小さい方を採用して、
// 輪郭(深度の段差)で法線が破綻するのを防ぐ。
float3 ReconstructGeometryNormal(
	Texture2D a_depthTex,
	int2 a_fullResId,
	float2 a_fullResDim,
	float a_centerDepth,
	float3 a_worldPos,
	float3 a_fallbackNormal)
{
	int2 _maxId = int2(a_fullResDim) - 1;

	int2 _rightId = min(a_fullResId + int2(1, 0), _maxId);
	int2 _leftId  = max(a_fullResId - int2(1, 0), int2(0, 0));
	int2 _downId  = min(a_fullResId + int2(0, 1), _maxId);
	int2 _upId    = max(a_fullResId - int2(0, 1), int2(0, 0));

	float _rightDepth = a_depthTex.Load(int3(_rightId, 0)).r;
	float _leftDepth  = a_depthTex.Load(int3(_leftId, 0)).r;
	float _downDepth  = a_depthTex.Load(int3(_downId, 0)).r;
	float _upDepth    = a_depthTex.Load(int3(_upId, 0)).r;

	// 深度差が小さい側（＝同じ面に乗っている可能性が高い側）を選ぶ
	float3 _dx = (abs(_rightDepth - a_centerDepth) < abs(_leftDepth - a_centerDepth))
		? (ReconstructWorldPos(_rightId, a_fullResDim, _rightDepth) - a_worldPos)
		: (a_worldPos - ReconstructWorldPos(_leftId, a_fullResDim, _leftDepth));

	float3 _dy = (abs(_downDepth - a_centerDepth) < abs(_upDepth - a_centerDepth))
		? (ReconstructWorldPos(_downId, a_fullResDim, _downDepth) - a_worldPos)
		: (a_worldPos - ReconstructWorldPos(_upId, a_fullResDim, _upDepth));

	float3 _cross = cross(_dx, _dy);
	float _len = length(_cross);

	// 復元に失敗した(隣接ピクセルが同じ位置になる等)場合はシェーディング法線で代用する
	if (_len < 1e-8f)
	{
		return a_fallbackNormal;
	}

	float3 _geoNormal = _cross / _len;

	// 外積の向きは座標系と差分の取り方で反転しうるので、
	// シェーディング法線を基準にして表を向かせる
	if (dot(_geoNormal, a_fallbackNormal) < 0.0f)
	{
		_geoNormal = -_geoNormal;
	}
	return _geoNormal;
}

// レイ生成シェーダー
[shader("raygeneration")]
void RayGen()
{
	uint2 _id = DispatchRaysIndex().xy;
	uint2 _dim = DispatchRaysDimensions().xy;

	// GBuffer はフル解像度だが、出力はハーフ、もしくはクォーターになるため
	uint2 _fullResId = _id * 2;
	float2 _fullResDim = _dim * 2.0f;
	
	// ※画面UVからのワールド座標復元は ReconstructWorldPos にまとめてある
	//   （ジオメトリ法線の復元でも隣接ピクセルに対して同じ計算が要るため）

	// GBuffer取得
	Texture2D _depthTex = ResourceDescriptorHeap[g_gbuffer.depth];
	Texture2D _normalTex = ResourceDescriptorHeap[g_gbuffer.normal];

	// 深度値を取得
	float _depth = _depthTex.Load(int3(_fullResId, 0)).r;
	if (_depth >= 1.0f)
	{
		gOutPut[_id] = float4(1, 1, 1, -1.0f);
		return;
	}
	
	// 法線を取得
	float2 _enc = _normalTex.Load(int3(_fullResId, 0)).rg; // 法線
	float3 _normal = DecsodeNormal(_enc); // 法線を復元

	// 3D空間での位置を復元
	float3 _worldPos = ReconstructWorldPos(_fullResId, _fullResDim, _depth);

	// ポリゴン平面の法線を復元する（レイの押し出しと半球のクランプに使う）
	float3 _geoNormal = ReconstructGeometryNormal(
		_depthTex, _fullResId, _fullResDim, _depth, _worldPos, _normal);

	// ピクセル座標からサーフェイス面上で阪急範囲にランダムにレイを飛ばす
	// ピクセル位置と時間をXORで混ぜてからハッシュ化する
	uint pixelIndex = _id.y * _dim.x + _id.x;
	uint _seed = Hash(pixelIndex ^ (g_gbuffer.frameCount * 2654435769u));

	// レイ構造体を作成
	RayDesc _ray;
	float _dist = length(g_camera.cameraPos.xyz - _worldPos);

	// 押し出し量。深度から復元したワールド座標は遠景ほど誤差が乗るので距離に比例させる。
	// 押し出す向きはシェーディング法線ではなくジオメトリ法線を使う
	// （法線マップで傾いた方向に押し出しても面から離れられないため）
	float _bias = max(0.01f, _dist * 0.002f);
	_ray.Origin = _worldPos + _geoNormal * _bias;

	// 半球サンプリング自体はシェーディング法線基準（見た目の凹凸をGIに反映させたい）
	float3 _dir = SampleHemisphereCosine(_normal, float2(Random01(_seed), Random01(_seed * 17 + 3)));

	// ただしポリゴン平面より下を向いてしまった方向は面の上へ折り返す。
	// これをやらないと自分自身に当たって黒い斑点になる
	if (dot(_dir, _geoNormal) < 0.0f)
	{
		_dir = normalize(reflect(_dir, _geoNormal));
	}
	_ray.Direction = _dir;

	// 原点をずらしただけだと浅い角度で面をかすめるので、TMinも同じスケールで持ち上げる
	_ray.TMin = _bias;
	_ray.TMax = 1000;

	// ペイロードを作成
	RayPayload _payload;
	_payload.color = float3(0, 0, 0);
	_payload.depth = 0;
	_payload.hit = 0;
	_payload.seed = Hash(_seed);
	_payload.hitDistance = -1.0f;

	// レイ発射
	TraceRay(
		g_raytracingWorld,
		0,
		0xFF,
		0,
		0,
		0,
		_ray,
		_payload
	);

	// 出力
	gOutPut[_id] = float4(_payload.color, _payload.hitDistance);
}

// レイがどのポリゴンとも接触しなかったときに呼び出されるシェーダー
[shader("miss")]
void Miss(inout RayPayload a_payload)
{
	a_payload.color = float3(0.7, 0.7, 0.8);
	a_payload.hitDistance = -1.0f;				// 当たらなかったらマイナス
}

[shader("miss")]
void ShadowMiss(inout RayPayload a_payload)
{
	a_payload.hit = 0;
}

// レイがポリゴンにヒットしたときに呼び出されるシェーダー
[shader("closesthit")]
void ClosestHit(inout RayPayload a_payload, in BuiltInTriangleIntersectionAttributes a_attr)
{
	a_payload.depth++;

	// 今のレイが飛んだ距離をペイロードに保存
	a_payload.hitDistance = RayTCurrent();
	
	// 各種IDとデータの取得
	uint instID = InstanceID(); // インスタンス番号
	uint geomID = GeometryIndex(); // 当たったサブメッシュ番号
	uint primID = PrimitiveIndex(); // 当たったポリゴン番号

	// データを配列から取得
	RayInstanceData instance = g_instanceData[instID]; // インスタンス情報
	RayMaterial _material = g_materialData[instance.materialOffset + geomID]; // サブメッシュマテリアル情報

	// ---------------------------------------------------------
	// メガバッファからのデータ取得
	// ---------------------------------------------------------
	
	// サブメッシュの開始位置 ＋ このポリゴンのオフセット ＋ インスタンスの全体インデックスオフセット
	uint _baseIndexLocation = instance.indexStart + _material.startIndexLocation + (primID * 3);
	
	// メガバッファからインデックスを取得
	uint _v0 = g_indexData[_baseIndexLocation];
	uint _v1 = g_indexData[_baseIndexLocation + 1];
	uint _v2 = g_indexData[_baseIndexLocation + 2];

	// 取得したローカルインデックスから頂点を取得(アニメメッシュはスキニング済み頂点を使う)。
	// BLASはアニメ済み頂点で更新されているので、ここも合わせないとジオメトリ法線が
	// バインドポーズ基準になり、アニメ時にライティングがずれる。
	Vertex _vert0 = FetchVertex(instance, _v0);
	Vertex _vert1 = FetchVertex(instance, _v1);
	Vertex _vert2 = FetchVertex(instance, _v2);

	// 頂点の位置（オブジェクト空間）を取得
	float3 _vpos0 = _vert0.pos;
	float3 _vpos1 = _vert1.pos;
	float3 _vpos2 = _vert2.pos;
	
	// オブジェクト空間からワールド空間へ変換（ObjectToWorld4x3()を使用）
	// ObjectToWorld4x3()がオブジェクトからワールドへの変換行列を返す
	_vpos0 = mul(float4(_vpos0, 1.0), ObjectToWorld4x3());
	_vpos1 = mul(float4(_vpos1, 1.0), ObjectToWorld4x3());
	_vpos2 = mul(float4(_vpos2, 1.0), ObjectToWorld4x3());
	

	// 外積を使ってジオメトリ法線（ポリゴン平面の法線）を計算
	float3 _geoNormal = normalize(cross(_vpos1 - _vpos0, _vpos2 - _vpos0));
	// UV取得 (引数を追加)
	float2 _uv = GetUV(a_attr, instance, primID, _material);

	// 法線取得 (引数を追加)
	float3 _normal = GetNormal(a_attr, _uv, instance, primID, _material);

	// 光源に向かってレイを飛ばす
	// hit の初期化は TraceLightRay の中で行う（フラグと対で意味が決まるため）
	TraceLightRay(a_payload, _normal, _geoNormal);
	float _lig = 0.0f;
	// 直接光が当たってる
	if (a_payload.hit == 0)
	{
		float3 _ligDir = normalize(-g_sun.dir);
		// ジオメトリ法線がライトに対して背を向けている場合強制的に影にする
		//float _GdotL = dot(_geoNormal, _ligDir);
		//if (_GdotL > 0.0f)
		//{
			_lig = max(0, dot(_normal, _ligDir));
		//}
		//else
		//{
		//	_lig = 0.0f;
		//}

	}
	
	// 間接光の取得
	RayPayload _refPayload;
	_refPayload.color = float3(0,0,0);
	_refPayload.depth = a_payload.depth;
	_refPayload.seed = a_payload.seed;
	TraceIndirectRay(_refPayload, _normal); // 次のバウンスレイを飛ばす

	// テクスチャからのアルベド取得
	float dist = RayTCurrent();
	float lod = clamp(log2(dist * 0.5), 0, 5);
	Texture2D albedoTex = ResourceDescriptorHeap[_material.baseIndex];
	Texture2D metaRogTex = ResourceDescriptorHeap[_material.metaRoughnessIndex];
	float3 _albedo = albedoTex.SampleLevel(gSamp, _uv, lod).rgb * _material.baseColor.xyz;

	// 最終的な色の合成
	float3 _directLight = (_lig / 3.141592f) * (g_sun.color.rgb * g_sun.brightness); // ライトの色をかける
	float3 _indirectLight = _refPayload.color; // 飛んだ先から持ち帰ってきた色

	// アルベド * (直接光 + 間接光) + エミッシブ(自己発光)
	// emissiveAdd はマテリアルに依らない自己発光。ここに乗せることで、
	// 光っている物体が跳ね返りのレイ経由で周りを照らす(GIの光源になる)
	float3 _finalColor = _albedo * (_directLight + _indirectLight) + _material.emissive + _material.emissiveAdd;

	// RGBの計算結果がマイナスや無限大（NaN）になるのを防ぐ
	a_payload.color = clamp(_finalColor, 0.0f, 10.0f);
	
	// ペイロードの深度とシードを親に渡す
	a_payload.seed = _refPayload.seed;
	a_payload.depth--;
}

[shader("closesthit")]
void ShadowCHS(inout RayPayload a_payload, in BuiltInTriangleIntersectionAttributes a_attr)
{
	a_payload.hit = 1;
}


