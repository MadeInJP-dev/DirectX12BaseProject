// インクルードガード
#ifndef LIGHTING_FOG_RESOLUTION_HLSLI
#define LIGHTING_FOG_RESOLUTION_HLSLI

//==========================================================================================
//
// FogResolution
//
// ボリュメトリックフォグを描画解像度より小さく回すときの、画素の対応。
//   SceneVolumetricFogCS : フォグの1画素につき、覆う範囲の中の代表の1画素を通るレイを積分する
//   SceneFogCompositeCS  : 近くのフォグの画素の代表の深度を引き、自分の深度に近いものほど重く混ぜる
// 両方が同じ代表を選ばないと、深度で重みを付けても縁がずれる。決め方はここだけに書く。
//
//==========================================================================================

// フォグの画素 a_fogCoord が代表にする、深度(描画解像度)の画素。
// フォグの画素の中心が乗る画素を選ぶ(1/2 なら 2x2 の右下)。同じ解像度ならそのまま
int2 CalcFogRepresentativePixel(int2 a_fogCoord, uint2 a_fogSize, uint2 a_depthSize)
{
	const float2 _scale = float2(a_depthSize) / float2(a_fogSize);
	const int2 _pixel = int2((float2(a_fogCoord) + 0.5f) * _scale);
	return min(_pixel, int2(a_depthSize) - 1);
}

#endif
