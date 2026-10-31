//==========================================================================================
//
// DamageEffectCS
//
// 被弾したときだけ掛かる画面効果。画面(シーン)と HUD(UI)へ別々に
//   ・砂嵐のノイズ
//   ・横の帯ずれ(グリッチ)
//   ・RGB のずれ(色収差)
// を掛ける。画面にはさらに色抜けと縁の染め、HUD にはちらつきを掛ける。
//
// 掛かり具合(intensity)が 0 のときは、2枚ともそのまま写すだけ。
//
//==========================================================================================
#include "../../../Common/RootSignatureLayout.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)            被弾の画面効果の設定
//   1 : SRVの番号(t0)    画面 / HUD(任意)
//   2 : UAVの番号(u0)    画面 / HUD
//
// 追加は必ず末尾へ足すこと。間に挟むと既存の番号が全部ずれる
//==========================================================================================
#define DAMAGE_EFFECT_RS \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
"CBV(b0, visibility = SHADER_VISIBILITY_ALL)," \
"RootConstants(num32BitConstants=2, b100), " \
"RootConstants(num32BitConstants=2, b101), " \
RS_STATIC_SAMPLER_CLAMP

// ※ CPU 側 Engine::Graphics::Pipeline::DamageEffectCB(DamageEffectPass.cpp)と並びを合わせること
struct DamageEffect
{
	float intensity;		// 掛かり具合(0〜1)
	float time;				// 経過時間(秒)
	float screenNoise;		// 画面 : ノイズの濃さ
	float screenGlitch;		// 画面 : 帯ずれの幅(UV)

	float screenRGBShift;	// 画面 : RGB のずれ幅(UV)
	float screenDesaturate;	// 画面 : 色を抜く量
	float vignette;			// 画面 : 縁を染める強さ
	float glitchBands;		// 帯ずれの縦の分け数

	float3 vignetteColor;	// 画面 : 縁を染める色
	float glitchRate;		// ずれる帯の割合

	float hudNoise;			// HUD : ノイズの濃さ
	float hudGlitch;		// HUD : 帯ずれの幅(UV)
	float hudRGBShift;		// HUD : RGB のずれ幅(UV)
	float hudFlicker;		// HUD : ちらつきの量

	float glitchSpeed;		// ずれ方が切り替わる回数(1秒あたり)
	float pad0;
	float pad1;
	float pad2;
};

cbuffer DamageEffectCB : register(b0)
{
	DamageEffect g_damage;
}

// 入力
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_colorTexIndex;
	uint g_uiTexIndex;		// 任意 : 未接続なら DESCRIPTOR_INDEX_NONE
}

// 繋がっていない入力に届く番号
#define DESCRIPTOR_INDEX_NONE 0xFFFFFFFF

Texture2D<float4> Get_colorTex() { Texture2D<float4> _r = ResourceDescriptorHeap[g_colorTexIndex]; return _r; }	// 画面
#define g_colorTex Get_colorTex()
Texture2D<float4> Get_uiTex() { Texture2D<float4> _r = ResourceDescriptorHeap[g_uiTexIndex]; return _r; }		// HUD(プリマルチプライド)
#define g_uiTex Get_uiTex()

// 出力
// UAVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex1 : register(b101)
{
	uint g_outColorIndex;
	uint g_outUIIndex;
}

RWTexture2D<float4> Get_outColor() { RWTexture2D<float4> _r = ResourceDescriptorHeap[g_outColorIndex]; return _r; }
#define g_outColor Get_outColor()
RWTexture2D<float4> Get_outUI() { RWTexture2D<float4> _r = ResourceDescriptorHeap[g_outUIIndex]; return _r; }
#define g_outUI Get_outUI()

SamplerState g_samp : register(s0);

// 0〜1 の乱数
float Hash12(float2 a_p)
{
	float3 _p3 = frac(float3(a_p.xyx) * 0.1031f);
	_p3 += dot(_p3, _p3.yzx + 33.33f);
	return frac((_p3.x + _p3.y) * _p3.z);
}

//------------------------------------------------------------------------------------------
// 帯ずれ : 縦を glitchBands 本の帯に分け、ずれる帯だけ横へずらす量(UV)を返す。
// 時間は glitchSpeed で刻むので、ずれ方はカクカクと切り替わる。
// a_salt で画面と HUD のずれ方を変える
//------------------------------------------------------------------------------------------
float GlitchOffset(float a_v, float a_width, float a_salt)
{
	if (a_width <= 0.0f) return 0.0f;

	const float _step = floor(g_damage.time * g_damage.glitchSpeed);

	// 帯の太さも時間でばらつかせる(全部同じ太さだと縞模様に見える)
	const float _bands = g_damage.glitchBands * lerp(0.5f, 1.5f, Hash12(float2(_step, a_salt + 3.0f)));
	const float _band = floor(a_v * _bands);

	const float _pick = Hash12(float2(_band + a_salt, _step));
	const float _rate = saturate(g_damage.glitchRate * g_damage.intensity);
	if (_pick < 1.0f - _rate) return 0.0f;

	const float _dir = Hash12(float2(_band * 1.7f + a_salt, _step + 11.0f)) * 2.0f - 1.0f;
	return _dir * a_width * g_damage.intensity;
}

// RGB のずれ幅(UV)。時間で少し脈打たせる
float RGBShift(float a_width)
{
	return a_width * g_damage.intensity * (0.75f + 0.25f * sin(g_damage.time * 47.0f));
}

[RootSignature(DAMAGE_EFFECT_RS)]
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
	uint _width, _height;
	g_outColor.GetDimensions(_width, _height);
	if (DTid.x >= _width || DTid.y >= _height) return;

	const int2 _coord = int2(DTid.xy);
	const bool _hasUI = (g_uiTexIndex != DESCRIPTOR_INDEX_NONE);

	//------------------------------------------------------------------
	// 掛からないフレームはそのまま写す
	//------------------------------------------------------------------
	if (g_damage.intensity <= 0.0f)
	{
		g_outColor[_coord] = g_colorTex.Load(int3(_coord, 0));
		g_outUI[_coord] = _hasUI ? g_uiTex.Load(int3(_coord, 0)) : float4(0, 0, 0, 0);
		return;
	}

	const float2 _uv = (float2(_coord) + 0.5f) / float2(_width, _height);
	const float _noiseStep = floor(g_damage.time * 60.0f);

	//------------------------------------------------------------------
	// 画面
	//------------------------------------------------------------------
	{
		const float2 _base = _uv + float2(GlitchOffset(_uv.y, g_damage.screenGlitch, 0.0f), 0.0f);
		const float _shift = RGBShift(g_damage.screenRGBShift);

		float3 _col;
		_col.r = g_colorTex.SampleLevel(g_samp, _base + float2(_shift, 0.0f), 0).r;
		_col.g = g_colorTex.SampleLevel(g_samp, _base, 0).g;
		_col.b = g_colorTex.SampleLevel(g_samp, _base - float2(_shift, 0.0f), 0).b;

		// 色抜け
		const float _luma = dot(_col, float3(0.299f, 0.587f, 0.114f));
		_col = lerp(_col, _luma.xxx, saturate(g_damage.screenDesaturate * g_damage.intensity));

		// 砂嵐 : 1 を中心に明暗させる(暗いところも白く荒れるよう、少し足し算も混ぜる)
		const float _n = Hash12(float2(_coord) + _noiseStep * 17.31f) - 0.5f;
		const float _noise = g_damage.screenNoise * g_damage.intensity;
		_col = _col * (1.0f + _n * _noise) + _n * _noise * 0.25f;

		// 縁の染め : 画面の端ほど強く
		const float2 _d = (_uv - 0.5f) * float2(float(_width) / float(_height), 1.0f);
		const float _edge = smoothstep(0.45f, 1.05f, length(_d) * 1.2f);
		_col = lerp(_col, g_damage.vignetteColor, saturate(_edge * g_damage.vignette * g_damage.intensity));

		g_outColor[_coord] = float4(saturate(_col), 1.0f);
	}

	//------------------------------------------------------------------
	// HUD
	//
	// 板はプリマルチプライド(RGB にアルファが掛かっている)なので、
	// チャンネルごとに別の場所から拾うときはアルファも一緒に拾って、濃い方を残す
	//------------------------------------------------------------------
	if (!_hasUI)
	{
		g_outUI[_coord] = float4(0, 0, 0, 0);
		return;
	}

	{
		uint _uiWidth, _uiHeight;
		g_uiTex.GetDimensions(_uiWidth, _uiHeight);

		const float2 _base = _uv + float2(GlitchOffset(_uv.y, g_damage.hudGlitch, 57.0f), 0.0f);
		const float _shift = RGBShift(g_damage.hudRGBShift);

		const float4 _r = g_uiTex.SampleLevel(g_samp, _base + float2(_shift, 0.0f), 0);
		const float4 _g = g_uiTex.SampleLevel(g_samp, _base, 0);
		const float4 _b = g_uiTex.SampleLevel(g_samp, _base - float2(_shift, 0.0f), 0);

		float4 _ui = float4(_r.r, _g.g, _b.b, max(_g.a, max(_r.a, _b.a)));

		// ノイズ : 描いてあるところだけ荒らす
		const float _n = Hash12(float2(_coord) * 1.37f + _noiseStep * 7.77f) - 0.5f;
		_ui.rgb += _n * g_damage.hudNoise * g_damage.intensity * _ui.a;

		// 走査線のように横の細い線で欠けさせる(ノイズに合わせて揺らす)。
		// 画面全体を覆う暗い板も HUD に入っているので、欠けさせすぎると下の絵が縞に透ける
		const float _line = Hash12(float2(floor(_uv.y * float(_uiHeight) / 3.0f), _noiseStep));
		const float _dropout = step(1.0f - g_damage.hudNoise * g_damage.intensity * 0.08f, _line);
		_ui *= 1.0f - _dropout * 0.6f;

		// ちらつき : ときどき全体が薄くなる
		const float _flickStep = floor(g_damage.time * g_damage.glitchSpeed * 1.5f);
		const float _flick = Hash12(float2(_flickStep, 91.0f));
		const float _flickAmount = saturate(g_damage.hudFlicker * g_damage.intensity);
		_ui *= 1.0f - _flickAmount * step(0.5f, _flick) * lerp(0.4f, 1.0f, _flick);

		g_outUI[_coord] = max(_ui, 0.0f);
	}
}
