#pragma once

#include "Application/Object/UI/Decoration/Decoration.h"

//==========================================================================================
// デコレーションの実装どうしで共有するもの(Decoration*.cpp 専用。外からは読まないこと)
//==========================================================================================
namespace App::Object::Decoration::Internal
{
	// 度で回す : UIAnchor::IsPointInside と同じ向き(時計回り)
	inline Math::Vector2 RotateDeg(const Math::Vector2& a_value, float a_degree)
	{
		if (a_degree == 0.0f) return a_value;

		const float _rad = DirectX::XMConvertToRadians(a_degree);
		const float _cos = std::cos(_rad);
		const float _sin = std::sin(_rad);

		return {
			a_value.x * _cos - a_value.y * _sin,
			a_value.x * _sin + a_value.y * _cos
		};
	}

	inline float Lerp(float a_start, float a_end, float a_rate)
	{
		return a_start + (a_end - a_start) * a_rate;
	}
	inline Math::Vector2 Lerp(const Math::Vector2& a_start, const Math::Vector2& a_end, float a_rate)
	{
		return { Lerp(a_start.x, a_end.x, a_rate), Lerp(a_start.y, a_end.y, a_rate) };
	}
	inline Math::Color Lerp(const Math::Color& a_start, const Math::Color& a_end, float a_rate)
	{
		return {
			Lerp(a_start.r, a_end.r, a_rate),
			Lerp(a_start.g, a_end.g, a_rate),
			Lerp(a_start.b, a_end.b, a_rate),
			Lerp(a_start.a, a_end.a, a_rate)
		};
	}

	//--------------------------------------------------------------------------------------
	// アニメーションを合成した結果
	//--------------------------------------------------------------------------------------
	struct AnimResult
	{
		Math::Vector2 positionAdd = {};				// 位置へ足す(px, 親の倍率が掛かる前)
		Math::Vector2 scaleMul = { 1.0f, 1.0f };	// 大きさへ掛ける
		float rotationAdd = 0.0f;					// 回転へ足す(度)
		Math::Color colorMul = Engine::Color::WHITE;// 色へ掛ける
		Math::Vector2 uvAdd = {};					// UVオフセットへ足す
	};

	/// <summary>
	/// アニメーションと反応を1つにまとめる
	/// </summary>
	/// <remarks>
	/// 描画(DecorationDraw.cpp)と当たり判定(CalcCurrentTransform)の両方がここを通る。
	/// 別々に組み立てると、絵は大きくなっているのに判定は素のまま、というずれが起きる
	/// </remarks>
	AnimResult MakeAnimResult(const Decoration& a_decoration);
}
