#pragma once

#include "Engine/EditorField/EditorField.h"

namespace App::Component
{
	//==========================================================================================
	// SpeedLineComponent
	//
	// カメラに付ける、速いときにカメラのまわりを流れる風の筋(スピードライン)の設定。
	//
	// 筋そのものはカメラに付けたエフェクト(EffectAssetComponent)が出す。
	// このコンポーネントは「今どれくらい出すか」を、エフェクトの個体パラメータ
	// (EffectOverrideComponent::params[0])へ 0..1 で書くための設定だけを持つ。
	// どれだけ効くか(発生数など)はエフェクトアセットの結び付け(ParamBindings)が決める。
	//
	//   t = speedThreshold から 1 の区間を 0..1 へ引き伸ばしたもの
	//
	// 速さはラジアルブラーや画角と同じく TPSCameraStateComponent::currentSpeed01 を使う。
	// 同じ元から引くので、画角・ブラー・筋が足並みを揃えて立ち上がる。
	// 書くのは SpeedLineSystem。
	//==========================================================================================
	struct SpeedLineComponent
	{
		// ---- 設定(保存する) ----

		// 出し始める速さ(0..1)。これ以下では筋は出ない
		float speedThreshold = 0.45f;

		// ---- ランタイム(保存しない。確認用) ----

		// 今エフェクトへ渡している値(0..1)
		float currentAmount = 0.0f;
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::SpeedLineComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::SpeedLineComponent& _comp = Engine::EditorField::RefValue<App::Component::SpeedLineComponent>(a_pData);
		a_ar.Field("speedThreshold", _comp.speedThreshold);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::SpeedLineComponent& _comp = Engine::EditorField::RefValue<App::Component::SpeedLineComponent>(a_context.pData);

		Engine::EditorField::Field("SpeedThreshold", _comp.speedThreshold, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("これ以下の速さ(0..1)では筋は出ない。速さの基準は TPSFollowComponent の SpeedReference");
		Engine::EditorField::HelpText("筋はカメラの EffectAssetComponent が出す。量は params[0] の結び付けで決まる");

		// システムが毎フレーム上書きするので表示のみ
		Engine::EditorField::Value("CurrentAmount", "%.2f", _comp.currentAmount);
	}
};
