#include "../../../Common/Math/CalcNormal.hlsli"
#include "../../../Common/Math/CalcLighting.hlsli"
#include "../../../Common/Math/EnvBRDF.hlsli"
#include "../../../Common/RootSignatureLayout.hlsli"

// ルートパラメーターの構造体
#include "../../../Common/RootParameters/CameraData.hlsli"
#include "../../../Common/RootParameters/AmbientData.hlsli"
#include "../../../Common/RootParameters/LightingOptionData.hlsli"
#include "../../../Common/RootParameters/LightData.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)            カメラ
//   1 : CBV(b10)           環境光・フォグ
//   2 : SRVの番号(t0-t7) GBuffer + 影マスク + GI + 鏡面反射
//   3 : UAVの番号(u0)    出力カラー
//   4 : CBV(b11)           ライティング調整値
//   5 : SRVの番号(t7-t8) ポイントライト配列 + 平行光配列
//   6 : CBV(b12)           ライト数
//
// 追加は必ず末尾へ足すこと。間に挟むと既存の番号が全部ずれる
#define DEFERRED_ROOT_SIG \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
"CBV(b0, visibility = SHADER_VISIBILITY_ALL)," \
"CBV(b10, visibility = SHADER_VISIBILITY_ALL)," \
"RootConstants(num32BitConstants=8, b100), " \
"RootConstants(num32BitConstants=1, b101), " \
"CBV(b11, visibility = SHADER_VISIBILITY_ALL)," \
"RootConstants(num32BitConstants=2, b102), " \
"CBV(b12, visibility = SHADER_VISIBILITY_ALL)," \
RS_STATIC_SAMPLER



cbuffer CBCamera : register(b0)
{
	CameraData g_camera;
}

cbuffer CBAmbient : register(b10)
{
	AmbientData g_ambient;
}

cbuffer CBLightingOption : register(b11)
{
	LightingOptionData g_lightingOp;
}

cbuffer CBLightCount : register(b12)
{
	LightCountData g_lightCount;
}

// ライト配列 : LightManager が毎フレーム詰め直したもの
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex2 : register(b102)
{
	uint g_pointLightsIndex;
	uint g_directionalLightsIndex;
}

StructuredBuffer<PointLight> Get_pointLights() { StructuredBuffer<PointLight> _r = ResourceDescriptorHeap[g_pointLightsIndex]; return _r; }
#define g_pointLights Get_pointLights()
StructuredBuffer<DirectionalLight> Get_directionalLights() { StructuredBuffer<DirectionalLight> _r = ResourceDescriptorHeap[g_directionalLightsIndex]; return _r; }
#define g_directionalLights Get_directionalLights()

// ディファードレンダリングでは共通
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_albedoTexIndex;
	uint g_normalTexIndex;
	uint g_materialTexIndex;
	uint g_emiTexIndex;
	uint g_depthTexIndex;
	uint g_shadowMaskIndex;
	uint g_rayGIIndex;
	uint g_rayReflectionIndex;	// 鏡面反射(RaytracingReflectionPass)。繋がっていなければ 0xFFFFFFFF
}

Texture2D Get_albedoTex() { Texture2D _r = ResourceDescriptorHeap[g_albedoTexIndex]; return _r; }
#define g_albedoTex Get_albedoTex()
Texture2D Get_normalTex() { Texture2D _r = ResourceDescriptorHeap[g_normalTexIndex]; return _r; }
#define g_normalTex Get_normalTex()
Texture2D Get_materialTex() { Texture2D _r = ResourceDescriptorHeap[g_materialTexIndex]; return _r; }
#define g_materialTex Get_materialTex()
Texture2D Get_emiTex() { Texture2D _r = ResourceDescriptorHeap[g_emiTexIndex]; return _r; }
#define g_emiTex Get_emiTex()
Texture2D Get_depthTex() { Texture2D _r = ResourceDescriptorHeap[g_depthTexIndex]; return _r; }
#define g_depthTex Get_depthTex()
Texture2D Get_shadowMask() { Texture2D _r = ResourceDescriptorHeap[g_shadowMaskIndex]; return _r; }
#define g_shadowMask Get_shadowMask()
Texture2D Get_rayGI() { Texture2D _r = ResourceDescriptorHeap[g_rayGIIndex]; return _r; }
#define g_rayGI Get_rayGI()
// 鏡面反射 : rgb = 反射先の放射輝度 / a = 1 : 物に当たった / 0 : 空。
// 1920x1080 固定で出力と解像度が違うことがあるので、Load ではなく UV で引くこと
Texture2D Get_rayReflection() { Texture2D _r = ResourceDescriptorHeap[g_rayReflectionIndex]; return _r; }
#define g_rayReflection Get_rayReflection()

// 出力
// UAVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex1 : register(b101)
{
	uint g_outputIndex;
}

RWTexture2D<float4> Get_output() { RWTexture2D<float4> _r = ResourceDescriptorHeap[g_outputIndex]; return _r; } // 結果書き込み用
#define g_output Get_output()

// サンプラー
SamplerState g_samp : register(s0);

// ヘルパー関数 : 上で宣言した g_camera / g_ambient を使うので、必ずこの位置で読むこと
#include "../../../Common/Math/Transform.hlsli"
#include "../../../Common/Math/Normal.hlsli"
#include "../../../Common/Lighting/Fog.hlsli"

float3 ReconstructViewPos(float2 uv, float depth)
{
	float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
	float4 view = mul(clip, g_camera.invProj);
	return view.xyz / view.w;
}

//------------------------------------------------------------------------------------------
// 鏡面反射を粗さに応じてぼかして引く
//
// レイトレの反射は鏡(画素ごとに1本)なので、そのままだとざらついた面にもくっきり映る。
// 粗さに比例した半径の円盤で周りの反射を集めて、ぼけた映り込みに見せる。
//
// 円盤は反射テクスチャ(1920x1080 固定)の画素で測る。
// 周りの画素が別の面(奥の物・向きの違う面)だと、その面の反射が滲んでくるので、
// GBuffer の法線と深度で同じ面かどうかを見て重みを落とす
//------------------------------------------------------------------------------------------
static const uint REFLECTION_BLUR_TAP_NUM = 12;

float3 SampleReflection(
	float2 a_uv,
	int2 a_centerCoord,
	float3 a_normal,
	float a_viewZ,
	float a_radius,
	float2 a_gbufferDim)
{
	uint _reflWidth, _reflHeight;
	g_rayReflection.GetDimensions(_reflWidth, _reflHeight);
	const float2 _texel = 1.0f / float2(_reflWidth, _reflHeight);

	// サンプラーは WRAP なので、端で反対側の反射を拾わないよう内側へ寄せる
	const float2 _minUV = _texel * 0.5f;
	const float2 _maxUV = 1.0f - _texel * 0.5f;

	const float3 _center = g_rayReflection.SampleLevel(g_samp, clamp(a_uv, _minUV, _maxUV), 0).rgb;

	// 1画素に満たないぼかしは掛けない(ほぼ鏡の面)
	if (a_radius < 0.5f) return _center;

	// 円盤の向きを画素ごと・フレームごとに回す(Interleaved Gradient Noise)。
	// 揃えたままだとタップの並びが模様になって見える。残るざらつきは後ろの TAA が均す。
	// フレームの番号は持っていないので、毎フレーム変わる TAA のジッターで代わりにずらす
	const float2 _seed = float2(a_centerCoord) + g_camera.jitterOffset * 1024.0f;
	const float _rotation = frac(52.9829189f * frac(dot(_seed, float2(0.06711056f, 0.00583715f)))) * 2.0f * PI;

	float3 _sum = _center;
	float _weightSum = 1.0f;

	const int2 _maxCoord = int2(a_gbufferDim) - 1;

	for (uint _i = 0; _i < REFLECTION_BLUR_TAP_NUM; ++_i)
	{
		// 黄金角で並べた円盤(Vogel disk) : 少ないタップでも偏りなく埋まる
		const float _r = sqrt((_i + 0.5f) / REFLECTION_BLUR_TAP_NUM) * a_radius;
		const float _theta = _i * 2.39996323f + _rotation;
		const float2 _tapUV = clamp(a_uv + float2(cos(_theta), sin(_theta)) * _r * _texel, _minUV, _maxUV);

		// 同じ面か : GBuffer の法線と深度を比べる
		const int2 _tapCoord = clamp(int2(_tapUV * a_gbufferDim), int2(0, 0), _maxCoord);
		const float _tapDepth = g_depthTex.Load(int3(_tapCoord, 0)).r;
		if (_tapDepth >= 1.0f) continue;

		const float3 _tapNormal = DecsodeNormal(g_normalTex.Load(int3(_tapCoord, 0)).rg);
		const float2 _tapGBufferUV = (float2(_tapCoord) + 0.5f) / a_gbufferDim;
		const float _tapViewZ = ReconstructViewPos(_tapGBufferUV, _tapDepth).z;

		const float _normalWeight = pow(saturate(dot(_tapNormal, a_normal)), 16.0f);
		const float _depthWeight = saturate(1.0f - abs(_tapViewZ - a_viewZ) / (abs(a_viewZ) * 0.05f + 1e-3f));
		const float _weight = _normalWeight * _depthWeight;

		_sum += g_rayReflection.SampleLevel(g_samp, _tapUV, 0).rgb * _weight;
		_weightSum += _weight;
	}

	return _sum / _weightSum;
}

[RootSignature(DEFERRED_ROOT_SIG)]

[numthreads(8, 8, 1)]
void CSMain( uint3 DTid : SV_DispatchThreadID )
{
	// 画像の解像度を取得
	uint _width, _height;
	g_output.GetDimensions(_width, _height);
	
	// 画面外ならリターン
	if (DTid.x >= _width || DTid.y >= _height)
		return;

	// 座標を計算
	float2 _uv = (DTid.xy + 0.5f) / float2(_width, _height); // UV
	int2 _centerCoord = int2(DTid.xy); // センター座標
	
	// GBufferから情報を取得
	float3 _albedo = g_albedoTex.Load(int3(_centerCoord, 0)).rgb; // アルベド
	float _arpha = g_albedoTex.Load(int3(_centerCoord, 0)).a; // アルファ
	float2 _enc = g_normalTex.Load(int3(_centerCoord, 0)).rg; // 法線
	float3 _normal = DecsodeNormal(_enc); // 法線を復元
	float _depth = g_depthTex.Load(int3(_centerCoord, 0)).r; // 深度
	float _metallic = g_materialTex.Load(int3(_centerCoord, 0)).b; // 金属度
	float _roughness = g_materialTex.Load(int3(_centerCoord, 0)).g; // 粗さ

	float3 _emissive = g_emiTex.Load(int3(_centerCoord, 0)).rgb; // エミッシブ(自己発光)

	float _shadow = g_shadowMask.Load(int3(_centerCoord, 0)).r; // 影



	// GI(間接光)。
	// レイトレを抜いたパイプラインでは GI 入力が繋がっておらず、番号が無効値(0xFFFFFFFF)で届く。
	// その番号でヒープを引くと落ちるので、引かずにシーンの環境光を一様な間接光として使う
	float3 _rayGI = (g_rayGIIndex != 0xFFFFFFFFu)
		? g_rayGI.Load(int3(_centerCoord, 0)).rgb
		: g_ambient.ambientColor;

	// 3D空間での位置を復元
	float3 _viewPos = ReconstructViewPos(_uv, _depth);
	float4 _worldPos4 = mul(float4(_viewPos, 1), g_camera.invView);
	float3 _worldPos = _worldPos4.xyz / _worldPos4.w;

	//float3 _specular = _albedo; // スペキュラはアルベドと同じにしておく（今回はスペキュラを考慮しないため）
	// 非金属の基本反射率(F0)はオプションから調整可能にする
	float3 _F0 = lerp(
		g_lightingOp.dielectricF0.xxx,
		_albedo,
		_metallic
	);
	float _smoothness = 1.0f - _roughness; // 滑らかさ
	
	float3 _V = normalize(g_camera.cameraPos.xyz - _worldPos); // カメラ位置からワールド位置へのベクトル
	
	// 出力色
	float3 _outColor = float3(0, 0, 0);
	
	//------------------------------------------------------------------
	// 平行光
	//
	// 影を受けるのは先頭の1つだけ。
	// 影マスク(g_shadowMask)は1チャンネルしかなく、RaytracingShadowPass も
	// 主光源へレイを1本飛ばしているだけなので、2つ目以降へ同じマスクを掛けると
	// 別の光源が落とした影がそのまま乗ってしまう
	//------------------------------------------------------------------
	for (uint _dlIdx = 0; _dlIdx < g_lightCount.directionalNum; ++_dlIdx)
	{
		DirectionalLight _dl = g_directionalLights[_dlIdx];

		float3 _L = normalize(-_dl.dir);			// 光源に向かうベクトル
		float _NdotL = saturate(dot(_normal, _L));

		float3 _radiance = _dl.color.rgb * _dl.brightness;

		// 主光源だけがレイトレの影を受ける
		float _dlShadow = (_dlIdx == 0) ? _shadow : 1.0f;

		//------------------------------------------------------------------
		// 拡散反射
		
		// シンプルなディズニーベースの拡散反射を実装する
		// フレネル反射を考慮した拡散反射を計算
		float _diffuseFromFresnel = CalcDiffuseFromFresnel(
			_normal,
			_L,
			_V,
			_roughness
		);

		// 正規化Lambert拡散反射を求める
		float3 _lambertDiffuse = _radiance * _NdotL / PI * _dlShadow;

		// 最終的な拡散反射光を計算
		float3 _diffuse = _albedo * _diffuseFromFresnel * _lambertDiffuse;

		//------------------------------------------------------------------
		// 鏡面反射
		// Cook-Torranceモデルの鏡面反射BRDF( D*F*G / (4*NdotL*NdotV) )を計算
		float _specTerm = CookTorranceSpecular(
			_L,
			_V,
			_normal,
			_metallic,
			_roughness
		);

		// レンダリング方程式の cosθ(=NdotL) を掛ける。
		// これが抜けていると BRDF の分母に残る 1/NdotL が NdotL→0 の明暗境界(ターミネータ)で
		// 発散し、球の側面に明るいリング状の模様が出たり、ハイライトが過剰に大きく/明るくなる。
		// (拡散反射側は _lambertDiffuse に NdotL が入っているが、鏡面側には掛かっていなかった)
		// あわせて float3 で受け、色付きライト/F0の色が正しく反映されるようにする。
		float3 _spec = _specTerm * _NdotL;
		_spec *= _radiance;
		_spec *= _dlShadow;

		// 金属度が高ければ、鏡面反射はF0(スペキュラカラー)、低ければ白
		_spec *= lerp(float3(1.0f, 1.0f, 1.0f), _F0, _metallic);

		// 直接光(拡散+鏡面)の強さをオプションから調整可能にする
		_outColor += (_diffuse + _spec) * g_lightingOp.directionalIntensity;

		//------------------------------------------------------------------
		// リムライト

		// カメラから見た輪郭を抽出し、逆光方向の主光源に連動させる
		// 主光源を対象にする
		if(_dlIdx == 0)
		{
			float _NdotV = saturate(dot(_normal, _V));
			float _rimMask = pow(1.0f - _NdotV, g_lightingOp.rimPower);

			// L は表面から光源へ向かう方向
			// 光源はカメラの反対側にあるほど、逆光係数が大きくなる
			const float _backStart = g_lightingOp.rimBackLightStart;
			const float _backEnd = max(g_lightingOp.rimBackLightEnd, _backStart + 1e-4f);
			float _backLight = smoothstep(
				_backStart,
				_backEnd,
				dot(-_L, _V)
			);

			// 影でどれだけ消すか
			// 影マスクは光に背を向けた面(NdotL <= 0)をレイを飛ばさず 0 にしている(RayShadowShader)。
			// 逆光のリムはまさにその面に出るので、そこでマスクを掛けると必ず消える。
			// 実際にレイで調べた面(NdotL > 0)だけマスクを使う
			float _rimShadowMask = (_NdotL > 0.0f) ? _dlShadow : 1.0f;
			float _rimShadow = lerp(1.0f, _rimShadowMask, g_lightingOp.rimShadowRate);

			float3 _rimLight = _rimMask * _backLight * _radiance * g_lightingOp.rimColor * _rimShadow * g_lightingOp.rimIntensity;
			_outColor += _rimLight;
		}
	}

	//------------------------------------------------------------------
	// 点光源
	//
	// 平行光と違って影を持たないので _shadow は掛けない。
	// directionalIntensity も太陽側のつまみなので掛けない
	//------------------------------------------------------------------
	for (uint _i = 0; _i < g_lightCount.pointNum; ++_i)
	{
		PointLight _pl = g_pointLights[_i];

		float3 _toLight = _pl.pos - _worldPos;
		float _distSq = dot(_toLight, _toLight);

		// 届く範囲の外と、面の裏側は計算ごと飛ばす
		if (_distSq >= _pl.range * _pl.range) continue;

		float _dist = sqrt(_distSq);
		float3 _plL = _toLight / max(_dist, 1e-4f);	// 光源に向かうベクトル
		float _plNdotL = saturate(dot(_normal, _plL));
		if (_plNdotL <= 0.0f) continue;

		// 距離減衰 : 逆二乗に「range で 0 になる窓」を掛ける。
		// 逆二乗だけだと range の境目で光が途切れて輪郭が出る。
		// 分母の +1 は光源に近づいたときに発散させないため
		float _window = saturate(1.0f - pow(_dist / _pl.range, 4.0f));
		float _atten = (_window * _window) / (_distSq + 1.0f);

		float3 _plRadiance = _pl.color.rgb * _pl.brightness * _atten;

		// 拡散反射 : 平行光と同じ式
		float _plDiffuseFromFresnel = CalcDiffuseFromFresnel(_normal, _plL, _V, _roughness);
		float3 _plDiffuse = _albedo * _plDiffuseFromFresnel * (_plRadiance * _plNdotL / PI);

		// 鏡面反射 : 平行光と同じくレンダリング方程式の NdotL を掛ける
		// (掛けないと BRDF の分母に残る 1/NdotL が明暗境界で発散する)
		float _plSpecTerm = CookTorranceSpecular(_plL, _V, _normal, _metallic, _roughness);
		float3 _plSpec = _plSpecTerm * _plNdotL * _plRadiance;

		// 金属度が高ければ鏡面反射はF0(スペキュラカラー)、低ければ白
		_plSpec *= lerp(float3(1.0f, 1.0f, 1.0f), _F0, _metallic);

		_outColor += _plDiffuse + _plSpec;
	}

	//------------------------------------------------------------------
	// 間接光(GI と鏡面反射)
	//
	// 周りから来る光を、拡散(GI)と鏡面(反射)で分け合う。
	//   鏡面へ回る割合 : 環境BRDF。F0(金属度で非金属の値とアルベドを混ぜたもの)・粗さ・
	//                    視線の角度で決まる。滑らかな面ほど、浅い角度ほど強い
	//   拡散へ回る割合 : 残り。金属は拡散しないので (1 - 金属度) を掛ける
	// 足して 1 を超えないので、反射を足しても面が明るくなりすぎない
	//------------------------------------------------------------------
	float _NdotV = saturate(dot(_normal, _V));
	float3 _specularWeight = EnvBRDFApprox(_F0, _roughness, _NdotV);
	float3 _diffuseWeight = (1.0f - _specularWeight) * (1.0f - _metallic);

	// アンビエント(GI/間接光) : 強さをオプションから調整可能にする
	float3 _indirectLight = _rayGI * g_lightingOp.giIntensity;
	_outColor += _indirectLight * _albedo * _diffuseWeight;

	// 反射色
	//
	// レイトレの反射は鏡なので、粗いほどぼかし、
	// 粗さ reflectionRoughnessStart → End で周りの間接光(GI)へ置き換える。
	// GI は全方向から来る光の平均なので、ざらざらの面の鏡面反射の代わりになる。
	// 反射が繋がっていないパイプラインも、GI を鏡面反射の代わりにする
	// (金属は拡散を持たないので、何も足さないと真っ黒になる)
	float3 _specularRadiance = _indirectLight;
	if (g_rayReflectionIndex != 0xFFFFFFFFu)
	{
		const float _roughStart = g_lightingOp.reflectionRoughnessStart;
		const float _roughEnd = max(g_lightingOp.reflectionRoughnessEnd, _roughStart + 1e-4f);
		const float _roughFade = saturate((_roughness - _roughStart) / (_roughEnd - _roughStart));

		// 置き換え切った面は反射を引かない
		if (_roughFade < 1.0f)
		{
			// ぼかし半径は粗さに比例させ、End で reflectionBlurRadius に届く
			const float _blurRadius = g_lightingOp.reflectionBlurRadius * saturate(_roughness / _roughEnd);

			// レイトレーシングのリファレクションテクスチャを取得
			// (1920x1080 固定で出力と解像度が違うことがあるので UV で引く)
			const float3 _reflectionRadiance = SampleReflection(
				_uv, _centerCoord, _normal, _viewPos.z, _blurRadius, float2(_width, _height));

			_specularRadiance = lerp(_reflectionRadiance, _indirectLight, _roughFade);
		}
	}

	_outColor += _specularRadiance * _specularWeight * g_lightingOp.reflectionIntensity;
	
	// エミッシブ(自己発光)
	// 面が自分で出している光なので、影やライトの向きの影響を受けずそのまま足す。
	// GBufferEmissiv は R11G11B10_FLOAT なので 1.0 を超える値もそのまま乗る。
	_outColor += _emissive;

	// フォグ
	// ライティングが終わった色に対して、カメラからの深度(ビュー空間Z)と
	// ワールドYを見て掛ける。無効なら AmbientData の enable で丸ごとスキップされる。
	// 何も描かれていない画素は深度が最遠なので、そのままフォグ色で埋まる(地平線のかすみ)。
	_outColor = ApplyFog(_outColor, _viewPos.z, _worldPos.y);

	g_output[_centerCoord] = float4(_outColor, 1);
}
