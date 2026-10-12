//==========================================================================================
//
// ScanLineCS
//
// メインカラーに走査線・ノイズ・明滅を掛けて、古いモニター越しのような絵にする。
//
// 無効時はそのまま素通しするので、絵は変わらない。
//
//==========================================================================================
#include "../../../Common/RootSignatureLayout.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)            走査線の設定
//   1 : SRVの番号(t0)    メインカラー / 掛ける範囲のマスク(任意)
//   2 : UAVの番号(u0)    出力カラー
//
// 追加は必ず末尾へ足すこと。間に挟むと既存の番号が全部ずれる
//==========================================================================================
#define SCAN_LINE_RS \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
"CBV(b0, visibility = SHADER_VISIBILITY_ALL)," \
"RootConstants(num32BitConstants=2, b100), " \
"RootConstants(num32BitConstants=1, b101)"

// ※ CPU 側 Engine::Graphics::Pipeline::ScanLineCB(ScanLinePass.cpp)と並びを合わせること
struct ScanLine
{
	float time;
	float opacityScanLine;
	float opacityNoise;
	float flickeringSpeed;
	float flickeringStrength;
	int enable;			// 0 でそのまま通す
	int enableScanLine;		// 0 で走査線を掛けない
	int enableNoise;		// 0 でノイズを掛けない

	int enableFlickering;	// 0 で明滅させない
	float pad0;
	float pad1;
	float pad2;
};

cbuffer ScanLineCB : register(b0)
{
	ScanLine g_scanLineData;
}

// 入力
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_colorTexIndex;
	uint g_maskTexIndex;	// 任意 : 未接続なら DESCRIPTOR_INDEX_NONE(画面全体に掛ける)
}

// 繋がっていない入力に届く番号
#define DESCRIPTOR_INDEX_NONE 0xFFFFFFFF

Texture2D<float4> Get_colorTex()
{
	Texture2D<float4> _r = ResourceDescriptorHeap[g_colorTexIndex];
	return _r;
} // メインカラー
#define g_colorTex Get_colorTex()

Texture2D<float> Get_maskTex()
{
	Texture2D<float> _r = ResourceDescriptorHeap[g_maskTexIndex];
	return _r;
} // 掛ける範囲(r : 1 = 掛ける)
#define g_maskTex Get_maskTex()

// 出力
// UAVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex1 : register(b101)
{
	uint g_outTexIndex;
}

RWTexture2D<float4> Get_outTex()
{
	RWTexture2D<float4> _r = ResourceDescriptorHeap[g_outTexIndex];
	return _r;
}
#define g_outTex Get_outTex()

float Random(float2 a_st)
{
	return frac(sin(dot(a_st.xy, float2(12.9898, 78.233))) * 43758.5453123);

}

[RootSignature(SCAN_LINE_RS)]
[numthreads(8, 8, 1)]
void CSMain( uint3 DTid : SV_DispatchThreadID )
{
	// 範囲外チェック
	uint _width, _height;
	g_outTex.GetDimensions(_width, _height);
	if (DTid.x >= _width || DTid.y >= _height) return;
	int2 _coord = DTid.xy;

	// 無効ならそのまま通す
	if (g_scanLineData.enable == 0)
	{
		g_outTex[_coord] = g_colorTex.Load(int3(_coord, 0));
		return;
	}

	float3 _col = float3(0, 0, 0);

	// 波
	if (g_scanLineData.enableScanLine != 0)
	{
		float _s = sin(_coord.y * 1000);
		float _c = cos(_coord.y * 1000);
		_col += float3(_c, _s, _c) * g_scanLineData.opacityScanLine;
	}

	// ノイズ
	if (g_scanLineData.enableNoise != 0)
	{
		float _r = Random(_coord * g_scanLineData.time);
		_col += float3(_r, _r, _r) * g_scanLineData.opacityNoise;
	}

	// 明滅
	if (g_scanLineData.enableFlickering != 0)
	{
		float _flash = sin(g_scanLineData.flickeringSpeed * g_scanLineData.time);
		_col += float3(_flash, _flash, _flash) * g_scanLineData.flickeringStrength;
	}

	// 元画像と合成
	// 1 を中心に明暗させる(各値が 0 なら元の絵のまま)。
	// マスクが繋がっていれば、立っているところだけに掛ける
	float _mask = 1.0f;
	if (g_maskTexIndex != DESCRIPTOR_INDEX_NONE)
	{
		_mask = g_maskTex.Load(int3(_coord, 0));
	}

	float4 _color = g_colorTex.Load(int3(_coord, 0));
	_color.rgb *= 1.0f + _col * _mask;

	// 出力
	g_outTex[_coord] = _color;
}
