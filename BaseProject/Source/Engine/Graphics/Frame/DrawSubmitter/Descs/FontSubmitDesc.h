#pragma once

namespace Engine::Graphics
{
	//==========================================================================================
	// 2D文字列描画命令用構造体
	// 
	// 以下の項目は必須なため入力時に入れること
	// .fontHandle
	// .text
	// .pos
	//==========================================================================================
	struct TextSubmitDesc
	{
		// 必須項目
		Handle<Resource::Font> fontHandle;			// 描画させたいフォント形式
		std::string text;							// 文字列の内容
		Math::Vector2 pos;							// スクリーン上の位置

		// フォントスタイルの乗算地
		float rot = 0.0f;							// 回転 : Z軸のみ,度
		float scale = 1.0f;							// 拡大率 : フォントサイズに掛け合わせる値

		// フォントスタイル
		Math::Color color = Math::Color::White();	// 文字色
		uint32_t fontSize = 32;						// 文字サイズ
		float letterSpacing = 0.0f;					// 文字と文字の間の間隔
	};
}