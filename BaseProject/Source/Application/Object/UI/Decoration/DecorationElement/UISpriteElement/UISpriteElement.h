#pragma once
#include "../UIDecorationElementBase.h"

namespace App::Object::Decoration
{
	class UISpriteElement : public UIDecorationElementBase
	{
	public:

		// 描画
		void Draw(const DrawContext& a_context) override;

	private:

		//------------------------------------------------------------------------------------
		// ポリゴンデータ : 画像が設定されていなければこのまま出る
		//------------------------------------------------------------------------------------
		Math::Vector2	m_uvOffset = {};				// UVスクロール・コマ送り
		Math::Vector2	m_uvScale = { 1.0f, 1.0f };		// 1枚に並べた絵から1コマ切り出すときの倍率

		// 枠
		bool		m_isFill = true;					// 中を塗るか(false で枠だけ)
		Math::Color m_edgeColor = Engine::Color::WHITE;	// 枠の色(親の色へ乗算)
		float		m_edgePixel = 0.0f;					// 枠の太さ(px)。0 で枠なし
		EDirection	m_edgeSide = EDirection::ALL;		// どの辺に出すか

		//------------------------------------------------------------------------------------
		// 画像データ : 任意項目
		//------------------------------------------------------------------------------------
		struct Imgae
		{
			Core::GUID texGUID = {};
			Engine::ResourceRef<Engine::Resource::Texture> texRef = {};		// ランタイム用

			Math::Vector2	uvOffset = {};				// UVスクロール・コマ送り
			Math::Vector2	uvScale = { 1.0f, 1.0f };	// 1枚に並べた絵から1コマ切り出すときの倍率	
		};

		std::optional<Imgae> m_noiseImage = std::nullopt;		// ノイズ画像
		std::optional<Imgae> m_maskImage = std::nullopt;		// マスク画像
		std::optional<Imgae> m_image = std::nullopt;			// 画像データ

	};
}