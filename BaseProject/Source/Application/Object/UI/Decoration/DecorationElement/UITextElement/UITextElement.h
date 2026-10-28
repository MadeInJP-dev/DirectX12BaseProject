#pragma once
#include "../UIDecorationElementBase.h"
namespace App::Object::Decoration
{
	class UITextElement : public UIDecorationElementBase
	{
	public:
		
		// 描画
		void Draw(const DrawContext& a_context) override;

	private:

		// 文字列の行揃え : ブロック全体の位置は pivot が決めるので、ここは行同士の揃え方
		enum class ETextAlign : uint32_t
		{
			Left,
			Center,
			Right,
		};

	private:

		//------------------------------------------------------------------------------------
		// 文字データ
		//------------------------------------------------------------------------------------
		std::string m_value = "Text";
		Core::GUID m_fontGUID = {};
		Engine::ResourceRef<Engine::Resource::Font> m_fontRef = {};

		//------------------------------------------------------------------------------------
		// 文字データ
		//------------------------------------------------------------------------------------
		float		m_fontPixelSize = 32.0f;				// 出したい文字の高さ px
		float		m_lineSpacing = 1.0f;					// 行送りの倍率(1.0 でフォントの既定)
		float		m_charSpacing = 0.0f;					// 字間へ足す量(px)
		ETextAlign	m_textAlign = ETextAlign::Center;		// 出現位置
	};
}