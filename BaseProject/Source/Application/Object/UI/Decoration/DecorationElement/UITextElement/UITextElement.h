#pragma once
#include "../UIDecorationElementBase.h"
#include "Engine/Graphics/Frame/DrawSubmitter/Descs/FontSubmitDesc.h"		// 行揃え(ETextAlign)

namespace App::Object::Decoration
{
	//======================================================================================
	// 文字のエレメント
	//
	// フォントから文字列を組んで出す。グリフの並べ方はエンジン側(DrawSubmitter::SubmitString)。
	// 大きさは pixelSize ではなく文字の大きさ(fontPixelSize)で決まり、ピボットは文字列のブロック全体に効く
	//======================================================================================
	class UITextElement final : public UIDecorationElementBase
	{
	public:

		// 文字列の行揃え : ブロック全体の位置は pivot が決めるので、ここは行同士の揃え方
		using ETextAlign = Engine::Graphics::ETextAlign;

	public:

		//------------------------------------------------------------------------------------
		// 種類
		//------------------------------------------------------------------------------------
		static constexpr EElementType TYPE = EElementType::Text;
		EElementType GetType() const override { return TYPE; }
		const char* GetTypeName() const override { return "Text"; }
		std::unique_ptr<UIDecorationElementBase> Clone() const override { return std::make_unique<UITextElement>(*this); }

		//------------------------------------------------------------------------------------
		// メイン処理
		//------------------------------------------------------------------------------------
		void Draw(const DrawContext& a_context) override;
		void RequestResources(Engine::Resource::ResourceManager* a_pResourceManager) override;

		//------------------------------------------------------------------------------------
		// アクセサ
		//------------------------------------------------------------------------------------

		// 文字列
		const std::string& GetValue() const { return m_value; }
		void SetValue(const std::string& a_value) { m_value = a_value; }

		// フォント : 差し替えたら RequestResources で参照を引き直す
		const Core::GUID& GetFontGUID() const { return m_fontGUID; }
		const Engine::ResourceRef<Engine::Resource::Font>& GetFontRef() const { return m_fontRef; }
		void SetFontGUID(const Core::GUID& a_fontGUID) { m_fontGUID = a_fontGUID; }

		// 文字の組み方
		float GetFontPixelSize() const { return m_fontPixelSize; }
		void SetFontPixelSize(float a_fontPixelSize) { m_fontPixelSize = a_fontPixelSize; }
		float GetLineSpacing() const { return m_lineSpacing; }
		void SetLineSpacing(float a_lineSpacing) { m_lineSpacing = a_lineSpacing; }
		float GetCharSpacing() const { return m_charSpacing; }
		void SetCharSpacing(float a_charSpacing) { m_charSpacing = a_charSpacing; }
		ETextAlign GetTextAlign() const { return m_textAlign; }
		void SetTextAlign(ETextAlign a_textAlign) { m_textAlign = a_textAlign; }

	protected:

		void ArchiveBody(Engine::Persistence::Archive& a_ar) override;
		bool DrawBodyInspector(const Engine::ECS::EngineServices& a_services) override;
		bool IsUsePixelSize() const override { return false; }

	private:

		//------------------------------------------------------------------------------------
		// 文字データ
		//------------------------------------------------------------------------------------
		std::string m_value = "Text";
		Core::GUID m_fontGUID = {};
		Engine::ResourceRef<Engine::Resource::Font> m_fontRef = {};

		//------------------------------------------------------------------------------------
		// 文字の組み方
		//------------------------------------------------------------------------------------
		float		m_fontPixelSize = 32.0f;				// 出したい文字の高さ px
		float		m_lineSpacing = 1.0f;					// 行送りの倍率(1.0 でフォントの既定)
		float		m_charSpacing = 0.0f;					// 字間へ足す量(px)
		ETextAlign	m_textAlign = ETextAlign::Center;		// 出現位置
	};
}
