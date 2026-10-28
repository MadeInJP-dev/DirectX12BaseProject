#include "UITextElement.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "../../UIDecoration.h"

namespace App::Object::Decoration
{
	//----------------------------------------------------------------------------------
	// 描画
	//
	// グリフの並べ方はエンジン側(DrawSubmitter::SubmitString)が持つ。
	// ここでは自分の設定を、文字列の描画命令の形へ直して渡すだけ
	//----------------------------------------------------------------------------------
	void UITextElement::Draw(const DrawContext& a_context)
	{
		if (!m_isVisible) return;
		if (m_value.empty()) return;

		// Manager類の存在チェックは上で終わっている前提
		auto* _pGraphicsEngine = a_context.pGraphicsEngine;

		// 文字アセットの存在チェック(読み込み待ちは SubmitString 側で弾く)
		if (!m_fontRef.IsValid()) return;

		auto* _pParent = a_context.pParent;
		if (!_pParent) return;
		const float _parentScale = _pParent->GetScale();

		Engine::Graphics::ETextAlign _textAlign = Engine::Graphics::ETextAlign::Left;
		switch (m_textAlign)
		{
		case ETextAlign::Center: _textAlign = Engine::Graphics::ETextAlign::Center; break;
		case ETextAlign::Right:  _textAlign = Engine::Graphics::ETextAlign::Right;  break;
		case ETextAlign::Left:
		default: break;
		}

		Engine::Graphics::FontSubmitDesc _desc = {};
		_desc.fontHandle = m_fontRef.GetRaw();
		_desc.text = m_value;
		_desc.pixelPos = m_offsetPos;		// 親の位置はまだ持っていないので、親からのずれをそのまま使う
		_desc.fontPixelSize = m_fontPixelSize * _parentScale;
		_desc.rotationDeg = m_rotation;
		_desc.Zlayer = m_layerOffset;
		_desc.color = m_color;
		_desc.pivot = m_pivotPos;
		_desc.textAlign = _textAlign;
		_desc.charSpacing = m_charSpacing * _parentScale;
		_desc.lineSpacing = m_lineSpacing;
		_pGraphicsEngine->RefDrawSubmitter()->SubmitString(_desc);
	}
}