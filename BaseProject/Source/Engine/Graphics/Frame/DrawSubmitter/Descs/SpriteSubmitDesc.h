#pragma once

namespace Engine::Resource
{
	class Texture;
}

namespace Engine::Graphics
{
	struct UIClipRect;

	//==========================================================================================
	// 2D描画命令用構造体
	//
	// 以下の項目は必須なため入力時に入れること
	// .texHandle
	// .pixelPos
	// .pixelSize
	//==========================================================================================
	struct SpriteSubmitDesc
	{
		// 必須項目
		Handle<Resource::Texture> texHandle;		// テクスチャ
		Math::Vector2 pixelPos;						// ピクセル上の座標
		Math::Vector2 pixelSize;					// ピクセル上のサイズ

		// 設定項目
		float rotationDeg = 0;						// Z軸回転角度 : 度
		float Zlayer = 0;							// Z順 : 大きいほど上に描画される

		// スプライトスタイル
		Math::Color color = Math::Color::White();	// 色
		Math::Vector2 uvOffset = { 0,0 };			// UVのオフセット値
		Math::Vector2 pivot = { 0.5f, 0.5f };		// 中心点 : 回転軸、基準点
		Math::Vector2 uvScale = { 1.0f, 1.0f };		// UVの拡大値

		float curveK = 0.0f;						// 湾曲度
		float curveOffsetX = 0.0f;					// 湾曲の中心からこのクアッドまでの横ずれ

		const UIClipRect* pClipRect = nullptr;		// 切り抜く矩形 : レクトアングル

		UINT postEffectFlags = 0;					// ポストプロセスの印 : Mask用 0 なら何も掛けない
	};
}