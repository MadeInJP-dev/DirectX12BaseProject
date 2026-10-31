#pragma once
namespace Engine::Graphics
{
	//------------------------------------------------------------------------------------------
	// 被弾の画面効果(DamageEffectPass)
	//
	// ゲーム側(HitDamageEffectSystem)が毎フレーム SceneView へ入れ、EndFrame で消える。
	// 入れなかったフレームは intensity が 0 になり、パスは絵をそのまま通す。
	//
	// 各効果の値は「intensity が 1 のときの強さ」。シェーダーで intensity を掛ける。
	// 画面(シーン)と HUD(UI)で別々に効かせられる
	//------------------------------------------------------------------------------------------
	struct DamageEffectData
	{
		float intensity = 0.0f;			// 掛かり具合(0〜1)。0 なら何もしない

		// ---- 画面(シーン) ----
		float screenNoise = 0.0f;		// 砂嵐のノイズの濃さ
		float screenGlitch = 0.0f;		// 横の帯ずれの幅(UV)
		float screenRGBShift = 0.0f;	// RGB を左右へずらす幅(UV)
		float screenDesaturate = 0.0f;	// 色を抜く量(0〜1)
		float vignette = 0.0f;			// 縁を染める強さ(0〜1)
		Math::Vector3 vignetteColor = { 1.0f, 0.0f, 0.0f };	// 縁を染める色

		// ---- HUD(UI) ----
		float hudNoise = 0.0f;			// ノイズの濃さ(UI の描いてあるところだけ)
		float hudGlitch = 0.0f;			// 横の帯ずれの幅(UV)
		float hudRGBShift = 0.0f;		// RGB を左右へずらす幅(UV)
		float hudFlicker = 0.0f;		// ちらつき(UI がときどき薄くなる量。0〜1)

		// ---- 共通 ----
		float glitchBands = 24.0f;		// 帯ずれの縦の分け数
		float glitchRate = 0.35f;		// ずれる帯の割合(intensity が 1 のとき。0〜1)
		float glitchSpeed = 18.0f;		// ずれ方が切り替わる回数(1秒あたり)
	};
}
