//==========================================================================================
//
// DitherFade
//
// フェード係数(MeshInstanceData.fade)で画素を間引く、アルファディザー。
//   1.0 = 完全に見える(何も捨てない) / 0.0 = 見えない(全部捨てる)
//
// 半透明で描くと前後の並べ替えや GBuffer への書き込みができなくなるので、
// 不透明のまま 4x4 の Bayer 行列で画素を市松状に捨てて、透けているように見せる。
//
// GBuffer は ZPre の深度と EQUAL で比べるため、ZPre(DepthDitherFadePS)と
// GBuffer(MeshGBufferPS)は必ずこの同じ関数で判定すること。
// 判定がずれると、捨てた画素に深度だけが残って後ろの物が描かれなくなる。
//
//==========================================================================================
#ifndef MATH_DITHER_FADE_HLSLI
#define MATH_DITHER_FADE_HLSLI

// 4x4 の Bayer 行列(0〜15)。隣り合う値ができるだけ離れるように並んでいる
static const float DITHER_BAYER_4X4[16] =
{
	 0.0f,  8.0f,  2.0f, 10.0f,
	12.0f,  4.0f, 14.0f,  6.0f,
	 3.0f, 11.0f,  1.0f,  9.0f,
	15.0f,  7.0f, 13.0f,  5.0f
};

// この画素を捨てるか。a_screenPos は SV_Position.xy(画素の座標)
bool IsDitherFadeDiscard(float a_fade, float2 a_screenPos)
{
	if (a_fade >= 1.0f) return false;

	const uint2 _cell = uint2(a_screenPos) & 3u;
	const float _threshold = (DITHER_BAYER_4X4[_cell.y * 4u + _cell.x] + 0.5f) / 16.0f;
	return a_fade < _threshold;
}

#endif
