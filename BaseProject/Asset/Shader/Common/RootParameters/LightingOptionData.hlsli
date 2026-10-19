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
};

#endif
