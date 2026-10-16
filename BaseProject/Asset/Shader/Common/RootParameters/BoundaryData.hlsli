// 境界面1枚ぶんの描画データ。
// ※ CPU 側 Engine::Graphics::BoundaryDrawData(Frame/DrawList/BoundaryData.h)と並びを合わせること
#ifndef ROOTPARAM_BOUNDARY_DATA_HLSLI
#define ROOTPARAM_BOUNDARY_DATA_HLSLI

struct BoundaryData
{
	// 板(1x1, XY 平面)をワールドへ移す行列。CPU 側は行ベクトル(v * M)で組んでいる
	row_major float4x4 worldMat;

	float4 color;				// 画像に掛ける色(a は一番濃いときの濃さ)

	float3 revealPos;			// 近づき具合を測る基準点(プレイヤー)
	float revealStartDistance;	// この距離より近いと見え始める

	float2 uvTiling;			// 面に画像を何枚並べるか
	float2 uvScroll;			// UVのずらし

	float revealEndDistance;	// この距離で一番濃くなる
	uint texIndex;				// 画像のSRVインデックス
	float time;					// ノイズと明滅を動かす経過時間(秒)
	float scanLineDensity;		// 画像1枚あたりの走査線の本数

	float opacityScanLine;		// 走査線の濃さ(0 で掛けない)
	float opacityNoise;			// ノイズの濃さ(0 で掛けない)
	float flickeringSpeed;		// 明滅の速さ
	float flickeringStrength;	// 明滅の強さ(0 で掛けない)
};

#endif
