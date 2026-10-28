#pragma once
#include "../UIDecorationElementBase.h"

namespace App::Object::Decoration
{
	//======================================================================================
	// 矩形のエレメント
	//
	// 画像が無ければ組み込みの白テクスチャを色で染めて出す(板ポリ)。
	// 画像を付けると、その絵を出す。枠はどちらでも出せる
	//======================================================================================
	class UISpriteElement final : public UIDecorationElementBase
	{
	public:

		//------------------------------------------------------------------------------------
		// 画像1枚ぶん
		//------------------------------------------------------------------------------------
		struct Image
		{
			Core::GUID texGUID = {};											// 保存用
			Engine::ResourceRef<Engine::Resource::Texture> texRef = {};		// ランタイム用

			Math::Vector2	uvOffset = {};				// UVスクロール・コマ送り
			Math::Vector2	uvScale = { 1.0f, 1.0f };	// 1枚に並べた絵から1コマ切り出すときの倍率
		};

	public:

		//------------------------------------------------------------------------------------
		// 種類
		//------------------------------------------------------------------------------------
		EElementType GetType() const override { return EElementType::Sprite; }
		const char* GetTypeName() const override { return "Sprite"; }
		std::unique_ptr<UIDecorationElementBase> Clone() const override { return std::make_unique<UISpriteElement>(*this); }

		//------------------------------------------------------------------------------------
		// メイン処理
		//------------------------------------------------------------------------------------
		void Draw(const DrawContext& a_context) override;
		void RequestResources(Engine::Resource::ResourceManager* a_pResourceManager) override;

		//------------------------------------------------------------------------------------
		// アクセサ
		//------------------------------------------------------------------------------------

		// ポリゴン
		const Math::Vector2& GetUVOffset() const { return m_uvOffset; }
		void SetUVOffset(const Math::Vector2& a_uvOffset) { m_uvOffset = a_uvOffset; }
		const Math::Vector2& GetUVScale() const { return m_uvScale; }
		void SetUVScale(const Math::Vector2& a_uvScale) { m_uvScale = a_uvScale; }

		// 枠
		bool IsFill() const { return m_isFill; }
		void SetFill(bool a_isFill) { m_isFill = a_isFill; }
		const Math::Color& GetEdgeColor() const { return m_edgeColor; }
		void SetEdgeColor(const Math::Color& a_edgeColor) { m_edgeColor = a_edgeColor; }
		float GetEdgePixel() const { return m_edgePixel; }
		void SetEdgePixel(float a_edgePixel) { m_edgePixel = a_edgePixel; }
		EDirection GetEdgeSide() const { return m_edgeSide; }
		void SetEdgeSide(EDirection a_edgeSide) { m_edgeSide = a_edgeSide; }

		// 画像 : 付けるときは emplace して texGUID を入れ、RequestResources で参照を引く
		std::optional<Image>& RefImage() { return m_image; }
		const std::optional<Image>& GetImage() const { return m_image; }
		std::optional<Image>& RefNoiseImage() { return m_noiseImage; }
		const std::optional<Image>& GetNoiseImage() const { return m_noiseImage; }
		std::optional<Image>& RefMaskImage() { return m_maskImage; }
		const std::optional<Image>& GetMaskImage() const { return m_maskImage; }

	protected:

		void ArchiveBody(Engine::Persistence::Archive& a_ar) override;

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
		std::optional<Image> m_noiseImage = std::nullopt;		// ノイズ画像 : UIマテリアルの効果が引くノイズを差し替える
		std::optional<Image> m_maskImage = std::nullopt;		// マスク画像 : 描画側(SpriteSubmitDesc)に受け口が無いので、まだ描画には使っていない
		std::optional<Image> m_image = std::nullopt;			// 画像データ : UV はポリゴンのものではなくこちらを使う

	};
}
