
struct ScreenSpaceLensFareData
{
	float intensity;		// 強度
};

cbuffer SSLFCB : register(b13)
{
	ScreenSpaceLensFareData g_sslf;
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

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	// 画像の縦横サイズを取得
	uint _width, _height;
	g_outTex.GetDimensions(_width, _height);

	// 画面外ならリターン
	if (DTid.x >= _width || DTid.y >= _height)
		return;

	float4 _mainColor = g_colorTex[DTid.xy];
	
	// 高輝度画像
	float3 _bloomCol0 = g_bloomTex[DTid.xy].rgb;
	float3 _bloomCol1 = g_bloomTex[float2(DTid.x, -DTid.y)].rgb;
	float3 _bloomCol2 = g_bloomTex[float2(-DTid.x, -DTid.y)].rgb;

	// SSLFカラー作成
	float3 _sslfColor = _bloomCol0 + _bloomCol1 + _bloomCol2;
	_sslfColor *= g_sslf.intensity;

	// アルファはメインカラーのものを保つ
	g_outTex[DTid.xy] = float4(_mainColor.rgb + _sslfColor, _mainColor.a);
}
