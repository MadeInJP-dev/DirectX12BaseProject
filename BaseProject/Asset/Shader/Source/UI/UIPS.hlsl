#include "UI.hlsli"

//==========================================================================================
// ノイズ
//
// テクスチャの指定が無いときはここで作ったノイズを使う。
// 値ノイズ(格子点に乱数を置いて滑らかに繋いだもの)を3段重ねてある。
// 座標は「1 で ノイズ1枚ぶん」なので、テクスチャと同じ大きさの感覚で揃う
//==========================================================================================
float Hash21(float2 a_p)
{
	a_p = frac(a_p * float2(123.34f, 456.21f));
	a_p += dot(a_p, a_p + 45.32f);
	return frac(a_p.x * a_p.y);
}

float ValueNoise(float2 a_p)
{
	const float2 _cell = floor(a_p);
	const float2 _f = frac(a_p);
	const float2 _u = _f * _f * (3.0f - 2.0f * _f);

	const float _a = Hash21(_cell);
	const float _b = Hash21(_cell + float2(1.0f, 0.0f));
	const float _c = Hash21(_cell + float2(0.0f, 1.0f));
	const float _d = Hash21(_cell + float2(1.0f, 1.0f));

	return lerp(lerp(_a, _b, _u.x), lerp(_c, _d, _u.x), _u.y);
}

float SampleNoise(uint a_noiseTexIndex, float2 a_uv)
{
	if (a_noiseTexIndex != UI_NOISE_TEX_NONE)
	{
		// インスタンスごとに違う番号が来うるので NonUniformResourceIndex で引く
		Texture2D _noiseTex = ResourceDescriptorHeap[NonUniformResourceIndex(a_noiseTexIndex)];
		return _noiseTex.Sample(g_samp, a_uv).r;	// サンプラーは WRAP なので繰り返して敷かれる
	}

	// 1枚ぶんに 4 マス並べ、細かい段を重ねる
	const float2 _p = a_uv * 4.0f;
	float _n = ValueNoise(_p) * 0.5f;
	_n += ValueNoise(_p * 2.0f) * 0.25f;
	_n += ValueNoise(_p * 4.0f) * 0.25f;
	return _n;
}

//==========================================================================================
// ディゾルブ : ノイズの値がしきい値より小さいところから消していく
//
// しきい値は進み具合 0 で「-縁の幅」、1 で「1」まで動く。
// 0 のときに縁の帯が見えないよう、帯の幅ぶん下から始めている。
// 残ったところのうち、しきい値のすぐ上(縁の幅の中)に縁の色を乗せる
//==========================================================================================
float4 ApplyDissolve(float4 a_color, VSOutput a_input)
{
	if (a_input.effectProgress <= 0.0f) return a_color;
	if (a_input.effectProgress >= 1.0f) discard;

	const float _noise = SampleNoise(a_input.noiseTexIndex, a_input.noiseUV);
	const float _width = max(a_input.edgeWidth, 1e-4f);
	const float _threshold = lerp(-_width, 1.0f, a_input.effectProgress);

	if (_noise < _threshold) discard;

	// 縁 : しきい値に近いほど 1
	const float _edge = 1.0f - saturate((_noise - _threshold) / _width);
	a_color.rgb = lerp(a_color.rgb, a_input.edgeColor.rgb, _edge * a_input.edgeColor.a);
	return a_color;
}

//==========================================================================================
// 縮小に強い読み方
//
// 文字は 64px で焼いたアトラスを縮めて出す(ミップマップは無い)。
// 1ピクセルで1回だけ読むと、間の細い線を飛ばしてかすれる(16px なら 4 テクセルに1回)。
// 1ピクセルが覆う範囲が広いときだけ、その範囲を格子に分けて読んで平均する。
// 勾配も格子の大きさに縮めて渡すので、ミップマップを持つテクスチャでも二重にぼけない
//==========================================================================================
static const float SUPER_SAMPLE_START = 1.25f;	// これより広く覆うと複数回読む(テクセル)
static const int SUPER_SAMPLE_MAX = 4;			// 1辺の最大の分割数(4 で 16 回)

float4 SampleMinified(Texture2D a_tex, float2 a_uv)
{
	const float2 _dx = ddx(a_uv);
	const float2 _dy = ddy(a_uv);

	float2 _texSize;
	a_tex.GetDimensions(_texSize.x, _texSize.y);
	const float _footprint = max(length(_dx * _texSize), length(_dy * _texSize));

	if (_footprint <= SUPER_SAMPLE_START) return a_tex.SampleGrad(g_samp, a_uv, _dx, _dy);

	const int _count = clamp((int)ceil(_footprint), 2, SUPER_SAMPLE_MAX);
	const float _inv = 1.0f / _count;
	const float2 _subDx = _dx * _inv;
	const float2 _subDy = _dy * _inv;

	float4 _sum = 0.0f;
	for (int _y = 0; _y < _count; ++_y)
	{
		for (int _x = 0; _x < _count; ++_x)
		{
			const float2 _offset = ((_x + 0.5f) * _inv - 0.5f) * _dx + ((_y + 0.5f) * _inv - 0.5f) * _dy;
			_sum += a_tex.SampleGrad(g_samp, a_uv + _offset, _subDx, _subDy);
		}
	}
	return _sum * (_inv * _inv);
}

PSOutput PSMain(VSOutput a_input)
{
	// 切り抜き : 矩形の外へ出たところは描かない(スクロールする一覧の項目など)
	if (any(a_input.ndc < a_input.clipRect.xy) || any(a_input.ndc > a_input.clipRect.zw))
	{
		discard;
	}

	// テクスチャ取得 : インスタンス番号ではなく、UIDataで指定された実際のSRV番号で引く
	Texture2D _tex = ResourceDescriptorHeap[a_input.texIndex];
	float4 _texColor = SampleMinified(_tex, a_input.uv);

	// 色合成
	float4 _outColor = _texColor * a_input.color;

	// 効果(UIマテリアル)
	if (a_input.effectType == UI_EFFECT_DISSOLVE)
	{
		_outColor = ApplyDissolve(_outColor, a_input);
	}

	// アルファ値がほぼ0ならピクセルを破棄して描画負荷を下げる
	if (_outColor.a < 0.001f)
	{
		discard;
	}

	PSOutput _out;
	_out.color = _outColor;

	// マスクは r に印、a に色と同じ不透明度を入れる(ブレンドで下のマスクへ重なる)
	const float _isScanLine = (a_input.postEffectFlags & UI_POST_EFFECT_SCAN_LINE) ? 1.0f : 0.0f;
	_out.scanLineMask = float4(_isScanLine, 0.0f, 0.0f, _outColor.a);

	return _out;
}
