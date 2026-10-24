#pragma once
namespace Engine::Graphics
{
	//------------------------------------------------------------------------------------------
	// シーンのボリュメトリックフォグ
	//
	// 値はシーン(Engine::Scene::SceneAmbient)の持ち物で、毎フレーム SceneView へ流し込まれる。
	// パス(SceneVolumetricFogPass / SceneFogCompositePass)は受け取って送るだけ。
	// シーンのフォグとグラウンドダストで CB を分けてある。
	//------------------------------------------------------------------------------------------
	// シーン全体に一様に漂うフォグの調整値
	// ※ HLSL 側(Asset/Shader/Common/RootParameters/SceneFogData.hlsli)と並びを合わせること
	struct SceneFogCB
	{
		Math::Vector3 fogColor = { 0.6f, 0.7f, 0.8f };	// フォグの色(届いた光に掛ける)
		float density = 0.002f;		// 濃さ(1m あたり)。0 ならシーンのフォグは掛からない

		float maxDistance = 1000.0f;	// 空(何も描かれていない画素)へ向けて積分する距離(m)
		float anisotropy = 0.3f;		// 平行光を散らす向きの偏り(-1..1)。正で光源の方向を見たときに明るい
		float lightScale = 1.0f;		// 媒質に届く光(環境光 + 平行光)に掛ける倍率
		float pad0 = 0.0f;
	};

	// 地面から一定の高さまで漂うチリ(グラウンドダスト)の調整値
	// time はパスが毎フレーム上書きする。
	// ※ HLSL 側(Asset/Shader/Common/RootParameters/GroundDustData.hlsli)と並びを合わせること
	struct GroundDustCB
	{
		Math::Vector3 dustColor = { 0.8f, 0.75f, 0.65f };	// チリの色
		float density = 0.2f;		// 濃さ(1m あたり)。0 ならチリは出ない

		float height = 2.0f;		// チリが立つ高さ(地面から。この高さで濃さが 0 になる)
		float noiseScale = 0.1f;	// ノイズのワールド座標に掛ける倍率(大きいほど細かい)
		float time = 0.0f;			// パスが回り始めてからの経過時間(秒)。ノイズを流すのに使う
		float stepSize = 0.5f;		// チリの層の中をレイマーチする1歩の長さ(m)
	};

	// フォグを照らす点光源(1つだけ)。光の筋(ゴッドレイ)を点から全方位へこぼすのに使う。
	//
	// シーンの持ち物ではなく、演出(ボスの死亡など)が毎フレーム SceneView へ積む。
	// SceneView::EndFrame で消えるので、点けている間は毎フレーム積み直すこと。
	// 面(地面・物体)は照らさない。面も照らしたいときは LightManager のポイントライトを別に置く。
	//
	// 遮るものはレイトレのワールド(RayTag を持つもの / 演出側が登録したもの)。
	// shadowStepCount が 0 なら遮らずに照らす。
	// ※ HLSL 側(Asset/Shader/Common/RootParameters/SceneFogData.hlsli の FogPointLightData)と並びを合わせること
	struct FogPointLightCB
	{
		Math::Vector3 position = {};				// 位置(ワールド)
		float range = 100.0f;						// 光の届く距離(m)。ここで 0 になる

		Math::Vector3 color = { 1.0f, 1.0f, 1.0f };	// 色
		float intensity = 0.0f;						// 色に掛ける強さ(減衰はディファードのポイントライトと同じ式)

		int enable = 0;								// 0 なら点光源なし
		uint32_t shadowStepCount = 0;				// 視線のうち光の届く範囲を歩く歩数(1歩ごとに影のレイを1本)。0 なら遮らない
		int isShadowValid = 0;						// レイトレのワールドが組めたか(パスが書く。積む側は触らない)
		float pad0 = 0.0f;
	};

	// フォグをメインカラーへ重ねるときの調整値(SceneFogCompositePass が送る)
	// ※ HLSL 側(SceneFogCompositeCS の SceneFogCompositeData)と並びを合わせること
	struct SceneFogCompositeCB
	{
		float intensity = 1.0f;		// フォグの濃さに掛ける倍率
		int   enable = 1;			// 0 なら重ねずにそのまま通す
		float pad0[2] = {};
	};
}
