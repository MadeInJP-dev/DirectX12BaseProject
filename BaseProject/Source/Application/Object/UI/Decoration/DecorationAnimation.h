#pragma once

#include "Application/AppCommon.h"

//==========================================================================================
// デコレーションのアニメーション
//
// 飾り1つに付けられる動き(トゥイーン・周期運動)と、その進み具合の曲線(イージング)。
// 進める処理は Decoration.cpp(AdvanceAnimation)にある
//==========================================================================================
namespace App::Object::Decoration
{
	//======================================================================================
	// イージング
	//======================================================================================
	enum class EEase : uint32_t
	{
		Linear,			// 等速
		InQuad,			// ゆっくり始まる
		OutQuad,		// ゆっくり終わる
		InOutQuad,		// 両端がゆっくり
		InCubic,
		OutCubic,
		InOutCubic,
		OutBack,		// 行き過ぎて戻る
		OutElastic,		// 弾んで止まる
	};

	/// 0〜1 の進み具合にイージングを掛ける
	float Ease(EEase a_ease, float a_rate);

	//======================================================================================
	// アニメーションで動かすチャンネル
	//======================================================================================
	enum class EAnimChannel : uint32_t
	{
		NONE		= 0,
		COLOR		= 1 << 0,	// 色(乗算)
		POSITION	= 1 << 1,	// 位置(px, 加算)
		SCALE		= 1 << 2,	// 大きさ(倍率, 乗算)
		ROTATION	= 1 << 3,	// 回転(度, 加算)
		UV			= 1 << 4,	// UVオフセット(加算)

		ALL			= COLOR | POSITION | SCALE | ROTATION | UV,
	};
	ENUM_ATTR_BITFLAG(EAnimChannel);

	//--------------------------------------------------------------------------------------
	// イージングアニメーション
	//--------------------------------------------------------------------------------------
	template<typename T>
	struct AnimElement
	{
		T start = {};
		T end = {};
	};

	//--------------------------------------------------------------------------------------
	// 周期運動
	//--------------------------------------------------------------------------------------
	template<typename T>
	struct Oscillation
	{
		T amplitude = {};
		float frequency = 1.0f;		// 1秒あたりの往復回数
		float phase = 0.0f;			// 位相のずれ(0〜1で1周)

		// 上下限で挟む
		bool isUseLimit = false;
		T minValue = {};			// 下限(波の底で届く値)
		T maxValue = {};			// 上限(波の頂点で届く値)
	};

	//--------------------------------------------------------------------------------------
	// 始点から終点へ1回だけ動かす(トゥイーン)
	//--------------------------------------------------------------------------------------
	struct UIAnimation
	{
		float currentTime = 0.0f;		// 経過時間
		float durationTime = 1.0f;		// 変化するのにかかる時間
		bool isLoop = false;			// ループ設定
		bool isPingPong = false;		// 往復するか(往きで end、復りで start へ戻る)

		EEase ease = EEase::Linear;					// 進み具合の曲線
		EAnimChannel channels = EAnimChannel::NONE;	// 動かすチャンネル

		// TweenAnimデータ : channels で選んだものだけが効く
		AnimElement<Math::Color> color = {};		// 元の色へ乗算
		AnimElement<Math::Vector2> position = {};	// 位置へ加算(px)
		AnimElement<Math::Vector2> scale = {};		// 大きさへ乗算(1で等倍)
		AnimElement<float> rotation = {};			// 回転へ加算(度)
		AnimElement<Math::Vector2> uv = {};			// UVオフセットへ加算
	};

	//--------------------------------------------------------------------------------------
	// ずっと揺らし続ける(周期運動)
	//--------------------------------------------------------------------------------------
	struct UIProceduralAnimation
	{
		float currentTime = 0.0f;					// 経過時間
		EAnimChannel channels = EAnimChannel::NONE;	// 動かすチャンネル

		Oscillation<Math::Vector2> position = {};	// 位置へ加算(px)
		Oscillation<Math::Vector2> scale = {};		// 大きさへ乗算(1が等倍)
		Oscillation<float> rotation = {};			// 回転へ加算(度)
		Oscillation<Math::Color> color = {};		// 色へ乗算(1が元の色)
	};
}
