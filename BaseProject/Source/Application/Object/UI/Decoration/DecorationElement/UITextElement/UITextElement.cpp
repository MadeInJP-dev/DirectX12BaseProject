#include "UITextElement.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"

namespace App::Object::Decoration
{
	//----------------------------------------------------------------------------------
	// 描画
	//
	// グリフの並べ方はエンジン側(DrawSubmitter::SubmitString)が持つ。
	// ここでは親と合成した状態を、文字列の描画命令の形へ直して渡すだけ
	//----------------------------------------------------------------------------------
	void UITextElement::Draw(const DrawContext& a_context)
	{
		if (!IsDrawn(a_context)) return;
		if (m_value.empty()) return;

		// 文字アセットの存在チェック(読み込み待ちは SubmitString 側で弾く)
		if (!m_fontRef.IsValid()) return;

		const Internal::Resolved _resolved = Resolve(a_context);

		Internal::TextLayout _layout = {};
		_layout.fontHandle = m_fontRef.GetRaw();
		_layout.text = m_value;
		_layout.fontPixelSize = m_fontPixelSize;
		_layout.lineSpacing = m_lineSpacing;
		_layout.charSpacing = m_charSpacing;
		_layout.textAlign = m_textAlign;
		Internal::SubmitText(a_context.pGraphicsEngine, _resolved, m_pivotPos, _layout);
	}

	//----------------------------------------------------------------------------------
	// GUIDから参照を引き直す
	//----------------------------------------------------------------------------------
	void UITextElement::RequestResources(Engine::Resource::ResourceManager* a_pResourceManager)
	{
		if (a_pResourceManager == nullptr) return;

		// 実体の到着は待たない。描画側が IsReady を見てスキップする
		m_fontRef = m_fontGUID.IsValid()
			? a_pResourceManager->RequestLoad<Engine::Resource::Font>(m_fontGUID)
			: Engine::ResourceRef<Engine::Resource::Font>();
	}

	//----------------------------------------------------------------------------------
	// 保存
	//----------------------------------------------------------------------------------
	void UITextElement::ArchiveBody(Engine::Persistence::Archive& a_ar)
	{
		a_ar.StringField("Text", m_value);
		a_ar.GUIDField("FontGUID", m_fontGUID);
		a_ar.Field("FontPixelSize", m_fontPixelSize);
		a_ar.Field("LineSpacing", m_lineSpacing);
		a_ar.Field("CharSpacing", m_charSpacing);
		a_ar.Field("TextAlign", m_textAlign);
	}
}
