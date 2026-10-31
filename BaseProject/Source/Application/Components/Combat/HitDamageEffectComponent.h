#pragma once

#include "Engine/EditorField/EditorField.h"

namespace App::Component
{
	//==========================================================================================
	// HitDamageEffectComponent
	//
	// プレイヤーに付ける、被弾したときの画面効果(DamageEffectPass)の設定。
	//
	// HealthComponent の体力が減ったフレームに効果を立ち上げ、duration 秒かけて消す。
	// 立ち上がりの強さは受けたダメージの大きさで決まる(fullDamageRatio で最大)。
	// 各効果の値は「強さが 1 のときの値」で、そこへ今の強さを掛けて送る。
	//
	// 書くのは HitDamageEffectSystem。パイプラインに DamageEffectPass のノードが要る
	//==========================================================================================
	struct HitDamageEffectComponent
	{
		// ---- 立ち上がりと消え方(保存する) ----
		float duration = 0.55f;			// 消えきるまでの秒数
		float minIntensity = 0.45f;		// 小さいダメージでもこれだけは出す(0〜1)
		float fullDamageRatio = 0.12f;	// 最大体力に対してこの割合を一度に食らうと最大の強さ
		float fadePower = 2.0f;			// 消え方の曲線(大きいほど早く引く)

		// ---- 画面(保存する) ----
		float screenNoise = 0.3f;		// 砂嵐のノイズの濃さ
		float screenGlitch = 0.025f;	// 横の帯ずれの幅(UV)
		float screenRGBShift = 0.005f;	// RGB を左右へずらす幅(UV)
		float screenDesaturate = 0.25f;	// 色を抜く量(0〜1)
		float vignette = 0.45f;			// 縁を染める強さ(0〜1)
		Math::Vector3 vignetteColor = { 0.85f, 0.05f, 0.05f };	// 縁を染める色

		// ---- HUD(保存する) ----
		float hudNoise = 0.6f;			// ノイズの濃さ
		float hudGlitch = 0.035f;		// 横の帯ずれの幅(UV)
		float hudRGBShift = 0.006f;		// RGB を左右へずらす幅(UV)
		float hudFlicker = 0.6f;		// ちらつき(0〜1)

		// ---- 帯ずれの細かさ(保存する) ----
		float glitchBands = 24.0f;		// 縦の分け数
		float glitchRate = 0.35f;		// ずれる帯の割合(0〜1)
		float glitchSpeed = 18.0f;		// ずれ方が切り替わる回数(1秒あたり)

		// ---- ランタイム(保存しない) ----
		float prevHealth = -1.0f;		// 前のフレームの体力(負ならまだ見ていない)
		float timer = 0.0f;				// 立ち上がってからの秒数
		float peakIntensity = 0.0f;		// 立ち上がりの強さ
		float currentIntensity = 0.0f;	// 今の強さ(0〜1)。確認用
		bool  isTestRequested = false;	// インスペクターの試しボタン
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::HitDamageEffectComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::HitDamageEffectComponent& _comp = Engine::EditorField::RefValue<App::Component::HitDamageEffectComponent>(a_pData);
		a_ar.Field("duration", _comp.duration);
		a_ar.Field("minIntensity", _comp.minIntensity);
		a_ar.Field("fullDamageRatio", _comp.fullDamageRatio);
		a_ar.Field("fadePower", _comp.fadePower);

		a_ar.Field("screenNoise", _comp.screenNoise);
		a_ar.Field("screenGlitch", _comp.screenGlitch);
		a_ar.Field("screenRGBShift", _comp.screenRGBShift);
		a_ar.Field("screenDesaturate", _comp.screenDesaturate);
		a_ar.Field("vignette", _comp.vignette);
		a_ar.Field("vignetteColor", _comp.vignetteColor);

		a_ar.Field("hudNoise", _comp.hudNoise);
		a_ar.Field("hudGlitch", _comp.hudGlitch);
		a_ar.Field("hudRGBShift", _comp.hudRGBShift);
		a_ar.Field("hudFlicker", _comp.hudFlicker);

		a_ar.Field("glitchBands", _comp.glitchBands);
		a_ar.Field("glitchRate", _comp.glitchRate);
		a_ar.Field("glitchSpeed", _comp.glitchSpeed);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::HitDamageEffectComponent& _comp = Engine::EditorField::RefValue<App::Component::HitDamageEffectComponent>(a_context.pData);

		Engine::EditorField::HelpText("パイプラインに DamageEffectPass のノードが要る");

		Engine::EditorField::Header("立ち上がりと消え方");
		Engine::EditorField::Field("Duration", _comp.duration, 0.01f, 0.05f, 5.0f);
		Engine::EditorField::Tooltip("消えきるまでの秒数");
		Engine::EditorField::Field("MinIntensity", _comp.minIntensity, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("小さいダメージでもこれだけは出す");
		Engine::EditorField::Field("FullDamageRatio", _comp.fullDamageRatio, 0.005f, 0.001f, 1.0f);
		Engine::EditorField::Tooltip("最大体力に対してこの割合を一度に食らうと最大の強さになる");
		Engine::EditorField::Field("FadePower", _comp.fadePower, 0.05f, 0.1f, 8.0f);
		Engine::EditorField::Tooltip("消え方の曲線。大きいほど早く引く");

		Engine::EditorField::Header("画面");
		Engine::EditorField::Field("Noise", _comp.screenNoise, 0.01f, 0.0f, 2.0f);
		Engine::EditorField::Field("Glitch", _comp.screenGlitch, 0.001f, 0.0f, 0.3f);
		Engine::EditorField::Tooltip("横の帯ずれの幅(画面の幅を 1 とした割合)");
		Engine::EditorField::Field("RGBShift", _comp.screenRGBShift, 0.0005f, 0.0f, 0.05f);
		Engine::EditorField::Tooltip("赤と青を左右へずらす幅(画面の幅を 1 とした割合)");
		Engine::EditorField::Field("Desaturate", _comp.screenDesaturate, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("Vignette", _comp.vignette, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::ColorField("VignetteColor", _comp.vignetteColor);

		Engine::EditorField::Header("HUD");
		Engine::EditorField::Field("HUDNoise", _comp.hudNoise, 0.01f, 0.0f, 2.0f);
		Engine::EditorField::Field("HUDGlitch", _comp.hudGlitch, 0.001f, 0.0f, 0.3f);
		Engine::EditorField::Field("HUDRGBShift", _comp.hudRGBShift, 0.0005f, 0.0f, 0.05f);
		Engine::EditorField::Field("HUDFlicker", _comp.hudFlicker, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("ときどき HUD 全体が薄くなる量");

		Engine::EditorField::Header("帯ずれ");
		Engine::EditorField::Field("GlitchBands", _comp.glitchBands, 0.5f, 1.0f, 200.0f);
		Engine::EditorField::Field("GlitchRate", _comp.glitchRate, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("ずれる帯の割合");
		Engine::EditorField::Field("GlitchSpeed", _comp.glitchSpeed, 0.5f, 0.0f, 120.0f);
		Engine::EditorField::Tooltip("ずれ方が切り替わる回数(1秒あたり)");

		Engine::EditorField::Line();
		if (Engine::EditorField::Button("Test"))
		{
			_comp.isTestRequested = true;
		}
		Engine::EditorField::Tooltip("最大の強さで一度出す(プレイ中のみ)");

		// システムが毎フレーム上書きするので表示のみ
		Engine::EditorField::Value("CurrentIntensity", "%.2f", _comp.currentIntensity);
	}
};
