//==========================================================================================
//
// SceneFogCompositeCS
//
// メインカラーへ、SceneVolumetricFogCS が書いたフォグを重ねる。
//
//   出力 = lerp(メインカラー, フォグの色, フォグの濃さ * intensity)
//
// トーンマップ前のHDR段階で重ねる(フォグの色もHDRのまま扱う)。
// 無効時はメインカラーをそのまま素通しするので、絵は変わらない。
//
// フォグは描画解像度より小さく回していることがある(SceneVolumetricFogPass の Resolution)。
// そのときは近くの4画素を、バイリニアの重みに「代表の深度が自分の深度に近いか」を掛けて混ぜる。
// 手前の物体(ボイドなど)と奥の地面でフォグの濃さが大きく違うので、
// ただのバイリニアだと物体の縁に地面のフォグがにじむ。
// 深度を繋がない・同じ解像度のときは、ただのバイリニアで引く。
//
//==========================================================================================
#include "../../../Common/RootSignatureLayout.hlsli"

#include "../../../Common/RootParameters/CameraData.hlsli"
#include "../../../Common/Lighting/FogResolution.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)            合成の設定
//   1 : SRVの番号(t0-t2) メインカラー + フォグ + シーンの深度(任意)
//   2 : UAVの番号(u0)    合成結果
//   3 : CBV(b1)            カメラ(深度を奥行きへ戻す)
//
// 追加は必ず末尾へ足すこと。間に挟むと既存の番号が全部ずれる
//==========================================================================================
#define SCENE_FOG_COMPOSITE_RS \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
"CBV(b0, visibility = SHADER_VISIBILITY_ALL)," \
"RootConstants(num32BitConstants=3, b100), " \
"RootConstants(num32BitConstants=1, b101), " \
"CBV(b1, visibility = SHADER_VISIBILITY_ALL)," \
RS_STATIC_SAMPLER_CLAMP

// 張られていないときの番号(未接続の入力。グラフ側と合わせる)
#define DESCRIPTOR_INDEX_NONE 0xFFFFFFFF

// 引き伸ばすときの奥行きの許容差。
// 自分の奥行きに対する割合と、近いところ用の下限(m)のうち大きいほうを使う
#define FOG_UPSAMPLE_DEPTH_TOLERANCE_RATE 0.1f
#define FOG_UPSAMPLE_DEPTH_TOLERANCE_MIN  0.1f

// 合成の設定
// ※ CPU 側 Engine::Graphics::SceneFogCompositeCB と並びを合わせること
struct SceneFogCompositeData
{
	float intensity;	// フォグの濃さに掛ける倍率
	int enable;			// 0 なら重ねずにそのまま通す
	float2 pad0;
};

cbuffer CBSceneFogComposite : register(b0)
{
	SceneFogCompositeData g_composite;
}

cbuffer CBCamera : register(b1)
{
	CameraData g_camera;
}

// 入力
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_colorTexIndex;
	uint g_fogTexIndex;
	uint g_sceneDepthTexIndex;	// 任意 : 未接続なら DESCRIPTOR_INDEX_NONE(ただのバイリニアで引く)
}

Texture2D<float4> Get_colorTex() { Texture2D<float4> _r = ResourceDescriptorHeap[g_colorTexIndex]; return _r; }	// メインカラー
#define g_colorTex Get_colorTex()
Texture2D<float4> Get_fogTex() { Texture2D<float4> _r = ResourceDescriptorHeap[g_fogTexIndex]; return _r; }		// フォグ(rgb = 色 / a = 濃さ)
#define g_fogTex Get_fogTex()
Texture2D<float> Get_sceneDepthTex() { Texture2D<float> _r = ResourceDescriptorHeap[g_sceneDepthTexIndex]; return _r; }	// シーンの深度(フォグのレイの終点と同じもの)
#define g_sceneDepthTex Get_sceneDepthTex()

// 出力
// UAVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex1 : register(b101)
{
	uint g_outTexIndex;
}

RWTexture2D<float4> Get_outTex() { RWTexture2D<float4> _r = ResourceDescriptorHeap[g_outTexIndex]; return _r; }
#define g_outTex Get_outTex()

// サンプラー
SamplerState g_samp : register(s0);

// 深度をカメラからの奥行き(ビュー空間の z の大きさ)へ戻す。
// 画面の中心を通る点で戻すので、ジッターは効かない
float LinearizeDepth(float a_depth)
{
	const float4 _viewPos = mul(float4(0.0f, 0.0f, a_depth, 1.0f), g_camera.invProj);
	return abs(_viewPos.z / _viewPos.w);
}

// 縮小解像度のフォグを、深度を見ながらこの画素へ引き伸ばす。
//   a_pixel     : 出力(描画解像度)の画素
//   a_fogSize   : フォグの解像度
//   a_depthSize : 深度の解像度
float4 SampleFogDepthAware(int2 a_pixel, uint2 a_fogSize, uint2 a_depthSize)
{
	const float _viewZ = LinearizeDepth(g_sceneDepthTex.Load(int3(min(a_pixel, int2(a_depthSize) - 1), 0)));
	const float _tolerance = max(_viewZ * FOG_UPSAMPLE_DEPTH_TOLERANCE_RATE, FOG_UPSAMPLE_DEPTH_TOLERANCE_MIN);

	// 自分の中心がフォグのどこに乗るか(バイリニアと同じ取り方)
	const float2 _pos = (float2(a_pixel) + 0.5f) * float2(a_fogSize) / float2(a_depthSize) - 0.5f;
	const int2 _base = int2(floor(_pos));
	const float2 _frac = _pos - float2(_base);
	const int2 _maxId = int2(a_fogSize) - 1;

	float4 _sum = 0.0f;
	float _sumWeight = 0.0f;
	float4 _fallback = 0.0f;		// どれも重みが付かないとき用 : 奥行きが一番近い画素
	float _fallbackDiff = 1e30f;

	[unroll]
	for (int _y = 0; _y < 2; ++_y)
	{
		[unroll]
		for (int _x = 0; _x < 2; ++_x)
		{
			const int2 _id = clamp(_base + int2(_x, _y), int2(0, 0), _maxId);
			const float4 _fog = g_fogTex.Load(int3(_id, 0));

			// この画素のフォグがレイを通した点の奥行き
			const int2 _repPixel = CalcFogRepresentativePixel(_id, a_fogSize, a_depthSize);
			const float _repViewZ = LinearizeDepth(g_sceneDepthTex.Load(int3(_repPixel, 0)));
			const float _diff = abs(_repViewZ - _viewZ);

			if (_diff < _fallbackDiff)
			{
				_fallbackDiff = _diff;
				_fallback = _fog;
			}

			const float _bilinear = (_x == 0 ? 1.0f - _frac.x : _frac.x) * (_y == 0 ? 1.0f - _frac.y : _frac.y);
			const float _weight = _bilinear * exp(-_diff / _tolerance);
			_sum += _fog * _weight;
			_sumWeight += _weight;
		}
	}

	return (_sumWeight > 1e-4f) ? (_sum / _sumWeight) : _fallback;
}

[RootSignature(SCENE_FOG_COMPOSITE_RS)]
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
	// 出力画像の解像度を取得
	uint _width, _height;
	g_outTex.GetDimensions(_width, _height);

	// 画面外チェック
	if (DTid.x >= _width || DTid.y >= _height) return;

	float4 _mainColor = g_colorTex[DTid.xy];

	// 無効ならそのまま通す
	if (g_composite.enable == 0)
	{
		g_outTex[DTid.xy] = _mainColor;
		return;
	}

	uint _fogWidth, _fogHeight;
	g_fogTex.GetDimensions(_fogWidth, _fogHeight);

	float4 _fog;
	const bool _isReduced = (_fogWidth != _width) || (_fogHeight != _height);
	if (_isReduced && g_sceneDepthTexIndex != DESCRIPTOR_INDEX_NONE)
	{
		uint _depthWidth, _depthHeight;
		g_sceneDepthTex.GetDimensions(_depthWidth, _depthHeight);
		_fog = SampleFogDepthAware(int2(DTid.xy), uint2(_fogWidth, _fogHeight), uint2(_depthWidth, _depthHeight));
	}
	else
	{
		// フォグはUVで引く : 深度が無いときもフォグだけ縮小解像度で回したものに合わせられるように
		float2 _uv = (float2(DTid.xy) + 0.5f) / float2(_width, _height);
		_fog = g_fogTex.SampleLevel(g_samp, _uv, 0);
	}

	float _amount = saturate(_fog.a * g_composite.intensity);

	// アルファはメインカラーのものを保つ(後段のUIやコピーが見ている)
	g_outTex[DTid.xy] = float4(lerp(_mainColor.rgb, _fog.rgb, _amount), _mainColor.a);
}
