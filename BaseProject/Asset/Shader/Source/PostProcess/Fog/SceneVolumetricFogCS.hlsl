//==========================================================================================
//
// SceneVolumetricFogCS
//
// カメラから見えている面(空なら maxDistance 先)までのレイに沿って、
// 2つの媒質を積分する。
//   ・シーンのフォグ   : シーン全体に一様に漂う(SceneFogData)
//   ・グラウンドダスト : 地面から一定の高さまで漂うチリ(GroundDustData)
// 出力 : rgb = フォグの色(2つの媒質の色を濃さで混ぜたもの) / a = フォグの濃さ(0..1)
//
// 媒質は環境光と平行光(主光源)で照らす。
//   届く光 = (環境光 + 平行光 × 位相関数 × 影) × lightScale
//   媒質の色 = 媒質の色(fogColor / dustColor) × 届く光
// 平行光の影はレイに沿って引く。窓などから光の筋が差す。求め方はシーンの設定に合わせる。
//   シャドウマップ : ShadowMapPass のシャドウマップ(CSM)を引く
//   レイトレ       : RaytracingVolumeShadowPass が作った、視線に沿った日なたの割合(VolumeShadow)を使う
//                    (フォグの中ではレイを飛ばさない。割合は範囲の中で一様として扱う)。
//                    VolumeShadow は低解像度なので、近くの4画素を「歩いた範囲の終わり」が
//                    自分の範囲に近いものほど重く混ぜて引き伸ばす(手前の物体の縁で背景の割合がにじまない)
// 影が届く範囲(シャドウマップは最後のカスケードの奥、レイトレは VolumeShadow の g)より先は
// 遮るものが無いので、光は一定になる。
//
// 演出の点光源(1つだけ。FogPointLightData)も媒質を照らす。光の届く球とレイが重なる区間だけを歩く。
//   ・点光源の光は光源に近いほど強い(逆二乗)ので、歩く位置は光源に近いところへ集める
//     (等角サンプリング : 光源から見た角度で等分する)。歩数が少なくても光源の周りが粗くならない
//   ・影は1歩ごとに光源へレイを1本飛ばす(インラインレイトレ。TLAS はパスが張る)。
//     体の隙間から光の筋が全方位へこぼれる
//   ・照らすのはシーンのフォグだけで、ダストは照らさない。
//     ダストは地面の近くに濃く溜まっているので、影を引かずに照らすと画面の下半分が一面に光ってしまい、
//     1歩ごとに影のレイを飛ばすと層の中は歩数が多くて重い。地面の照り返しは面の点光源に任せる
//
// 点光源の届いた光は、媒質の照らされ方(環境光・平行光)に掛ける lightScale を掛けない。
// 強さは点光源の intensity だけで決める。
//
// シーンのフォグは一様なので、光が一定の区間は式で一度に求める。
// レイマーチするのは、影の届く範囲とダストの層の中だけ(歩数をそこへ集めるため)。
// 1歩目の位置は画素ごと・フレームごとにずらし、残る縞は TAA に均させる。
// 手前で透過率がほぼ 0 になったら、その奥は見えないので歩くのをやめる(MIN_TRANSMITTANCE)。
//
// ・ダストは見えている地面の画素にだけ置く
//     シーンの深度と地面だけの深度を比べ、見えている面が地面そのものの画素だけにダストを積分する。
//     高さの基準はその地面の点。
//     手前に物体がある画素・空の画素にはダストを置かない(シーンのフォグだけ)。
//     物体の真下の地面などは推測しないので、物体の後ろに何が映っているかでダストが変わることはない
// ・衝撃でチリが払われる・波頭へ寄せられる量は、GroundFieldPass が書いた
//   真上からのテクスチャを引く。波頭では寄せられたぶんだけ層が高くなる(巻き上がり)
//
//==========================================================================================
#include "../../../Common/RootSignatureLayout.hlsli"

#include "../../../Common/RootParameters/CameraData.hlsli"
#include "../../../Common/RootParameters/GroundFieldData.hlsli"
#include "../../../Common/RootParameters/SceneFogData.hlsli"
#include "../../../Common/RootParameters/GroundDustData.hlsli"
#include "../../../Common/RootParameters/AmbientData.hlsli"
#include "../../../Common/RootParameters/ShadowMapData.hlsli"
#include "../../../Common/RootParameters/LightData.hlsli"
#include "../../../Common/Lighting/FogResolution.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)            カメラ
//   1 : CBV(b1)            シーンのフォグの調整値
//   2 : CBV(b2)            グラウンドダストの調整値
//   3 : SRVの番号(t0-t4) シーンの深度 + 地面だけの深度(任意) + グラウンドフィールド(任意)
//                          + シャドウマップ(任意) + レイトレの影(任意)(レンダーグラフが張る)
//   4 : UAVの番号(u0)    フォグ(レンダーグラフが張る)
//   5 : SRVの番号(t4)    ノイズテクスチャ(パスが張る)
//   6 : CBV(b3)            環境光(AmbientData)
//   7 : CBV(b4)            主光源のシャドウマップ(カスケードの行列・区切り・バイアス)
//   8 : CBV(b5)            主光源(平行光の向き・色・強さ)
//   9 : CBV(b6)            演出の点光源(FogPointLightData)
//  10 : SRV(t0)            TLAS(点光源の影。パスが張る。isShadowValid が 0 なら中身は読まない)
//
// サンプラー
//   s0 : ノイズ・グラウンドフィールド用(WRAP)
//   s1 : シャドウマップ用の比較サンプラー
//
// 追加は必ず末尾へ足すこと。間に挟むと既存の番号が全部ずれる
//==========================================================================================
#define SCENE_VOLUMETRIC_FOG_RS \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
"CBV(b0, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b1, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b2, visibility = SHADER_VISIBILITY_ALL)," \
"RootConstants(num32BitConstants=5, b100), " \
"RootConstants(num32BitConstants=1, b101), " \
"RootConstants(num32BitConstants=1, b102), " \
"CBV(b3, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b4, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b5, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b6, visibility = SHADER_VISIBILITY_ALL)," \
"SRV(t0)," \
RS_STATIC_SAMPLER "," \
"StaticSampler(s1, " \
"    filter = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, " \
"    addressU = TEXTURE_ADDRESS_CLAMP, " \
"    addressV = TEXTURE_ADDRESS_CLAMP, " \
"    addressW = TEXTURE_ADDRESS_CLAMP, " \
"    comparisonFunc = COMPARISON_LESS_EQUAL)"

// 張られていないときの番号(未接続の入力・未設定のノイズ。パス側と合わせる)
#define DESCRIPTOR_INDEX_NONE 0xFFFFFFFF

// ダストの層の中をレイマーチする歩数の上限。
// 層の中が長いほど歩数が増えるので、ここで頭打ちにして1歩を伸ばす
#define GROUND_DUST_MAX_STEPS 64

// 影の届く範囲を歩く歩数(区間1つあたり)。
// 区間の長さによらず同じ歩数なので、レイが短い(屋内など)ほど1歩が細かくなる。
// 歩くのはシャドウマップの影だけ(レイトレの影は割合が届くので歩かない)
#define SUN_SHADOW_STEPS 32

// 透過率がこれを切ったら積分をやめる。
// 奥から届く光はこの割合より小さく、足しても見た目は変わらない。
// 出力の色は濃さで割り戻すので打ち切っても色はずれず、濃さ(a)が最大でこのぶん薄くなるだけ
#define MIN_TRANSMITTANCE 0.01f

// 影の求め方
#define SUN_SHADOW_NONE       0	// 影を引かない(平行光は遮られない)
#define SUN_SHADOW_SHADOW_MAP 1	// シャドウマップ(CSM)
#define SUN_SHADOW_RAYTRACING 2	// レイトレ(RaytracingVolumeShadowPass の VolumeShadow)

// 波頭でチリが巻き上がる高さの上限(height の何倍まで)。
// レイはこの高さで切るので、上げすぎると歩数が層の外で無駄になる
#define GROUND_DUST_MAX_LIFT 2.0f

// 見えている面が地面そのものかを判定するときの許容差。
// シーンの深度と地面だけの深度は別のパスで描くので、同じ地面でもわずかにずれることがある。
// 地面までの距離に対する割合と、近いところ用の下限(m)のうち大きいほうを使う
#define GROUND_PIXEL_TOLERANCE_RATE 0.002f
#define GROUND_PIXEL_TOLERANCE_MIN  0.05f

// レイトレの影(低解像度)を引き伸ばすときの、範囲の終わりの許容差。
// 自分の範囲に対する割合と、近いところ用の下限(m)のうち大きいほうを使う
#define VOLUME_SHADOW_RANGE_TOLERANCE_RATE 0.1f
#define VOLUME_SHADOW_RANGE_TOLERANCE_MIN  0.1f

// 点光源の影を引かないとき(歩数 0 / ワールドが組めていない)に、光の届く区間を歩く歩数。
// 等角サンプリングなので、遮るものが無ければ少ない歩数でほぼ正しく求まる
#define POINT_LIGHT_UNSHADOWED_STEPS 8

// 点光源の影を引くときの歩数の上限(積む側の値をここで頭打ちにする)
#define POINT_LIGHT_MAX_SHADOW_STEPS 128

// 出力の色の上限。色は濃さで割り戻すので、点光源が強いと半精度(R16G16B16A16_FLOAT)の上限を超えることがある
#define FOG_COLOR_MAX 60000.0f

// シャドウマップのアトラスの1辺あたりのタイル数(ShadowMapMaskCS と合わせる)
static const uint kShadowAtlasTiles = 2;

cbuffer CBCamera : register(b0)
{
	CameraData g_camera;
}

cbuffer CBSceneFog : register(b1)
{
	SceneFogData g_sceneFog;
}

cbuffer CBGroundDust : register(b2)
{
	GroundDustData g_groundDust;
}

cbuffer CBAmbient : register(b3)
{
	AmbientData g_ambient;
}

cbuffer CBShadowMap : register(b4)
{
	ShadowMapData g_shadow;
}

cbuffer CBSunLight : register(b5)
{
	SunLightData g_sun;
}

cbuffer CBPointLight : register(b6)
{
	FogPointLightData g_pointLight;
}

// 点光源の影を引くレイトレのワールド
RaytracingAccelerationStructure g_raytracingWorld : register(t0);

// 入力
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_sceneDepthTexIndex;
	uint g_groundDepthTexIndex;		// 任意 : 未接続なら DESCRIPTOR_INDEX_NONE(ダストは出ない)
	uint g_groundFieldTexIndex;		// 任意 : 未接続なら DESCRIPTOR_INDEX_NONE(衝撃でチリが動かない)
	uint g_shadowMapIndex;			// 任意 : 未接続なら DESCRIPTOR_INDEX_NONE(シャドウマップの影は引かない)
	uint g_volumeShadowIndex;		// 任意 : 未接続なら DESCRIPTOR_INDEX_NONE(レイトレの影は引かない)
}

Texture2D<float> Get_sceneDepthTex() { Texture2D<float> _r = ResourceDescriptorHeap[g_sceneDepthTexIndex]; return _r; }		// シーン全体の深度(レイの終点)
#define g_sceneDepthTex Get_sceneDepthTex()
Texture2D<float> Get_groundDepthTex() { Texture2D<float> _r = ResourceDescriptorHeap[g_groundDepthTexIndex]; return _r; }	// 地面だけの深度(ダストの高さの基準)
#define g_groundDepthTex Get_groundDepthTex()
Texture2D<float2> Get_groundFieldTex() { Texture2D<float2> _r = ResourceDescriptorHeap[g_groundFieldTexIndex]; return _r; }	// グラウンドフィールド(r = 残ったチリ / g = 寄せられたチリ)
#define g_groundFieldTex Get_groundFieldTex()
Texture2D<float> Get_shadowMap() { Texture2D<float> _r = ResourceDescriptorHeap[g_shadowMapIndex]; return _r; }			// 主光源のシャドウマップ(カスケードを 2x2 に並べたアトラス)
#define g_shadowMap Get_shadowMap()
Texture2D<float2> Get_volumeShadowTex() { Texture2D<float2> _r = ResourceDescriptorHeap[g_volumeShadowIndex]; return _r; }	// レイトレの影(r = 日なたの割合 / g = 範囲の終わり)。低解像度
#define g_volumeShadowTex Get_volumeShadowTex()

// 出力
// UAVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex1 : register(b101)
{
	uint g_outTexIndex;
}

RWTexture2D<float4> Get_outTex() { RWTexture2D<float4> _r = ResourceDescriptorHeap[g_outTexIndex]; return _r; }
#define g_outTex Get_outTex()

// ノイズ
// グラフのリソースではないので、パスが自分で番号を渡す。未設定なら DESCRIPTOR_INDEX_NONE
cbuffer PassDescriptorIndex2 : register(b102)
{
	uint g_noiseTexIndex;
}

Texture2D<float4> Get_noiseTex() { Texture2D<float4> _r = ResourceDescriptorHeap[g_noiseTexIndex]; return _r; }	// ノイズ(r)
#define g_noiseTex Get_noiseTex()

// サンプラー
SamplerState g_samp : register(s0);
SamplerComparisonState g_shadowSamp : register(s1);

//------------------------------------------------------------------------------------------
// レイ1本ぶんの光の条件。画素ごとに一度だけ求めて、各区間の積分へ渡す
//------------------------------------------------------------------------------------------
struct FogLighting
{
	float3 ambient;		// 環境光
	float3 sun;			// 平行光 × 位相関数(平行光なので、レイの向きが決まれば一定)
	uint shadowMethod;	// 影の求め方(SUN_SHADOW_*)
	float shadowEnd;	// 影の届く範囲の終わり(カメラからの距離)
	float rayVisibility;	// レイトレの影 : 範囲の中の日なたの割合
	float viewZPerDistance;	// レイを 1m 進んだときのビュー空間の奥行きの増え方
	float shadowTileSize;	// カスケード1枚ぶんのタイルの解像度
	float jitter;		// 歩く位置のずらし(0..1)

	// 演出の点光源 : 光の届く球とレイが重なる区間(カメラからの距離)。重ならなければ start >= end
	float pointStart;
	float pointEnd;
	float pointClosestT;		// レイの上で光源に一番近い点(カメラからの距離)
	float pointClosestDist;	// その点と光源の距離に、減衰の分母の +1 を足したもの(sqrt(d^2 + 1))
	uint pointSteps;		// 区間を歩く歩数
	bool isPointShadow;		// 1歩ごとに影のレイを飛ばすか
};

// 画素の UV と深度からワールド座標を戻す
float3 ReconstructWorldPos(float2 a_uv, float a_depth)
{
	float4 _ndc = float4(a_uv.x * 2.0f - 1.0f, 1.0f - a_uv.y * 2.0f, a_depth, 1.0f);
	float4 _worldPos = mul(_ndc, g_camera.invViewProj);
	return _worldPos.xyz / _worldPos.w;
}

// 画素とフレームで変わる 0..1 のずらし(Interleaved Gradient Noise)
float InterleavedGradientNoise(float2 a_pixel, float a_frame)
{
	a_pixel += 5.588238f * a_frame;
	return frac(52.9829189f * frac(dot(a_pixel, float2(0.06711056f, 0.00583715f))));
}

// 位相関数(Henyey-Greenstein)。
// 全方向に同じ(g = 0)とき 1 になるよう 4π を掛けてある。
// こうしておくと、環境光と平行光を同じ物差しで足せる(光源の方を向いた面と同じくらい明るい)
//   a_cos : 光の進む向きと、カメラへ向かう向きのなす角の cos(1 = 光源の方を見ている)
float PhaseHenyeyGreenstein(float a_cos, float a_g)
{
	const float _g2 = a_g * a_g;
	return (1.0f - _g2) / pow(max(1.0f + _g2 - 2.0f * a_g * a_cos, 1e-4f), 1.5f);
}

// 水平位置(xz)のグラウンドフィールドを引く。
//   x = 払われずに残ったチリの量 / y = 波頭に寄せられたチリの量
// 未接続・範囲の外は「衝撃なし」(1, 0)
float2 SampleGroundField(float2 a_posXZ, float2 a_center, float a_halfTexel)
{
	if (g_groundFieldTexIndex == DESCRIPTOR_INDEX_NONE) return float2(1.0f, 0.0f);

	float2 _uv = GroundFieldWorldToUV(a_posXZ, a_center);
	if (any(_uv < 0.0f) || any(_uv > 1.0f)) return float2(1.0f, 0.0f);

	// サンプラーは WRAP(ノイズ用)なので、縁で反対側を拾わないよう半テクセル内側へ寄せる
	_uv = clamp(_uv, a_halfTexel, 1.0f - a_halfTexel);
	return g_groundFieldTex.SampleLevel(g_samp, _uv, 0);
}

// 主光源の影 : 1 = ひなた / 0 = 影。
// 媒質は面ではないので、法線方向へのずらしはせず、比較サンプラー1回(2x2 のバイリニア)だけで引く。
// どのカスケードを引くかは ShadowMapMaskCS と同じく、ビュー空間の奥行きで決める
float SampleSunShadow(float3 a_worldPos, float a_viewZ, float a_tileSize)
{
	uint _cascade = g_shadow.cascadeCount;
	for (uint _i = 0; _i < g_shadow.cascadeCount; ++_i)
	{
		if (a_viewZ <= g_shadow.cascadeFar[_i])
		{
			_cascade = _i;
			break;
		}
	}
	if (_cascade >= g_shadow.cascadeCount) return 1.0f;

	float4 _lightClip = mul(float4(a_worldPos, 1.0f), g_shadow.lightViewProj[_cascade]);
	float3 _ndc = _lightClip.xyz / _lightClip.w;

	// 箱の外は描かれていない
	if (_ndc.z >= 1.0f) return 1.0f;
	float2 _uv = float2(_ndc.x * 0.5f + 0.5f, 0.5f - _ndc.y * 0.5f);
	if (any(_uv < 0.0f) || any(_uv > 1.0f)) return 1.0f;

	// バイアスはワールドの長さで持っているので、箱の奥行きで割って射影後の値にする
	float _compareDepth = _ndc.z - g_shadow.depthBias / g_shadow.cascadeDepthRange[_cascade];

	// タイルの中だけを引く(端まで行くとバイリニアが隣のカスケードを拾う)
	float _texel = 1.0f / a_tileSize;
	float2 _tileUV = clamp(_uv, _texel, 1.0f - _texel);
	float2 _atlasUV = (float2(_cascade % kShadowAtlasTiles, _cascade / kShadowAtlasTiles) + _tileUV) / kShadowAtlasTiles;

	return g_shadowMap.SampleCmpLevelZero(g_shadowSamp, _atlasUV, _compareDepth);
}

// レイトレの影(低解像度)を、この画素の位置へ引き伸ばす。
//   a_rangeEnd : この画素の影の届く範囲の終わり(カメラからの距離)
// 近くの4画素をバイリニアの重みで混ぜるが、歩いた範囲の終わり(g)が自分の範囲と違う画素は軽くする。
// 範囲の終わりは見えている面までの距離なので、手前の物体と背景の割合が縁で混ざらない。
// 4画素とも範囲 0(レイを飛ばしていない)なら false
bool SampleRayVisibility(float2 a_uv, float a_rangeEnd, out float a_outVisibility)
{
	a_outVisibility = 1.0f;

	uint _width, _height;
	g_volumeShadowTex.GetDimensions(_width, _height);

	const float2 _pos = a_uv * float2(_width, _height) - 0.5f;
	const int2 _base = int2(floor(_pos));
	const float2 _frac = _pos - float2(_base);
	const int2 _maxId = int2(_width, _height) - 1;

	const float _tolerance = max(a_rangeEnd * VOLUME_SHADOW_RANGE_TOLERANCE_RATE, VOLUME_SHADOW_RANGE_TOLERANCE_MIN);

	float _sum = 0.0f;
	float _sumWeight = 0.0f;
	float _fallback = 1.0f;			// どれも重みが付かないとき用 : 範囲が一番近い画素の割合
	float _fallbackDiff = 1e30f;
	bool _isValid = false;

	[unroll]
	for (int _y = 0; _y < 2; ++_y)
	{
		[unroll]
		for (int _x = 0; _x < 2; ++_x)
		{
			const int2 _id = clamp(_base + int2(_x, _y), int2(0, 0), _maxId);
			const float2 _sample = g_volumeShadowTex.Load(int3(_id, 0));
			if (_sample.y <= 0.0f) continue;	// レイを飛ばしていない画素
			_isValid = true;

			const float _diff = abs(_sample.y - a_rangeEnd);
			if (_diff < _fallbackDiff)
			{
				_fallbackDiff = _diff;
				_fallback = _sample.x;
			}

			const float _bilinear = (_x == 0 ? 1.0f - _frac.x : _frac.x) * (_y == 0 ? 1.0f - _frac.y : _frac.y);
			const float _weight = _bilinear * exp(-_diff / _tolerance);
			_sum += _sample.x * _weight;
			_sumWeight += _weight;
		}
	}

	if (!_isValid) return false;

	a_outVisibility = (_sumWeight > 1e-4f) ? (_sum / _sumWeight) : _fallback;
	return true;
}

// レイの上の距離 a_t の点に届く光
float3 CalcLightAt(FogLighting a_light, float3 a_rayStart, float3 a_rayDir, float a_t)
{
	float _visibility = 1.0f;
	if (a_light.shadowMethod != SUN_SHADOW_NONE && a_t < a_light.shadowEnd)
	{
		if (a_light.shadowMethod == SUN_SHADOW_SHADOW_MAP)
		{
			_visibility = SampleSunShadow(a_rayStart + a_rayDir * a_t, a_t * a_light.viewZPerDistance, a_light.shadowTileSize);
		}
		else
		{
			_visibility = a_light.rayVisibility;
		}
	}
	return a_light.ambient + a_light.sun * _visibility;
}

// 点光源が見えるか : 1 = 届く / 0 = 遮られる。
// 当たったかどうかだけ分かればよいので、最初に当たった時点で打ち切る
float TracePointLightVisibility(float3 a_origin, float3 a_toLight, float a_distance)
{
	RayDesc _ray;
	_ray.Origin = a_origin;
	_ray.Direction = a_toLight;
	_ray.TMin = 0.001f;
	_ray.TMax = a_distance;

	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> _query;
	_query.TraceRayInline(g_raytracingWorld, RAY_FLAG_NONE, 0xFF, _ray);

	// 全部不透明扱いなので、候補を自分で判定する必要は無い(1回で走査が終わる)
	_query.Proceed();

	return (_query.CommittedStatus() == COMMITTED_NOTHING) ? 1.0f : 0.0f;
}

// 点光源から a_pos に届く光(減衰と位相関数込み)。
// 減衰はディファードのポイントライトと同じ式(逆二乗に「range で 0 になる窓」を掛ける)なので、
// 同じ強さを面の点光源へ渡すと、面とフォグが同じくらいに照らされる
float3 CalcPointLightAt(float3 a_pos, float3 a_rayDir, bool a_isShadow)
{
	const float3 _toLight = g_pointLight.position - a_pos;
	const float _distSq = dot(_toLight, _toLight);
	if (_distSq >= g_pointLight.range * g_pointLight.range) return 0.0f;

	const float _dist = sqrt(_distSq);
	const float3 _dirToLight = (_dist > 1e-4f) ? _toLight / _dist : float3(0.0f, 1.0f, 0.0f);

	const float _window = saturate(1.0f - pow(_dist / g_pointLight.range, 4.0f));
	const float _atten = (_window * _window) / (_distSq + 1.0f);

	// 光の進む向き(光源 → この点)と、カメラへ向かう向きのなす角
	const float _phase = PhaseHenyeyGreenstein(dot(-_dirToLight, -a_rayDir), g_sceneFog.anisotropy);

	float _visibility = 1.0f;
	if (a_isShadow) _visibility = TracePointLightVisibility(a_pos, _dirToLight, _dist);

	return g_pointLight.color * g_pointLight.intensity * _atten * _phase * _visibility;
}

// シーンのフォグだけが漂う区間 [a_start, a_end] のうち、点光源の届くところで散る光を足す。
// 透過率は区間の入り口の値から、一様な濃さで減っていくものとして求める(透過率そのものは変えない)。
//
// 歩く位置は等角サンプリング : 光源から見た角度を等分して、その角度の位置を歩く。
// 光源に近いところほど細かく歩くので、逆二乗で強くなる光源の周りを少ない歩数で拾える。
//   t     = 一番近い点 + D × tan(θ)
//   確率密度 = D / ((θb - θa) × (D^2 + (t - 一番近い点)^2))
// D に減衰の分母の +1 を含めてあるので、確率密度は減衰と同じ形になり、遮るものが無ければほぼ一定の値を足すだけになる
void IntegratePointLight(FogLighting a_light, float3 a_rayStart, float3 a_rayDir, float a_start, float a_end,
	inout float3 a_inoutColor, float a_transmittance)
{
	const float _start = max(a_start, a_light.pointStart);
	const float _end = min(a_end, a_light.pointEnd);
	if (_end <= _start || a_light.pointSteps == 0) return;

	const float _d = a_light.pointClosestDist;
	const float _thetaA = atan((_start - a_light.pointClosestT) / _d);
	const float _thetaB = atan((_end - a_light.pointClosestT) / _d);
	const float _thetaRange = _thetaB - _thetaA;
	if (_thetaRange <= 1e-6f) return;

	float3 _sum = 0.0f;
	for (uint _i = 0; _i < a_light.pointSteps; ++_i)
	{
		const float _theta = _thetaA + _thetaRange * ((_i + a_light.jitter) / (float) a_light.pointSteps);
		const float _offset = _d * tan(_theta);
		const float _t = clamp(a_light.pointClosestT + _offset, _start, _end);

		// 1歩ぶんの長さ = 1 / (歩数 × 確率密度)
		const float _stepLength = _thetaRange * (_d * _d + _offset * _offset) / (_d * (float) a_light.pointSteps);

		const float _transmittance = a_transmittance * exp(-g_sceneFog.density * (_t - a_start));
		_sum += _transmittance * _stepLength * CalcPointLightAt(a_rayStart + a_rayDir * _t, a_rayDir, a_light.isPointShadow);
	}

	a_inoutColor += g_sceneFog.density * g_sceneFog.fogColor * _sum;
}

// 光が一定の区間(長さ a_length)を、シーンのフォグだけで式で一度に積分する
void IntegrateSceneFogConstant(float a_length, float3 a_light, inout float3 a_inoutColor, inout float a_inoutTransmittance)
{
	if (a_length <= 0.0f) return;

	const float _transmittance = exp(-g_sceneFog.density * a_length);
	a_inoutColor += a_inoutTransmittance * g_sceneFog.fogColor * a_light * (1.0f - _transmittance);
	a_inoutTransmittance *= _transmittance;
}

// シーンのフォグだけが漂う区間 [a_start, a_end] を積分する。
// 濃さは一様なので、光が一定のところは式で一度に求める。
//   シャドウマップ : 影の届く範囲の中だけ、影を引きながら歩く
//   レイトレ       : 範囲の中は日なたの割合で一定なので、範囲の中と外の2回の式で済む
void IntegrateSceneFog(FogLighting a_light, float3 a_rayStart, float3 a_rayDir, float a_start, float a_end,
	inout float3 a_inoutColor, inout float a_inoutTransmittance)
{
	if (a_end <= a_start || g_sceneFog.density <= 0.0f) return;
	if (a_inoutTransmittance < MIN_TRANSMITTANCE) return;	// 手前でもう見えなくなっている

	// 演出の点光源 : 光が一定にならないので別に歩く(透過率は入り口の値から求めるので、先に足しておく)
	IntegratePointLight(a_light, a_rayStart, a_rayDir, a_start, a_end, a_inoutColor, a_inoutTransmittance);

	// 影の届く範囲の中 : シャドウマップは歩く
	const float _marchEnd = (a_light.shadowMethod == SUN_SHADOW_SHADOW_MAP) ? min(a_end, a_light.shadowEnd) : a_start;
	if (_marchEnd > a_start)
	{
		const float _stepLength = (_marchEnd - a_start) / (float) SUN_SHADOW_STEPS;
		const float _stepTransmittance = exp(-g_sceneFog.density * _stepLength);

		for (uint _i = 0; _i < SUN_SHADOW_STEPS; ++_i)
		{
			const float _t = a_start + (_i + a_light.jitter) * _stepLength;
			const float3 _light = CalcLightAt(a_light, a_rayStart, a_rayDir, _t);

			a_inoutColor += a_inoutTransmittance * g_sceneFog.fogColor * _light * (1.0f - _stepTransmittance);
			a_inoutTransmittance *= _stepTransmittance;

			// ここより奥は見えない。残りの区間(一定の光の式)も足さずに抜ける
			if (a_inoutTransmittance < MIN_TRANSMITTANCE) return;
		}
	}

	float _restStart = max(a_start, _marchEnd);

	// 影の届く範囲の中 : レイトレは日なたの割合で一定
	if (a_light.shadowMethod == SUN_SHADOW_RAYTRACING)
	{
		const float _rangeEnd = min(a_end, a_light.shadowEnd);
		IntegrateSceneFogConstant(_rangeEnd - _restStart,
			a_light.ambient + a_light.sun * a_light.rayVisibility, a_inoutColor, a_inoutTransmittance);
		_restStart = max(_restStart, _rangeEnd);
	}

	// 残り : 遮るものが無いので光は一定
	IntegrateSceneFogConstant(a_end - _restStart, a_light.ambient + a_light.sun, a_inoutColor, a_inoutTransmittance);
}

[RootSignature(SCENE_VOLUMETRIC_FOG_RS)]
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
	// 出力画像の解像度を取得
	uint _width, _height;
	g_outTex.GetDimensions(_width, _height);

	// 画面外チェック
	if (DTid.x >= _width || DTid.y >= _height) return;

	int2 _coord = int2(DTid.xy);	// 出力(フォグ)の画素

	// 出力は描画解像度より小さいことがある(パスの Resolution)。
	// レイは、この画素が覆う範囲の中から代表の1画素を選んで、その画素を通す。
	// 合成側(SceneFogCompositeCS)は同じ決め方で代表の深度を引き、深度の近い画素から引き伸ばす
	uint _depthWidth, _depthHeight;
	g_sceneDepthTex.GetDimensions(_depthWidth, _depthHeight);
	const int2 _depthCoord = CalcFogRepresentativePixel(_coord, uint2(_width, _height), uint2(_depthWidth, _depthHeight));

	float2 _uv = (float2(_depthCoord) + 0.5f) / float2(_depthWidth, _depthHeight);	// 代表の画素の中心を指すUV

	//------------------------------------------------------------------------------
	// レイ : カメラから見えている面まで。空なら maxDistance 先まで
	//------------------------------------------------------------------------------
	const float _sceneDepth = g_sceneDepthTex.Load(int3(_depthCoord, 0));
	const float3 _rayStart = g_camera.cameraPos.xyz;
	float3 _rayEnd;
	if (_sceneDepth < 1.0f)
	{
		_rayEnd = ReconstructWorldPos(_uv, _sceneDepth);
	}
	else
	{
		const float3 _farWorld = ReconstructWorldPos(_uv, 1.0f);
		_rayEnd = _rayStart + normalize(_farWorld - _rayStart) * g_sceneFog.maxDistance;
	}

	const float3 _rayVector = _rayEnd - _rayStart;
	const float _rayLength = length(_rayVector);

	// レイが極端に短ければ切る
	if (_rayLength <= 0.0001f)
	{
		g_outTex[_coord] = float4(g_sceneFog.fogColor, 0.0f);
		return;
	}
	const float3 _rayDir = _rayVector / _rayLength;

	//------------------------------------------------------------------------------
	// 光 : 環境光と、平行光 × 位相関数(レイの向きで決まる)
	//------------------------------------------------------------------------------
	FogLighting _light;
	_light.ambient = g_ambient.ambientColor * g_sceneFog.lightScale;
	_light.sun = 0.0f;
	_light.shadowMethod = SUN_SHADOW_NONE;
	_light.shadowEnd = 0.0f;
	_light.rayVisibility = 1.0f;
	_light.viewZPerDistance = mul(float4(_rayDir, 0.0f), g_camera.view).z;
	_light.shadowTileSize = 1.0f;
	// 1歩目のずらし : フレームごとに変えて TAA に均させる
	_light.jitter = InterleavedGradientNoise(float2(_coord), fmod(floor(g_groundDust.time * 60.0f), 64.0f));

	// 演出の点光源 : 光の届く球とレイが重なる区間
	_light.pointStart = 0.0f;
	_light.pointEnd = 0.0f;
	_light.pointClosestT = 0.0f;
	_light.pointClosestDist = 1.0f;
	_light.pointSteps = 0;
	_light.isPointShadow = false;
	if (g_pointLight.enable != 0 && g_pointLight.intensity > 0.0f && g_pointLight.range > 0.0f)
	{
		const float3 _toLight = g_pointLight.position - _rayStart;
		const float _closestT = dot(_toLight, _rayDir);
		const float _closestDistSq = max(dot(_toLight, _toLight) - _closestT * _closestT, 0.0f);
		const float _halfChordSq = g_pointLight.range * g_pointLight.range - _closestDistSq;
		if (_halfChordSq > 0.0f)
		{
			const float _halfChord = sqrt(_halfChordSq);
			_light.pointStart = max(_closestT - _halfChord, 0.0f);
			_light.pointEnd = min(_closestT + _halfChord, _rayLength);
			_light.pointClosestT = _closestT;
			_light.pointClosestDist = sqrt(_closestDistSq + 1.0f);

			_light.isPointShadow = (g_pointLight.isShadowValid != 0) && (g_pointLight.shadowStepCount > 0);
			_light.pointSteps = _light.isPointShadow
				? min(g_pointLight.shadowStepCount, (uint) POINT_LIGHT_MAX_SHADOW_STEPS)
				: (uint) POINT_LIGHT_UNSHADOWED_STEPS;
		}
	}

	if (g_sun.enable != 0)
	{
		const float3 _sunDir = normalize(g_sun.dir);
		const float _cos = dot(_sunDir, -_rayDir);
		_light.sun = g_sun.color.rgb * g_sun.brightness * PhaseHenyeyGreenstein(_cos, g_sceneFog.anisotropy) * g_sceneFog.lightScale;

		// 影 : 届く範囲はどちらもビュー空間の奥行きで決まっているので、レイの上の距離へ直す。
		// どちらを引くかはパスがシーンの設定に合わせて渡してくる
		//   シャドウマップ : カスケードが組まれている(影の求め方がシャドウマップのフレーム)
		//   レイトレ       : RaytracingVolumeShadowPass が視線に沿った日なたの割合を書いている(範囲 g > 0)。
		//                    影の求め方がシャドウマップのフレーム・フォグを使わないフレームは、
		//                    範囲 0(影なし)で埋められて届く
		if (_light.viewZPerDistance > 0.0001f)
		{
			if (g_shadowMapIndex != DESCRIPTOR_INDEX_NONE && g_shadow.cascadeCount > 0)
			{
				// 最後のカスケードの奥までがシャドウマップの届く範囲
				uint _mapWidth, _mapHeight;
				g_shadowMap.GetDimensions(_mapWidth, _mapHeight);

				_light.shadowMethod = SUN_SHADOW_SHADOW_MAP;
				_light.shadowEnd = g_shadow.cascadeFar[g_shadow.cascadeCount - 1] / _light.viewZPerDistance;
				_light.shadowTileSize = (float) _mapWidth / kShadowAtlasTiles;
			}
			else if (g_volumeShadowIndex != DESCRIPTOR_INDEX_NONE)
			{
				// 範囲の終わりは RaytracingVolumeShadowPass と同じ決め方で、この画素ぶんを自分で求める
				// (VolumeShadow は低解像度なので、隣の画素の g をそのまま使うと縁で範囲がずれる)
				const float _rangeEnd = min((_sceneDepth < 1.0f) ? _rayLength : 1e30f,
					g_shadow.distance / _light.viewZPerDistance);

				float _visibility;
				if (SampleRayVisibility(_uv, _rangeEnd, _visibility))
				{
					_light.shadowMethod = SUN_SHADOW_RAYTRACING;
					_light.rayVisibility = saturate(_visibility);
					_light.shadowEnd = _rangeEnd;
				}
			}
		}
	}

	//------------------------------------------------------------------------------
	// ダストを置くのは、見えている面が地面そのものの画素だけ
	//
	// 地面だけの深度(物体を描いていない)に映っている地面と、シーンの深度で見えている面が
	// 同じ距離なら地面の画素。手前に物体がある画素・空の画素はシーンのフォグだけ
	//------------------------------------------------------------------------------
	float _groundY = 0.0f;
	bool _hasGround = false;

	const bool _isUseDust = (g_groundDust.density > 0.0f) && (g_groundDepthTexIndex != DESCRIPTOR_INDEX_NONE);

	if (_isUseDust && _sceneDepth < 1.0f)
	{
		// シーンの深度と同じ点を引く(大きさが違っても UV で合わせる)
		uint _groundWidth, _groundHeight;
		g_groundDepthTex.GetDimensions(_groundWidth, _groundHeight);
		const int2 _groundCoord = min(int2(_uv * float2(_groundWidth, _groundHeight)), int2(_groundWidth, _groundHeight) - 1);

		const float _groundDepth = g_groundDepthTex.Load(int3(_groundCoord, 0));
		if (_groundDepth < 1.0f)
		{
			const float3 _groundPos = ReconstructWorldPos(_uv, _groundDepth);
			const float _groundDistance = length(_groundPos - _rayStart);
			const float _tolerance = max(_groundDistance * GROUND_PIXEL_TOLERANCE_RATE, GROUND_PIXEL_TOLERANCE_MIN);

			if (_rayLength >= _groundDistance - _tolerance)
			{
				_groundY = _groundPos.y;
				_hasGround = true;
			}
		}
	}

	//------------------------------------------------------------------------------
	// ダストの層(地面から巻き上がりの上限まで)に入っている区間 [_dustStart, _dustEnd]
	// 層に入らなければ、区間は空(全体がシーンのフォグだけ)
	//------------------------------------------------------------------------------
	float _dustStart = _rayLength;
	float _dustEnd = _rayLength;
	if (_hasGround)
	{
		const float _layerTop = _groundY + max(g_groundDust.height, 0.0001f) * GROUND_DUST_MAX_LIFT;
		if (_rayStart.y <= _layerTop || _rayEnd.y <= _layerTop)
		{
			_dustStart = 0.0f;
			if (_rayStart.y > _layerTop) _dustStart = _rayLength * (_rayStart.y - _layerTop) / (_rayStart.y - _rayEnd.y);	// 上から層へ入る
			if (_rayEnd.y > _layerTop) _dustEnd = _rayLength * (_layerTop - _rayStart.y) / (_rayEnd.y - _rayStart.y);		// 層から上へ抜ける
		}
	}

	//------------------------------------------------------------------------------
	// 積分
	//   色   : 手前から届く光の和(透過率を掛けながら足す)
	//   透過 : ここまでで残っている割合
	//------------------------------------------------------------------------------
	float3 _color = 0.0f;
	float _transmittance = 1.0f;

	// 層の手前 : シーンのフォグだけ
	IntegrateSceneFog(_light, _rayStart, _rayDir, 0.0f, _dustStart, _color, _transmittance);

	// 層の中 : シーンのフォグ + ダストを歩いて積分する
	const float _dustLength = _dustEnd - _dustStart;
	if (_dustLength > 0.0f)
	{
		uint _steps = (uint) ceil(_dustLength / max(g_groundDust.stepSize, 0.01f));
		_steps = min(_steps, (uint) GROUND_DUST_MAX_STEPS);
		const float _stepLength = _dustLength / max((float) _steps, 1.0f);

		uint _fieldWidth = 1, _fieldHeight = 1;
		if (g_groundFieldTexIndex != DESCRIPTOR_INDEX_NONE)
		{
			g_groundFieldTex.GetDimensions(_fieldWidth, _fieldHeight);
		}
		const float2 _fieldCenter = CalcGroundFieldCenter(_rayStart, (float) _fieldWidth);
		const float _fieldHalfTexel = 0.5f / (float) _fieldWidth;

		const bool _isUseNoise = (g_noiseTexIndex != DESCRIPTOR_INDEX_NONE);
		const float _dustHeight = max(g_groundDust.height, 0.0001f);

		for (uint _i = 0; _i < _steps; ++_i)
		{
			const float _t = _dustStart + (_i + _light.jitter) * _stepLength;
			const float3 _samplePos = _rayStart + _rayDir * _t;

			// 衝撃で払われた量と、波頭に寄せられた量
			const float2 _field = SampleGroundField(_samplePos.xz, _fieldCenter, _fieldHalfTexel);

			// 地面からの高さ。波頭では寄せられたぶんだけ層が高くなる(巻き上がり)
			const float _heightFromGround = _samplePos.y - _groundY;
			const float _layerHeight = _dustHeight * min(1.0f + _field.y, GROUND_DUST_MAX_LIFT);
			const float _heightMask = 1.0f - saturate(_heightFromGround / _layerHeight);

			float _dustDensity = 0.0f;
			if (_heightMask > 0.0f)
			{
				// ノイズ : 未設定なら一様(1)
				float _noise = 1.0f;
				if (_isUseNoise)
				{
					_noise = g_noiseTex.SampleLevel(g_samp, _samplePos.xz * g_groundDust.noiseScale + g_groundDust.time * 0.01f, 0).r;
				}

				// 払われずに残ったチリ + 波頭に寄せられたチリ
				_dustDensity = g_groundDust.density * _heightMask * _noise * (_field.x + _field.y);
			}

			// 2つの媒質を合わせた1歩ぶん(Beer-Lambert)。色は濃さの割合で混ぜる
			const float _density = g_sceneFog.density + _dustDensity;
			if (_density <= 0.0f) continue;

			const float _stepTransmittance = exp(-_density * _stepLength);
			const float3 _mediaColor =
				(g_sceneFog.density * g_sceneFog.fogColor + _dustDensity * g_groundDust.dustColor) / _density;

			// この点に届く光で照らす
			const float3 _lightAt = CalcLightAt(_light, _rayStart, _rayDir, _t);

			_color += _transmittance * _mediaColor * _lightAt * (1.0f - _stepTransmittance);
			_transmittance *= _stepTransmittance;

			// ここより奥は見えない(層の奥のシーンのフォグも IntegrateSceneFog が飛ばす)
			if (_transmittance < MIN_TRANSMITTANCE) break;
		}
	}

	// 層の奥 : シーンのフォグだけ
	IntegrateSceneFog(_light, _rayStart, _rayDir, _dustEnd, _rayLength, _color, _transmittance);

	//------------------------------------------------------------------------------
	// 出力 : 合成側は lerp(元の色, rgb, a) で重ねるので、色は濃さで割り戻しておく
	//------------------------------------------------------------------------------
	const float _fogAmount = 1.0f - _transmittance;
	const float3 _fogColor = (_fogAmount > 0.00001f) ? min(_color / _fogAmount, FOG_COLOR_MAX) : g_sceneFog.fogColor;

	g_outTex[_coord] = float4(_fogColor, _fogAmount);
}
