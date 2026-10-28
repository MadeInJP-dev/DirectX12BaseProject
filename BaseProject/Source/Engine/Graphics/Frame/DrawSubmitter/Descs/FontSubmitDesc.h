#pragma once
#include "Engine/Graphics/Frame/DrawSubmitter/Descs/SpriteSubmitDesc.h"

namespace Engine::Resource
{
	class Font;
}

namespace Engine::Graphics
{
	struct UIClipRect;

	//==========================================================================================
	// 文字列の行揃え : ブロック全体の位置は pivot が決めるので、ここは行同士の揃え方
	//==========================================================================================
	enum class ETextAlign : uint32_t
	{
		Left,
		Center,
		Right,
	};

	//==========================================================================================
	// 2D文字列描画命令用構造体
	//
	// 以下の項目は必須なため入力時に入れること
	// .fontHandle
	// .text
	// .pixelPos
	//==========================================================================================
	struct FontSubmitDesc
	{
		// 必須項目
		Handle<Resource::Font> fontHandle;			// フォント
		std::string_view text;						// 文字列(UTF-8, \n で改行)。積む間だけ生きていればよい
		Math::Vector2 pixelPos;						// ピボットが乗るピクセル上の座標

		// 設定項目
		float fontPixelSize = 32.0f;				// 出したい文字の高さ(px)
		float rotationDeg = 0;						// Z軸回転角度 : 度。pixelPos を軸に文字列全体が回る
		float Zlayer = 0;							// Z順 : 大きいほど上に描画される

		// 文字スタイル
		Math::Color color = Math::Color::White();	// 文字色
		Math::Vector2 pivot = { 0.0f, 0.0f };		// 基準点 : 文字列のブロック全体に対して効く
		ETextAlign textAlign = ETextAlign::Left;	// 行揃え
		float charSpacing = 0.0f;					// 字間へ足す量(px)
		float lineSpacing = 1.0f;					// 行送りの倍率(1.0 でフォントの既定)

		float curvature = 0.0f;						// 湾曲 : 曲率(1/px, 符号付き)。0 で曲げない
		float curveOffsetX = 0.0f;					// 湾曲 : 弧の頂点から pixelPos までの横ずれ(回転前の px, 右が正)
		float curveOffsetY = 0.0f;					// 湾曲 : 弧の頂点から pixelPos までの縦ずれ(回転前の px, 下が正)

		const UIClipRect* pClipRect = nullptr;		// 切り抜く矩形 : レクトアングル

		UINT postEffectFlags = 0;					// ポストプロセスの印 : Mask用 0 なら何も掛けない

		UIEffectDesc effect = {};					// 掛ける効果(UIマテリアル)
	};
}
