#include "../MeshShader/MeshCommon.hlsli"
#include "../../../Common/Math/CalcLighting.hlsli"
#include "../../../Common/Math/EnvBRDF.hlsli"
#include "../../../Common/RootParameters/LightData.hlsli"

//==========================================================================================
// 半透明のモデルを、ライティング済みの色の上へ直接描くピクセルシェーダー(前方ライティング)
//
// GBuffer は1画素に1枚の面しか持てないので、半透明はディファードに乗せられない。
// ライティングの後(TransparentPass)で、遠い順に1枚ずつ色を計算して重ねる。
//
// 光の当て方は DeferredLightingShader と同じ式(平行光・点光源・環境光・自己発光)。
// 次のものは持たない : 影・GI・レイトレの反射・リムライト・フォグ
//==========================================================================================

// 前方ライティングに要る値。ルートパラメーター 13 番(RootConstants b2)。
// ※ C++ 側 TransparentPass の ForwardLightConstants と並びを合わせること。
//    cbuffer なので float3 が16バイト行をまたがないよう、uint 4つの後ろに置いている
cbuffer ForwardLightConstants : register(b2)
{
	uint g_pointLightsIndex;		// ポイントライト配列の SRV 番号
	uint g_directionalLightsIndex;	// 平行光配列の SRV 番号
	uint g_directionalLightNum;		// 平行光の数
	uint g_pointLightNum;			// ポイントライトの数
	float3 g_ambientColor;			// 環境光(GI の代わりに一様な間接光として使う)
	float g_forwardPad;
};

StructuredBuffer<PointLight> Get_pointLights() { StructuredBuffer<PointLight> _r = ResourceDescriptorHeap[g_pointLightsIndex]; return _r; }
#define g_pointLights Get_pointLights()
StructuredBuffer<DirectionalLight> Get_directionalLights() { StructuredBuffer<DirectionalLight> _r = ResourceDescriptorHeap[g_directionalLightsIndex]; return _r; }
#define g_directionalLights Get_directionalLights()

// 非金属の垂直入射の反射率(ライティングパスの既定値と同じ)
static const float DIELECTRIC_F0 = 0.04f;

// ルートシグネチャ定義
[RootSignature(MESHGLOBAL_ROOT_SIG)]
float4 PSMain(VertexOutput a_input) : SV_Target0
{
	float2 _uv = a_input.uv;

	// -----------------------------------------------------------
	// マテリアル情報のフェッチ
	// -----------------------------------------------------------
	MeshInstanceData _inst = g_instanceData[a_input.instanceID];
	MeshMaterial _mat = g_materialData[_inst.materialOffset];

	// -----------------------------------------------------------
	// アルベドと不透明度
	// -----------------------------------------------------------
	float4 _baseTex = float4(1, 1, 1, 1);
	if (_mat.albedoIndex >= 0)
	{
		Texture2D albedoTex = ResourceDescriptorHeap[NonUniformResourceIndex(_mat.albedoIndex)];
		_baseTex = albedoTex.Sample(smp, _uv);
	}
	const float4 _base = _baseTex * _mat.baseColor;
	const float3 _albedo = _base.rgb;

	// フェード(カメラに近い障害物など)は、半透明なら画素を捨てずに不透明度へ掛ける。
	// 深度を書かないので、ZPre と画素をそろえる必要が無い
	const float _alpha = saturate(_base.a * _inst.fade);
	if (_alpha <= 0.0f)
	{
		discard;
	}

	// -----------------------------------------------------------
	// 法線 : GBuffer と同じく TBN でノーマルマップを掛ける
	// -----------------------------------------------------------
	float3 _nTex = float3(0, 0, 1);
	if (_mat.normalIndex >= 0)
	{
		Texture2D normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(_mat.normalIndex)];
		_nTex = normalTex.Sample(smp, _uv).xyz * 2.0f - 1.0f;
	}

	float3 _N = normalize(a_input.normal);
	float3 _T = normalize(a_input.tangent);
	_T = normalize(_T - dot(_T, _N) * _N);
	float3 _B = cross(_N, _T);
	float3x3 TBN = { _T, _B, _N };
	float3 _normal = normalize(mul(_nTex, TBN));

	const float3 _V = normalize(g_camera.cameraPos.xyz - a_input.worldPos);

	// 裏から見ている面(裏も描くマテリアル)は法線を視線側へ返す
	if (dot(_normal, _V) < 0.0f)
	{
		_normal = -_normal;
	}

	// -----------------------------------------------------------
	// 金属度・粗さ
	// -----------------------------------------------------------
	float3 _mr = float3(1, 1, 1);
	if (_mat.metaRoughnessIndex >= 0)
	{
		Texture2D mrTex = ResourceDescriptorHeap[NonUniformResourceIndex(_mat.metaRoughnessIndex)];
		_mr = mrTex.Sample(smp, _uv).rgb;
	}
	const float _roughness = _mr.g * _mat.roughness;
	const float _metallic = _mr.b * _mat.metallic;

	const float3 _F0 = lerp(DIELECTRIC_F0.xxx, _albedo, _metallic);

	float3 _outColor = float3(0, 0, 0);

	//------------------------------------------------------------------
	// 平行光 : 影は無い(影マスクは不透明の面のぶんしか持っていない)
	//------------------------------------------------------------------
	for (uint _dlIdx = 0; _dlIdx < g_directionalLightNum; ++_dlIdx)
	{
		DirectionalLight _dl = g_directionalLights[_dlIdx];

		float3 _L = normalize(-_dl.dir);
		float _NdotL = saturate(dot(_normal, _L));
		float3 _radiance = _dl.color.rgb * _dl.brightness;

		float _diffuseFromFresnel = CalcDiffuseFromFresnel(_normal, _L, _V, _roughness);
		float3 _diffuse = _albedo * _diffuseFromFresnel * (_radiance * _NdotL / PI);

		float3 _spec = CookTorranceSpecular(_L, _V, _normal, _metallic, _roughness) * _NdotL * _radiance;
		_spec *= lerp(float3(1.0f, 1.0f, 1.0f), _F0, _metallic);

		_outColor += _diffuse + _spec;
	}

	//------------------------------------------------------------------
	// 点光源 : ライティングパスと同じ減衰
	//------------------------------------------------------------------
	for (uint _i = 0; _i < g_pointLightNum; ++_i)
	{
		PointLight _pl = g_pointLights[_i];

		float3 _toLight = _pl.pos - a_input.worldPos;
		float _distSq = dot(_toLight, _toLight);
		if (_distSq >= _pl.range * _pl.range) continue;

		float _dist = sqrt(_distSq);
		float3 _plL = _toLight / max(_dist, 1e-4f);
		float _plNdotL = saturate(dot(_normal, _plL));
		if (_plNdotL <= 0.0f) continue;

		float _window = saturate(1.0f - pow(_dist / _pl.range, 4.0f));
		float _atten = (_window * _window) / (_distSq + 1.0f);
		float3 _plRadiance = _pl.color.rgb * _pl.brightness * _atten;

		float _plDiffuseFromFresnel = CalcDiffuseFromFresnel(_normal, _plL, _V, _roughness);
		float3 _plDiffuse = _albedo * _plDiffuseFromFresnel * (_plRadiance * _plNdotL / PI);

		float3 _plSpec = CookTorranceSpecular(_plL, _V, _normal, _metallic, _roughness) * _plNdotL * _plRadiance;
		_plSpec *= lerp(float3(1.0f, 1.0f, 1.0f), _F0, _metallic);

		_outColor += _plDiffuse + _plSpec;
	}

	//------------------------------------------------------------------
	// 間接光 : 環境光を拡散と鏡面で分け合う(ライティングパスで GI が無いときと同じ)
	//------------------------------------------------------------------
	const float _NdotV = saturate(dot(_normal, _V));
	const float3 _specularWeight = EnvBRDFApprox(_F0, _roughness, _NdotV);
	const float3 _diffuseWeight = (1.0f - _specularWeight) * (1.0f - _metallic);
	_outColor += g_ambientColor * (_albedo * _diffuseWeight + _specularWeight);

	//------------------------------------------------------------------
	// 自己発光 : GBuffer と同じく、テクスチャ由来 + マテリアルに依らない加算
	//------------------------------------------------------------------
	float3 _eTex = float3(1, 1, 1);
	if (_mat.emissiveIndex >= 0)
	{
		Texture2D emissiveTex = ResourceDescriptorHeap[NonUniformResourceIndex(_mat.emissiveIndex)];
		_eTex = emissiveTex.Sample(smp, _uv).rgb;
	}
	_outColor += _eTex * _mat.emissive + _mat.emissiveAdd;

	return float4(_outColor, _alpha);
}
