//==========================================================================================
//
// RayReflectionCS
//
// 鏡面反射 : 画素ごとに視線を法線で反射したレイを1本飛ばし、当たった先の放射輝度を書く。
//   rgb = 反射先の放射輝度(HDR) / a = 1 : 物に当たった / 0 : 空(環境)
// 背景の画素(深度が far)は (0, 0, 0, 0)。
//
// ・出力は固定解像度(1920x1080)。描画解像度とずれていてもよいように、
//   GBuffer は出力画素の UV から引く
// ・DispatchRays ではなくインラインレイトレ(RayQuery)で飛ばす。
//   当たった先のシェーディングはこのシェーダーの中で済ませる(ヒットシェーダーが無い)
// ・当たった先の照明は 主光源(影はレイ1本) + 環境光 + 自己発光。
//   金属度と粗さで拡散と鏡面に分ける(鏡面に映るものは環境光で代用する)。
//   ポイントライトと間接光(2バウンス目)は拾わない
// ・空に抜けたレイはスカイテクスチャを引く。テクスチャが無ければ環境光の色
//
//==========================================================================================

// ルートパラメーターの構造体
#include "../../../Common/RootSignatureLayout.hlsli"
#include "../../../Common/RootParameters/CameraData.hlsli"
#include "../../../Common/RootParameters/LightData.hlsli"
#include "../../../Common/RootParameters/SkyData.hlsli"
#include "../../../Common/RootParameters/AmbientData.hlsli"
#include "../../../Common/RootParameters/RaytracingData.hlsli"
#include "../../../Common/RootParameters/Vertex.hlsli"

// ヘルパー関数
#include "../Raytracing.hlsli"
#include "../../../Common/Math/CalcNormal.hlsli"
#include "../../../Common/Math/EnvBRDF.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)          カメラ
//   1 : CBV(b1)          主光源
//   2 : CBV(b2)          スカイ設定(空に抜けたレイの色)
//   3 : CBV(b3)          環境光(当たった先の間接光の代わり)
//   4 : CBV(b4)          反射の設定
//   5 : SRVの番号      深度 / 法線(レンダーグラフが張る)
//   6 : SRVの番号      インスタンス / マテリアル / 頂点 / インデックス / アニメ済み頂点 / スカイ(パスが張る)
//   7 : UAVの番号      反射の出力(レンダーグラフが張る)
//   8 : SRV(t0)          TLAS(パスが張る)
//
// 実体は C++ 側(RaytracingReflectionPass)。並びを変えるときは両方を揃えること
//==========================================================================================
#define RAY_REFLECTION_RS \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
"CBV(b0, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b1, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b2, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b3, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b4, visibility = SHADER_VISIBILITY_ALL)," \
"RootConstants(num32BitConstants=2, b100)," \
"RootConstants(num32BitConstants=6, b101)," \
"RootConstants(num32BitConstants=1, b102)," \
"SRV(t0)," \
RS_STATIC_SAMPLER "," \
"StaticSampler(s1, " \
"    filter = FILTER_MIN_MAG_MIP_LINEAR, " \
"    addressU = TEXTURE_ADDRESS_WRAP, " \
"    addressV = TEXTURE_ADDRESS_CLAMP, " \
"    addressW = TEXTURE_ADDRESS_CLAMP)"

//==========================================================================================
// 定数バッファデータ
//==========================================================================================

// カメラ
cbuffer CBCamera : register(b0)
{
	CameraData g_camera;
}

// ライト
cbuffer CBSunLight : register(b1)
{
	SunLightData g_sun;
}

// スカイ
cbuffer CBSky : register(b2)
{
	SkyData g_sky;
}

// 環境光
cbuffer CBAmbient : register(b3)
{
	AmbientData g_ambient;
}

// 反射の設定
// ※ CPU 側(RaytracingReflectionPass の ReflectionCB)と並びを合わせること
struct ReflectionParam
{
	float maxDistance;		// レイの届く距離
	uint isShadow;			// 当たった先で主光源への影を求めるか
	float2 pad;
};

cbuffer CBReflection : register(b4)
{
	ReflectionParam g_reflection;
}

//==========================================================================================
// 入力テクスチャ
//==========================================================================================
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_depthTexIndex;
	uint g_normalTexIndex;
}

Texture2D<float> Get_depthTex()
{
	Texture2D<float> _r = ResourceDescriptorHeap[g_depthTexIndex];
	return _r;
}
#define g_depthTex Get_depthTex()

// 法線は八面体圧縮で rg に入っている
Texture2D<float4> Get_normalTex()
{
	Texture2D<float4> _r = ResourceDescriptorHeap[g_normalTexIndex];
	return _r;
}
#define g_normalTex Get_normalTex()

//==========================================================================================
// 入力バッファ
//==========================================================================================
cbuffer PassDescriptorIndex1 : register(b101)
{
	uint g_instanceBufferIndex;				// インスタンス
	uint g_materialBufferIndex;				// マテリアル
	uint g_vertexBufferIndex;				// 頂点
	uint g_indexBufferIndex;				// インデックス
	uint g_animatedVertexBufferIndex;		// アニメーション後頂点
	uint g_skyTexIndex;						// スカイ(正距円筒)。無ければ 0xFFFFFFFF
}

StructuredBuffer<RayInstanceData> Get_instanceBuffer()
{
	StructuredBuffer<RayInstanceData> _r = ResourceDescriptorHeap[g_instanceBufferIndex];
	return _r;
}
#define g_instanceBuffer Get_instanceBuffer()

StructuredBuffer<RayMaterial> Get_materialBuffer()
{
	StructuredBuffer<RayMaterial> _r = ResourceDescriptorHeap[g_materialBufferIndex];
	return _r;
}
#define g_materialBuffer Get_materialBuffer()

StructuredBuffer<MeshVertex> Get_vertexBuffer()
{
	StructuredBuffer<MeshVertex> _r = ResourceDescriptorHeap[g_vertexBufferIndex];
	return _r;
}
#define g_vertexBuffer Get_vertexBuffer()

StructuredBuffer<uint> Get_indexBuffer()
{
	StructuredBuffer<uint> _r = ResourceDescriptorHeap[g_indexBufferIndex];
	return _r;
}
#define g_indexBuffer Get_indexBuffer()

StructuredBuffer<AnimatedVertex> Get_animatedVertexBuffer()
{
	StructuredBuffer<AnimatedVertex> _r = ResourceDescriptorHeap[g_animatedVertexBufferIndex];
	return _r;
}
#define g_animatedVertexBuffer Get_animatedVertexBuffer()

Texture2D<float4> Get_skyTex()
{
	Texture2D<float4> _r = ResourceDescriptorHeap[g_skyTexIndex];
	return _r;
}
#define g_skyTex Get_skyTex()

//==========================================================================================
// レイトレワールド
//==========================================================================================
RaytracingAccelerationStructure g_raytracingWorld : register(t0);

//==========================================================================================
// 出力UAV
//==========================================================================================
cbuffer PassDescriptorIndex2 : register(b102)
{
	uint g_outTexIndex;
}

RWTexture2D<float4> Get_outTex()
{
	RWTexture2D<float4> _r = ResourceDescriptorHeap[g_outTexIndex];
	return _r;
}
#define g_outTex Get_outTex()

//==========================================================================================
// サンプラー
//==========================================================================================
SamplerState g_samp : register(s0);		// マテリアル(WRAP)
SamplerState g_skySamp : register(s1);	// スカイ(V だけ CLAMP。天頂で地面の色が回り込まないように)

//==========================================================================================
// GBuffer から面を戻す
//==========================================================================================

// GBuffer の画素座標とデバイス深度からワールド座標を復元する
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
// これをレイの押し出し方向に使うと、ポリゴン平面より下に
// レイが潜り込んで自分自身に当たる(自己交差)。
//
// 前方差分と後方差分のうち深度の変化が小さい方を採用して、
// 輪郭(深度の段差)で法線が破綻するのを防ぐ。
float3 ReconstructGeometryNormal(
	Texture2D<float> a_depthTex,
	int2 a_fullResId,
	float2 a_fullResDim,
	float a_centerDepth,
	float3 a_worldPos,
	float3 a_fallbackNormal)
{
	int2 _maxId = int2(a_fullResDim) - 1;

	int2 _rightId = min(a_fullResId + int2(1, 0), _maxId);
	int2 _leftId = max(a_fullResId - int2(1, 0), int2(0, 0));
	int2 _downId = min(a_fullResId + int2(0, 1), _maxId);
	int2 _upId = max(a_fullResId - int2(0, 1), int2(0, 0));

	float _rightDepth = a_depthTex.Load(int3(_rightId, 0)).r;
	float _leftDepth = a_depthTex.Load(int3(_leftId, 0)).r;
	float _downDepth = a_depthTex.Load(int3(_downId, 0)).r;
	float _upDepth = a_depthTex.Load(int3(_upId, 0)).r;

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

//==========================================================================================
// 当たった先の面
//==========================================================================================

// 当たった三角形の面情報
struct HitSurface
{
	float3 pos;			// ワールド座標
	float3 normal;		// シェーディング法線(法線マップ込み)
	float3 geoNormal;	// ポリゴン平面の法線
	float2 uv;
};

// アニメーションするインスタンスはスキニング済み頂点から取る。
// BLAS はスキニング済み頂点で作り直されているので、ここも合わせないと
// バインドポーズの頂点と食い違って反射がずれる
Vertex FetchVertex(RayInstanceData a_inst, uint a_localIndex)
{
	// uv はスキニングで変わらないので、アニメメッシュでも静的な頂点から読む
	MeshVertex _static = g_vertexBuffer[a_inst.vertexStart + a_localIndex];
	if (a_inst.isAnimated == 0)
	{
		return DecodeMeshVertex(_static);
	}
	return DecodeAnimatedVertex(g_animatedVertexBuffer[a_inst.animatedVertexStart + a_localIndex], _static);
}

// 当たった三角形の頂点を重心座標で補間して、ワールド空間の面を作る
HitSurface BuildHitSurface(
	RayInstanceData a_inst,
	RayMaterial a_mat,
	uint a_primitiveID,
	float2 a_barycentrics,
	float3x4 a_objectToWorld,
	float3x4 a_worldToObject)
{
	float3 _bary = float3(1.0f - a_barycentrics.x - a_barycentrics.y, a_barycentrics.x, a_barycentrics.y);

	// サブメッシュの開始位置 ＋ このポリゴンのオフセット ＋ インスタンスの全体インデックスオフセット
	uint _baseIndexLocation = a_inst.indexStart + a_mat.startIndexLocation + (a_primitiveID * 3);

	Vertex _v0 = FetchVertex(a_inst, g_indexBuffer[_baseIndexLocation]);
	Vertex _v1 = FetchVertex(a_inst, g_indexBuffer[_baseIndexLocation + 1]);
	Vertex _v2 = FetchVertex(a_inst, g_indexBuffer[_baseIndexLocation + 2]);

	HitSurface _surface;

	// 座標
	float3 _pos0 = mul(a_objectToWorld, float4(_v0.pos, 1.0f));
	float3 _pos1 = mul(a_objectToWorld, float4(_v1.pos, 1.0f));
	float3 _pos2 = mul(a_objectToWorld, float4(_v2.pos, 1.0f));
	_surface.pos = _bary.x * _pos0 + _bary.y * _pos1 + _bary.z * _pos2;

	// UV
	_surface.uv = _bary.x * _v0.uv + _bary.y * _v1.uv + _bary.z * _v2.uv;

	// ポリゴン平面の法線(ワールド空間)
	_surface.geoNormal = normalize(cross(_pos1 - _pos0, _pos2 - _pos0));

	// 法線と接線(オブジェクト空間)
	float3 _normal = normalize(_bary.x * _v0.normal + _bary.y * _v1.normal + _bary.z * _v2.normal);
	float3 _tangent = normalize(_bary.x * _v0.tangent + _bary.y * _v1.tangent + _bary.z * _v2.tangent);
	float3 _binormal = normalize(cross(_tangent, _normal));

	// 法線マップ
	Texture2D _normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(a_mat.normalIndex)];
	float3 _tanNormal = _normalTex.SampleLevel(g_samp, _surface.uv, 0).rgb * 2.0f - 1.0f;
	_normal = _tangent * _tanNormal.x + _binormal * _tanNormal.y + _normal * _tanNormal.z;

	// オブジェクト空間 → ワールド空間(法線は逆転置で運ぶ)
	_surface.normal = normalize(mul(_normal, (float3x3) a_worldToObject));
	return _surface;
}

//==========================================================================================
// 光
//==========================================================================================

// 主光源が見えるか : 1 = 日なた / 0 = 影
float TraceSunVisibility(float3 a_origin, float3 a_toSun, float a_bias)
{
	RayDesc _ray;
	_ray.Origin = a_origin;
	_ray.Direction = a_toSun;
	_ray.TMin = a_bias;
	_ray.TMax = 10000.0f;

	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> _query;
	_query.TraceRayInline(g_raytracingWorld, RAY_FLAG_NONE, 0xFF, _ray);

	// 全部不透明扱いなので、候補を自分で判定する必要は無い(1回で走査が終わる)
	_query.Proceed();

	return (_query.CommittedStatus() == COMMITTED_NOTHING) ? 1.0f : 0.0f;
}

// 方向 → 正距円筒(緯度経度)UV。SkyShader と同じ取り方
float2 DirectionToEquirectUV(float3 a_dir)
{
	const float _yaw = atan2(a_dir.x, a_dir.z);

	float2 _uv;
	_uv.x = _yaw / (2.0f * PI) + 0.5f + (g_sky.rotationDeg / 360.0f);
	_uv.y = acos(clamp(a_dir.y, -1.0f, 1.0f)) / PI;

	return _uv;
}

// 空に抜けたレイの放射輝度
float3 SampleEnvironment(float3 a_dir)
{
	// スカイテクスチャが無いシーンは、環境光を一様な空の色として使う
	if (g_skyTexIndex == 0xFFFFFFFFu)
	{
		return g_ambient.ambientColor;
	}

	return g_skyTex.SampleLevel(g_skySamp, DirectionToEquirectUV(a_dir), 0).rgb * g_sky.exposure;
}

// 非金属の基本反射率。ディファードの dielectricF0 の既定値と揃える
static const float DIELECTRIC_F0 = 0.04f;

// 当たった先の放射輝度
//
//   拡散 : アルベド * (主光源 + 環境光) * 拡散へ回る割合
//   鏡面 : 環境光 * 鏡面へ回る割合(映り込む先までは追わないので、周りを環境光で代用する)
//   + 自己発光
//
// 割合は GBuffer と同じ金属度・粗さから、ディファードと同じ環境BRDFで決める。
// 金属は拡散を持たないので、拡散の割合に (1 - 金属度) を掛ける
float3 ShadeHit(RayMaterial a_mat, HitSurface a_surface, float3 a_rayDir, float a_hitDistance)
{
	// 遠くで当たったものほど細かい模様は見えないので、ミップを落として引く
	const float _lod = clamp(log2(max(a_hitDistance, 1e-3f) * 0.5f), 0.0f, 5.0f);

	Texture2D _albedoTex = ResourceDescriptorHeap[NonUniformResourceIndex(a_mat.baseIndex)];
	const float3 _albedo = _albedoTex.SampleLevel(g_samp, a_surface.uv, _lod).rgb * a_mat.baseColor.rgb;

	Texture2D _emissiveTex = ResourceDescriptorHeap[NonUniformResourceIndex(a_mat.emissiveIndex)];
	const float3 _emissive = _emissiveTex.SampleLevel(g_samp, a_surface.uv, _lod).rgb * a_mat.emissive + a_mat.emissiveAdd;

	// 金属度・粗さ : GBuffer と同じく テクスチャ(g = 粗さ / b = 金属度) * マテリアルの値
	Texture2D _metaRoughTex = ResourceDescriptorHeap[NonUniformResourceIndex(a_mat.metaRoughnessIndex)];
	const float3 _metaRough = _metaRoughTex.SampleLevel(g_samp, a_surface.uv, _lod).rgb;
	const float _roughness = _metaRough.g * a_mat.roughness;
	const float _metallic = _metaRough.b * a_mat.metallic;

	// 拡散と鏡面の割合。視線はレイの逆向き(当たった点から反射元へ)
	const float3 _F0 = lerp(DIELECTRIC_F0.xxx, _albedo, _metallic);
	const float _NdotV = saturate(dot(a_surface.normal, -a_rayDir));
	const float3 _specularWeight = EnvBRDFApprox(_F0, _roughness, _NdotV);
	const float3 _diffuseWeight = (1.0f - _specularWeight) * (1.0f - _metallic);

	// 主光源
	float3 _direct = float3(0.0f, 0.0f, 0.0f);
	if (g_sun.enable != 0)
	{
		const float3 _toSun = normalize(-g_sun.dir);
		const float _NdotL = saturate(dot(a_surface.normal, _toSun));

		float _visibility = 1.0f;
		if (_NdotL > 0.0f && g_reflection.isShadow != 0)
		{
			// 押し出し量はカメラからの距離に比例させる(遠いほどワールド座標の精度が粗い)
			const float _bias = max(0.005f, length(g_camera.cameraPos.xyz - a_surface.pos) * 0.001f);
			_visibility = TraceSunVisibility(a_surface.pos + a_surface.geoNormal * _bias, _toSun, _bias);
		}

		_direct = (_NdotL / PI) * g_sun.color.rgb * g_sun.brightness * _visibility;
	}

	const float3 _diffuse = _albedo * (_direct + g_ambient.ambientColor) * _diffuseWeight;
	const float3 _specular = g_ambient.ambientColor * _specularWeight;
	const float3 _color = _diffuse + _specular + _emissive;

	// マイナスや無限大(NaN)を後段へ流さない
	return clamp(_color, 0.0f, 10.0f);
}

//==========================================================================================
// 鏡面反射を求める
//==========================================================================================
[RootSignature(RAY_REFLECTION_RS)]
[numthreads(8, 8, 1)]
void CSMain( uint3 DTid : SV_DispatchThreadID )
{
	// ピクセルが出力画面内かどうか調べる
	uint _width, _height;
	g_outTex.GetDimensions(_width,_height);
	if (DTid.x >= _width || DTid.y >= _height) return;

	// ピクセル位置
	const uint2 _id = DTid.xy;

	// 出力は固定解像度なので、出力画素の中心が指す GBuffer の画素を引く
	uint _fullWidth, _fullHeight;
	g_depthTex.GetDimensions(_fullWidth, _fullHeight);
	const float2 _fullResDim = float2(_fullWidth, _fullHeight);

	const float2 _uv = (float2(_id) + 0.5f) / float2(_width, _height);
	const int2 _fullId = int2(min(uint2(_uv * _fullResDim), uint2(_fullWidth - 1, _fullHeight - 1)));

	// 深度値を取得する
	float _depth = g_depthTex.Load(int3(_fullId, 0));

	// 背景ピクセルは反射しない
	if(_depth >= 1.0f)
	{
		g_outTex[_id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	// ワールド座標とワールドノーマルを復元
	float3 _worldPos = ReconstructWorldPos(_fullId, _fullResDim, _depth);
	float3 _normal = DecsodeNormal(g_normalTex.Load(int3(_fullId, 0)).rg);
	float3 _geoNormal = ReconstructGeometryNormal(g_depthTex, _fullId, _fullResDim, _depth, _worldPos, _normal);

	// カメラから表面へ向かう方向とは逆向きのベクトル
	float3 _V = normalize(g_camera.cameraPos.xyz - _worldPos);

	// カメラから表面へ向かうレイを法線で反射
	float3 _R = normalize(reflect(-_V, _normal));

	// 法線マップで傾いた法線だと、反射方向がポリゴン平面より下を向くことがある。
	// そのまま飛ばすと自分に当たるので、平面で折り返して上へ向ける
	if (dot(_R, _geoNormal) < 0.0f)
	{
		_R = normalize(reflect(_R, _geoNormal));
	}

	// 表面からの自己交差を防ぐため、ポリゴン平面の向きへ押し出す。
	// 深度から戻した座標は遠いほど誤差が乗るので、押し出し量は距離に比例させる
	const float _bias = max(0.01f, length(g_camera.cameraPos.xyz - _worldPos) * 0.002f);

	RayDesc _ray;
	_ray.Origin = _worldPos + _geoNormal * _bias;
	_ray.Direction = _R;
	_ray.TMin = _bias;
	_ray.TMax = g_reflection.maxDistance;

	// レイ発射
	// BLAS はすべて不透明で作っているので、候補の判定(アルファテスト)は要らない
	RayQuery<RAY_FLAG_FORCE_OPAQUE> _query;
	_query.TraceRayInline(
		g_raytracingWorld,
		RAY_FLAG_NONE,
		0xFF,
		_ray
	);

	// 候補ヒットを評価する(不透明だけなので1回で終わる)
	while (_query.Proceed())
	{
	}

	// 不透明三角形とヒットしたのなら
	if (_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		// ヒットした三角形の情報を取得できる
		uint _instanceID = _query.CommittedInstanceID();
		uint _geometryID = _query.CommittedGeometryIndex();
		uint _primitiveID = _query.CommittedPrimitiveIndex();

		float _hitDistance = _query.CommittedRayT();
		float2 _barycentrics = _query.CommittedTriangleBarycentrics();

		// ヒット先のマテリアルと法線を取得し、その地点の放射輝度を取得する
		RayInstanceData _inst = g_instanceBuffer[_instanceID];
		RayMaterial _mat = g_materialBuffer[_inst.materialOffset + _geometryID];

		HitSurface _surface = BuildHitSurface(
			_inst, _mat, _primitiveID, _barycentrics,
			_query.CommittedObjectToWorld3x4(), _query.CommittedWorldToObject3x4());

		// 裏面に当たったら(両面のポリゴンや閉じていないメッシュ)、レイの来た側へ向ける
		if (dot(_surface.geoNormal, _R) > 0.0f)
		{
			_surface.geoNormal = -_surface.geoNormal;
			_surface.normal = -_surface.normal;
		}

		float3 _reflectedRadiance = ShadeHit(_mat, _surface, _R, _hitDistance);

		// 反射色を出力
		g_outTex[_id] = float4(_reflectedRadiance,1.0f);
	}
	else
	{
		// 環境マップやスカイの放射輝度を取得する
		float3 _environmentRadiance = SampleEnvironment(_R);
		g_outTex[_id] = float4(_environmentRadiance, 0.0f);
	}
}
