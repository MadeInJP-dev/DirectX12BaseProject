#include "Boundary.hlsli"

float Random(float2 a_st)
{
	return frac(sin(dot(a_st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}

// ルートシグネチャ定義
[RootSignature(BOUNDARY_ROOT_SIG)]

float4 PSMain(VSOutput a_input) : SV_TARGET
{
	//------------------------------------------------------------------
	// 近づき具合 : 基準点(プレイヤー)に近いところほど濃い
	//
	// 面全体ではなく点ごとに距離を測るので、近づいた辺りだけが丸く浮かび上がる。
	// 見え始めが一番濃い距離以下なら、距離に関係なく常に見せる
	//------------------------------------------------------------------
	float _reveal = 1.0f;
	if (g_boundary.revealStartDistance > g_boundary.revealEndDistance)
	{
		const float _dist = distance(a_input.worldPos, g_boundary.revealPos);
		_reveal = 1.0f - smoothstep(g_boundary.revealEndDistance, g_boundary.revealStartDistance, _dist);
	}
	if (_reveal <= 0.0f)
	{
		discard;
	}

	// 画像 : 面に並べてからずらす
	const float2 _uv = a_input.uv * g_boundary.uvTiling + g_boundary.uvScroll;
	float4 _color = g_mainTex.Sample(g_samp, _uv) * g_boundary.color;

	//------------------------------------------------------------------
	// 走査線・ノイズ・明滅(ScanLineCS と同じく 1 を中心に明暗させる)
	//------------------------------------------------------------------
	float _light = 0.0f;

	// 走査線 : 画像1枚あたり scanLineDensity 本。ずらしにも付いていく
	if (g_boundary.opacityScanLine != 0.0f)
	{
		_light += sin(_uv.y * g_boundary.scanLineDensity * 6.2831853f) * g_boundary.opacityScanLine;
	}

	// ノイズ : 画面のピクセルごと。
	// 時間はそのまま掛けると長く遊ぶほど桁が大きくなって sin の精度が落ちるので、1〜2 に畳む
	if (g_boundary.opacityNoise != 0.0f)
	{
		_light += Random(floor(a_input.svPos.xy) * (1.0f + frac(g_boundary.time))) * g_boundary.opacityNoise;
	}

	// 明滅
	if (g_boundary.flickeringStrength != 0.0f)
	{
		_light += sin(g_boundary.flickeringSpeed * g_boundary.time) * g_boundary.flickeringStrength;
	}

	_color.rgb *= max(1.0f + _light, 0.0f);
	_color.a *= _reveal;

	// ほぼ透明なら捨てる
	if (_color.a < 0.001f)
	{
		discard;
	}

	return _color;
}
