#pragma once

namespace App::Component
{
	//==========================================================================================
	// RadialBlurComponent
	//
	// カメラに付ける、放射状ブラー(ラジアルブラー)の設定。
	//
	// blurCenter から外へ向かって絵を引きずり、スピード感を出す画面効果。
	// 引きずる長さは中心からの距離に比例するので、画面中央はほとんど動かず、
	// 端へ行くほど強く流れる。
	//
	//   中心からの距離が radius(UV) を超えたところから効き始め、
	//   falloff の傾きで 0 → 1 へ立ち上がる。
	//
	//------------------------------------------------------------------------------------------
	// 速度レスポンス
	//------------------------------------------------------------------------------------------
	// 強さは手で入れるのではなく、自機の速さから作る。
	// TPSカメラが既に「今どれくらい速いか」を 0..1 へ正規化した値
	// (TPSCameraStateComponent::currentSpeed01)を持っているので、それをそのまま使う。
	// 画角の広がり(fovBoost)と同じ元から効かせることで、両方の演出が足並みを揃える。
	//
	//   t        = speedThreshold から 1 の区間を 0..1 へ引き伸ばしたもの
	//   currentStrength → baseStrength + strengthAtSpeed * t へ responseRate でなまして寄る
	//
	// 実際に currentStrength を書くのは RadialBlurSpeedSystem。
	// アクティブカメラの値を CamSetShaderSystem が GraphicsEngine へ送り、
	// RadialBlurPass が定数バッファとして受け取る。
	//==========================================================================================
	struct RadialBlurComponent
	{
		// ---- 設定(保存される) ----

		// ブラーの中心(UV : 画面左上が {0,0}、右下が {1,1})。既定は画面中央
		Math::Vector2 blurCenter	= { 0.5f, 0.5f };

		// サンプル数。多いほど滑らかだが重い
		int   sampleCount			= 12;

		// ここまで(中心からのUV距離)はボカさない。
		// 注視している真ん中まで流すと何も見えなくなるので必ず残す
		float radius				= 0.15f;

		// radius から先の効きの立ち上がり。大きいほど急に効く
		float falloff				= 2.0f;

		// ---- 速度レスポンス(保存される) ----

		// 速度に関係なく常に掛かる引きずり量(UV単位)。
		// 既定は 0。TPSカメラでないカメラで固定量を掛けたいときだけ使う
		float baseStrength			= 0.0f;

		// 全開(speed01 = 1)のときに baseStrength へ上乗せする量(UV単位)
		float strengthAtSpeed		= 0.22f;

		// 効き始める速さ(0..1)。これ以下ではまったく掛からない。
		// 巡航中にうっすら滲み続けると画面が汚いので、速いときだけ効かせる
		float speedThreshold		= 0.35f;

		// 強さそのものの追従レート(1秒あたりの指数減衰)。
		// speed01 は TPSFollowComponent 側で既になまされているが、
		// ブラーの立ち上がりは画角とは別に詰めたいので独立して持つ。
		// 0 以下にすると速度レスポンスが止まる
		float responseRate			= 5.0f;

		// false なら流さずそのまま通す
		bool  enable				= false;

		// ---- ランタイム(保存しない。確認用) ----

		// 速さから作った今の引きずり量。RadialBlurSpeedSystem が毎フレーム書く
		float currentStrength		= 0.0f;

		// 震え(ボスの咆哮など)で足す引きずり量と、中心のずれ(UV)。
		// RadialBlurShakeSystem が RadialBlurShakeResource から毎フレーム書く
		float shakeStrength			= 0.0f;
		Math::Vector2 shakeCenterOffset = { 0.0f, 0.0f };
		float shakeTime				= 0.0f;	// 揺れの位相を進める時計(秒)

		// 震えているか。震えている間は enable が false でも掛ける
		bool IsShaking() const { return shakeStrength > 0.0f; }

		// シェーダーへ送るか
		bool IsActive() const { return enable || IsShaking(); }

		// 実際にシェーダーへ送る引きずり量。切っているカメラは震えのぶんだけ
		float GetStrength() const { return (enable ? baseStrength + currentStrength : 0.0f) + shakeStrength; }

		// 実際にシェーダーへ送る中心(UV)
		Math::Vector2 GetCenter() const { return blurCenter + shakeCenterOffset; }
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::RadialBlurComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::RadialBlurComponent& _comp = Engine::EditorField::RefValue<App::Component::RadialBlurComponent>(a_pData);
		a_ar.Field("blurCenter",      _comp.blurCenter);
		a_ar.Field("sampleCount",     _comp.sampleCount);
		a_ar.Field("radius",          _comp.radius);
		a_ar.Field("falloff",         _comp.falloff);
		a_ar.Field("baseStrength",    _comp.baseStrength);
		a_ar.Field("strengthAtSpeed", _comp.strengthAtSpeed);
		a_ar.Field("speedThreshold",  _comp.speedThreshold);
		a_ar.Field("responseRate",    _comp.responseRate);
		a_ar.Field("enable",          _comp.enable);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::RadialBlurComponent& _comp = Engine::EditorField::RefValue<App::Component::RadialBlurComponent>(a_context.pData);

		Engine::EditorField::Field("RadialBlur Enable", _comp.enable);
		Engine::EditorField::Field("BlurCenter (UV)", _comp.blurCenter, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("画面左上が 0,0 / 右下が 1,1");
		Engine::EditorField::Field("SampleCount", _comp.sampleCount, 1.0f, 1, 64);
		Engine::EditorField::Field("Radius (UV)", _comp.radius, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("この内側はボカさない");
		Engine::EditorField::Field("Falloff", _comp.falloff, 0.1f, 0.0f, 32.0f);

		Engine::EditorField::Line();
		Engine::EditorField::HelpText("Speed Response");
		Engine::EditorField::Field("BaseStrength", _comp.baseStrength, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("速度に関係なく常に掛かる量");
		Engine::EditorField::Field("StrengthAtSpeed", _comp.strengthAtSpeed, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("SpeedThreshold", _comp.speedThreshold, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("これ以下の速さでは掛からない");
		Engine::EditorField::Field("ResponseRate", _comp.responseRate, 0.1f, 0.0f, 60.0f);
		Engine::EditorField::Tooltip("速さの基準は TPSFollowComponent の SpeedReference");

		// システムが毎フレーム上書きするので表示のみ
		Engine::EditorField::Line();
		Engine::EditorField::Value("CurrentStrength", "%.3f", _comp.currentStrength);
		Engine::EditorField::Value("ShakeStrength", "%.3f", _comp.shakeStrength);
		Engine::EditorField::Value("SendStrength", "%.3f", _comp.GetStrength());
	}
};
