#include "UI.hlsli"

float4 PSMain(VSOutput a_input) : SV_TARGET
{
	// 切り抜き : 矩形の外へ出たところは描かない(スクロールする一覧の項目など)
	if (any(a_input.ndc < a_input.clipRect.xy) || any(a_input.ndc > a_input.clipRect.zw))
	{
		discard;
	}

	// テクスチャ取得 : インスタンス番号ではなく、UIDataで指定された実際のSRV番号で引く
	Texture2D _tex = ResourceDescriptorHeap[a_input.texIndex];
	float4 _texColor = _tex.Sample(g_samp,a_input.uv);

	// 色合成
	float4 _outColor = _texColor * a_input.color;

	// アルファ値がほぼ0ならピクセルを破棄して描画負荷を下げる
	if (_outColor.a < 0.001f)
	{
		discard;
	}
	
	return _outColor;
}
