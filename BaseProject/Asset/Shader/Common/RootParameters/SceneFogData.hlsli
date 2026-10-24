// シーン全体に掛かるボリュメトリックフォグの調整値。シーンの環境設定(SceneAmbient)の値を SceneVolumetricFogPass が送る。
//
// ※ CPU 側 Engine::Graphics::SceneFogCB と並びを合わせること
#ifndef ROOTPARAM_SCENE_FOG_DATA_HLSLI
#define ROOTPARAM_SCENE_FOG_DATA_HLSLI

struct SceneFogData
{
	float3 fogColor;	// フォグの色(届いた光に掛ける)
	float density;		// 濃さ(1m あたり)。0 ならシーンのフォグは掛からない

	float maxDistance;	// 空(何も描かれていない画素)へ向けて積分する距離(m)
	float anisotropy;	// 平行光を散らす向きの偏り(-1..1)。正で光源の方向を見たときに明るい
	float lightScale;	// 媒質に届く光(環境光 + 平行光)に掛ける倍率
	float pad0;
};

// フォグを照らす点光源(1つだけ)。演出(ボスの死亡など)が毎フレーム積み、SceneVolumetricFogPass が送る。
//
// ※ CPU 側 Engine::Graphics::FogPointLightCB と並びを合わせること
struct FogPointLightData
{
	float3 position;		// 位置(ワールド)
	float range;			// 光の届く距離(m)。ここで 0 になる

	float3 color;			// 色
	float intensity;		// 色に掛ける強さ

	int enable;				// 0 なら点光源なし
	uint shadowStepCount;	// 視線のうち光の届く範囲を歩く歩数(1歩ごとに影のレイを1本)。0 なら遮らない
	int isShadowValid;		// レイトレのワールドが組めたか(0 ならレイを飛ばさない)
	float pad0;
};

#endif
