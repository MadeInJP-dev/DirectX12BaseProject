#pragma once

#include "Application/AppCommon.h"

//==========================================================================================
// デコレーションのカーソルへの反応
//
// 親(UI)の状態(乗っている・押している…)と、それに応じて飾りが変える見た目。
// 状態を作るのは UIInteraction、見た目を寄せていくのは Decoration.cpp(AdvanceAnimation)
//==========================================================================================
namespace App::Object::Decoration
{
	//======================================================================================
	// UIの状態 : 飾りが親から受け取る
	//======================================================================================
	enum class EUIState : uint32_t
	{
		Normal = 0,		// 何もされていない
		Hovered,		// カーソルが乗っている
		Pressed,		// 押されている最中
		Disabled,		// 押せない
	};

	// 出す状態の絞り込み用(ビットフラグなので値は2の冪)
	enum class EUIStateFlag : uint32_t
	{
		NONE		= 0,
		NORMAL		= 1 << 0,
		HOVERED		= 1 << 1,
		PRESSED		= 1 << 2,
		DISABLED	= 1 << 3,

		ALL			= NORMAL | HOVERED | PRESSED | DISABLED,
	};
	ENUM_ATTR_BITFLAG(EUIStateFlag);

	/// 状態を絞り込み用のビットへ
	constexpr EUIStateFlag ToStateFlag(EUIState a_state)
	{
		switch (a_state)
		{
		case EUIState::Hovered:  return EUIStateFlag::HOVERED;
		case EUIState::Pressed:  return EUIStateFlag::PRESSED;
		case EUIState::Disabled: return EUIStateFlag::DISABLED;

		case EUIState::Normal:
		default:                 return EUIStateFlag::NORMAL;
		}
	}

	//--------------------------------------------------------------------------------------
	// 状態1つぶんの見た目 : Normal からの差
	//--------------------------------------------------------------------------------------
	struct UIStateStyle
	{
		Math::Color color = Engine::Color::WHITE;	// 元の色へ乗算(白で変化なし)
		Math::Vector2 scale = { 1.0f, 1.0f };		// 大きさへ乗算(1で等倍)
		Math::Vector2 offsetAdd = {};				// 位置へ加算(px)
	};

	//--------------------------------------------------------------------------------------
	// 親の状態に対する反応
	// アニメーションと同じく 付いている飾りだけがカーソルに反応する。
	//--------------------------------------------------------------------------------------
	struct UIReaction
	{
		// 出す状態 : ここに入っていない状態では描かない
		EUIStateFlag visibleState = EUIStateFlag::ALL;

		// 状態ごとの見た目(Normal は素のまま)
		UIStateStyle hovered = {};	// 重なっている
		UIStateStyle pressed = {};	// 押された
		UIStateStyle disabled = {};	// 押されることがない

		float blendSpeed = 14.0f;

		//---- ランタイム(保存しない) ----
		UIStateStyle current = {};	// いま適用している値。ここを目標へ寄せていく
		float visibleRate = 1.0f;	// 出ている割合(0で完全に透明)
		bool isInitialized = false;	// 初回は補間せずに合わせる(出た瞬間に寄り始めない)
	};
}
