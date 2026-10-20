// ライティングの調整値。DeferredLightingPass が持つ値を毎フレーム流し込む
#ifndef ROOTPARAM_LIGHTING_OPTION_DATA_HLSLI
#define ROOTPARAM_LIGHTING_OPTION_DATA_HLSLI

struct LightingOptionData
{
	float giIntensity;
	float directionalIntensity;
	float dielectricF0;			// 非金属の基本反射率(スペキュラF0)
	float reflectionIntensity;	// 鏡面反射(間接光の鏡面ぶん)の強さ

	// 粗さに応じた鏡面反射の扱い。
	// レイトレの反射は鏡(1本)なので、粗い面ほどぼかし、
	// reflectionRoughnessStart から End にかけて周りの間接光(GI)へ置き換える
	float reflectionRoughnessStart;
	float reflectionRoughnessEnd;
	float reflectionBlurRadius;	// 粗さ End のときのぼかし半径(反射テクスチャの画素数)
	float pad;

	// リムライト(主光源の逆光で輪郭を光らせる)
	float3 rimColor;			// 光の色に掛ける色
	float rimIntensity;			// 強さ
	float rimPower;				// 輪郭の細さ(大きいほど縁だけに寄る)
	float rimBackLightStart;	// 逆光の度合いがこの値から効き始める(-1..1)
	float rimBackLightEnd;		// この値で効き切る
	float rimShadowRate;		// 影で消す割合(0 = 影を無視, 1 = 影で消す)。光に背を向けた面には効かない
};

#endif
