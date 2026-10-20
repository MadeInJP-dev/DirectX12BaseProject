//==========================================================================================
//
// ScreenSpaceLensFlareCS
//
// まとめ終わったブルームを画面の中心で折り返して、メインカラーへ加算する。
//
//   出力 = メインカラー + (上下反転したブルーム + 点対称に反転したブルーム) * intensity
//
// 明るいところの反対側にゴーストが映る、簡易的なスクリーンスペースのレンズフレア。
// 元の位置のブルームは BloomCompositePass が足し終えているので、ここでは足さない。
//
// ブルームと同じくトーンマップ前のHDR段階で足す。
// 無効時はメインカラーをそのまま素通しするので、絵は変わらない。
//
//==========================================================================================
#include "../../../Common/RootSignatureLayout.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b13)           レンズフレア設定
//   1 : SRVの番号(t0-t1) メインカラー + ブルーム
//   2 : UAVの番号(u0)    合成結果
//==========================================================================================
#define SSLF_RS \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
"CBV(b13, visibility = SHADER_VISIBILITY_ALL)," \
"RootConstants(num32BitConstants=2, b100), " \
"RootConstants(num32BitConstants=1, b101)"

// ※ C++ の ScreenSpaceLensFlarePass::ScreenSpaceLensFlareCB と並びを合わせること
struct ScreenSpaceLensFlareData
{
	float intensity;		// 強度
	int enable;			// 0 なら素通し
	float2 pad;
};

cbuffer SSLFCB : register(b13)
{
	ScreenSpaceLensFlareData g_sslf;
}

// 入力
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_colorTexIndex;
	uint g_bloomTexIndex;
}

Texture2D<float4> Get_colorTex()
{
	Texture2D<float4> _r = ResourceDescriptorHeap[g_colorTexIndex];
	return _r;
} // メインカラー
#define g_colorTex Get_colorTex()
Texture2D<float4> Get_bloomTex()
{
	Texture2D<float4> _r = ResourceDescriptorHeap[g_bloomTexIndex];
	return _r;
} // まとめ終わったブルーム
#define g_bloomTex Get_bloomTex()


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

[RootSignature(SSLF_RS)]
[numthreads(8, 8, 1)]
void CSMain( uint3 DTid : SV_DispatchThreadID )
{
	// 画像の縦横サイズを取得
	uint _width, _height;
	g_outTex.GetDimensions(_width, _height);

	// 画面外ならリターン
	if (DTid.x >= _width || DTid.y >= _height)
		return;

	float4 _mainColor = g_colorTex[DTid.xy];

	// 無効ならそのまま通す
	if (g_sslf.enable == 0)
	{
		g_outTex[DTid.xy] = _mainColor;
		return;
	}

	// 画面の中心で折り返した位置。
	// uint のまま符号を反転すると巨大な値になって範囲外読み(= 0)になるので、
	// 反対側の端から数える。ブルームは出力と同じ解像度(BloomCompositePass と同じ前提)
	const uint _flipX = _width - 1 - DTid.x;
	const uint _flipY = _height - 1 - DTid.y;

	// 高輝度画像
	float3 _bloomCol1 = g_bloomTex[uint2(DTid.x, _flipY)].rgb;	// 上下反転
	float3 _bloomCol2 = g_bloomTex[uint2(_flipX, _flipY)].rgb;	// 点対称

	// SSLFカラー作成
	float3 _sslfColor = _bloomCol1 + _bloomCol2;
	_sslfColor *= g_sslf.intensity;

	// アルファはメインカラーのものを保つ
	g_outTex[DTid.xy] = float4(_mainColor.rgb + _sslfColor, _mainColor.a);
}
