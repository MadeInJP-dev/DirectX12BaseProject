#pragma once
namespace Engine::Graphics
{
	//------------------------------------------------------------------------------------------
	// カメラ発の画面効果
	//
	// アクティブカメラのコンポーネントを CamSetShaderSystem が詰め、SceneView が今フレームぶん持つ。
	// パスは SceneView から読んで送るだけ。
	//------------------------------------------------------------------------------------------

	// 被写界深度(DoF)の調整値
	// アクティブカメラの FocusParamComponent を CamSetShaderSystem が詰め、
	// CoCパスとDoFパスの両方へ送る。
	// 後ろのぼかし(backdrop)はカメラではなく SceneView が送る直前に重ねる(SceneView::ApplyBackdropBlur)
	// ※ HLSL 側(Asset/Shader/Common/RootParameters/DoFOptionData.hlsli)と並びを合わせること
	struct DoFOptionCB
	{
		float focusDistance;	// ピントが合う距離(カメラからの深度)
		float focusRange;		// ピントが合う幅
		float nearRange;		// 手前側が最大ボケになるまでの距離
		float farRange;			// 奥側が最大ボケになるまでの距離

		float maxBlurRadius;	// 最大ボケ半径(ピクセル)
		int   enable;			// 0 ならボカさずそのまま通す
		float backdropBlur;		// 後ろのぼかしの掛かり具合(0〜1)。画面全体をピントに関係なくぼかす
		float backdropRadius;	// 後ろのぼかしの半径(ピクセル)
	};

	// ラジアルブラーの調整値
	// アクティブカメラの RadialBlurComponent を CamSetShaderSystem が詰め、
	// RadialBlurPass へ送る。
	// ※ HLSL 側(Asset/Shader/Common/RootParameters/RadialBlurOptionData.hlsli)と並びを合わせること
	struct RadialBlurOptionCB
	{
		Math::Vector2 blurCenter;	// ブラーの中心(UV : 画面左上が0、右下が1)
		float strength;					// 引きずる長さ(UV単位。中心からの距離に比例して伸びる)
		int   sampleCount;				// サンプル数

		float radius;					// ここまで(中心からのUV距離)はボカさない
		float falloff;					// radius から先の効きの立ち上がり
		int   enable;					// 0 ならボカさずそのまま通す
		float pad0;
	};

	// 魚眼レンズの調整値
	// アクティブカメラの FishEyeComponent を CamSetShaderSystem が詰めて送る。
	// ※ HLSL 側(Asset/Shader/Common/RootParameters/FishEyeOptionData.hlsli)と並びを合わせること
	struct FishEyeOptionCB
	{
		Math::Vector2 center;		// 歪みの中心(UV : 画面左上が0、右下が1)
		float strength;					// 歪みの強さ(0で歪まない。正で樽型、負で糸巻き型)
		int   enable;					// 0 なら歪ませずそのまま通す
	};
}
