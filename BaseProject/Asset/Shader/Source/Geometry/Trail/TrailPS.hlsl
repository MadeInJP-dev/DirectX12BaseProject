#include "Trail.hlsli"

//==========================================================================================
// トレイルの色
//
// プリマルチプライドで出す(合成は 1 × 色 + (1 - α) × 下地)。
//   半透明 : (色 × α, α)  下地を α のぶん隠して色を置く
//   加算   : (色 × α, 0)  下地はそのまま、色を足す
// どちらも同じ PSO で描けるので、加算と半透明の帯を同じ並びで遠い順に描ける
//==========================================================================================

// ルートシグネチャ定義
[RootSignature(TRAIL_ROOT_SIG)]

float4 PSMain(VSOutput a_input) : SV_Target0
{
	float4 _color = a_input.color;

	if (g_textureIndex != 0xFFFFFFFFu)
	{
		Texture2D _tex = ResourceDescriptorHeap[g_textureIndex];
		_color *= _tex.Sample(g_samp, a_input.uv);
	}

	const float _alpha = saturate(_color.a);
	if (_alpha <= 0.0f)
	{
		discard;
	}

	const float3 _rgb = _color.rgb * _alpha;
	return float4(_rgb, (g_isAdditive != 0) ? 0.0f : _alpha);
}
