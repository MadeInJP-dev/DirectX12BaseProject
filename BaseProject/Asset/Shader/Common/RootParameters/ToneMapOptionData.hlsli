// トーンマップの設定。曲線は絵作りの好みで切り替えたいのでシェーダーではなく値で持つ。
// ※ CPU 側 ToneMapPass::ToneMapCB::type に入る値。数値を変えないこと(パイプラインに保存されている)
#ifndef ROOTPARAM_TONEMAP_OPTION_DATA_HLSLI
#define ROOTPARAM_TONEMAP_OPTION_DATA_HLSLI

#define TONEMAP_TYPE_NONE				0	// 0〜1へ切り詰めるだけ
#define TONEMAP_TYPE_ACES				1
#define TONEMAP_TYPE_REINHARD			2
#define TONEMAP_TYPE_REINHARD_EXTENDED	3
#define TONEMAP_TYPE_UNCHARTED2			4

struct ToneMapOptionData
{
	uint	type;			// 上の TONEMAP_TYPE_*
	float	exposure;		// トーンマップ前に乗せる露出倍率
	float	whitePoint;		// ReinhardExtended / Uncharted2 のみ使用
	float	pad;
};

#endif
