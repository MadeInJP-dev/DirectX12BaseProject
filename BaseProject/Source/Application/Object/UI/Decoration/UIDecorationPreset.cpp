#include "UIDecoration.h"

#include "Engine/ECS/System/SystemContext.h"	// EngineServices
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Application/Object/UI/Decoration/Decoration.h"
#include "DecorationElement/UISpriteElement/UISpriteElement.h"
#include "DecorationElement/UITextElement/UITextElement.h"

//==========================================================================================
// UIDecoration : プリセット(UIPreset)とのつながり
//
// プリセットの中身は旧 Decoration の JSON。既にあるプリセットとそれを指すシーンを
// そのまま使えるよう、エレメントを旧 Decoration へ変換して旧の仕組み
// (SyncPreset / PushToPreset / DrawPresetInspector)を通し、結果を戻す。
//
// 旧 Decoration に無い値(ParentColor・ノイズ画像・マスク画像)は変換で触らないので、
// 写しても書き戻してもエレメントの値のまま残る
//==========================================================================================
namespace App::Object::Decoration
{
	namespace
	{
		ETextAlign ToDecorationTextAlign(Engine::Graphics::ETextAlign a_textAlign)
		{
			switch (a_textAlign)
			{
			case Engine::Graphics::ETextAlign::Center: return ETextAlign::Center;
			case Engine::Graphics::ETextAlign::Right:  return ETextAlign::Right;
			case Engine::Graphics::ETextAlign::Left:
			default:                                   return ETextAlign::Left;
			}
		}

		Engine::Graphics::ETextAlign ToElementTextAlign(ETextAlign a_textAlign)
		{
			switch (a_textAlign)
			{
			case ETextAlign::Center: return Engine::Graphics::ETextAlign::Center;
			case ETextAlign::Right:  return Engine::Graphics::ETextAlign::Right;
			case ETextAlign::Left:
			default:                 return Engine::Graphics::ETextAlign::Left;
			}
		}
	}

	//======================================================================================
	// 旧 Decoration との変換
	//======================================================================================
	Decoration UIDecoration::ToDecoration(const UIDecorationElementBase& a_element)
	{
		Decoration _decoration = {};

		// 共通
		_decoration.id = a_element.m_id.IsValid() ? a_element.m_id.value : 0;
		_decoration.name = a_element.m_name;
		_decoration.isVisible = a_element.m_isVisible;
		_decoration.group = a_element.m_group;

		_decoration.offsetPos = a_element.m_offsetPos;
		_decoration.pixelSize = a_element.m_pixelSize;
		_decoration.rotation = a_element.m_rotation;
		_decoration.scale = a_element.m_scale;
		_decoration.pivot = a_element.m_pivotPos;
		_decoration.layerOffset = a_element.m_layerOffset;
		_decoration.color = a_element.m_color;

		_decoration.opTweenAnim = a_element.m_opTweenAnim;
		_decoration.opOscillationAnim = a_element.m_opOscillationAnim;
		_decoration.opReaction = a_element.m_opReaction;
		_decoration.preset = a_element.m_preset;

		// 種類ごとの中身
		if (a_element.GetType() == EElementType::Sprite)
		{
			const auto* _pSprite = static_cast<const UISpriteElement*>(&a_element);
			const auto& _opImage = _pSprite->GetImage();

			// 画像を付けていれば画像の UV、無ければポリゴンの UV
			QuadStyle _quad = {};
			_quad.uvOffset = _opImage ? _opImage->uvOffset : _pSprite->GetUVOffset();
			_quad.uvScale = _opImage ? _opImage->uvScale : _pSprite->GetUVScale();
			_quad.isFill = _pSprite->IsFill();
			_quad.edgeColor = _pSprite->GetEdgeColor();
			_quad.edgePixel = _pSprite->GetEdgePixel();
			_quad.edgeSide = _pSprite->GetEdgeSide();

			if (_opImage)
			{
				ImageData _image = {};
				_image.quad = _quad;
				_image.texGUID = _opImage->texGUID;
				_image.texRef = _opImage->texRef;
				_decoration.body = std::move(_image);
			}
			else
			{
				PolygonData _polygon = {};
				_polygon.quad = _quad;
				_decoration.body = std::move(_polygon);
			}
		}
		else if (a_element.GetType() == EElementType::Text)
		{
			const auto* _pText = static_cast<const UITextElement*>(&a_element);
			// フォントの参照は写さない : 戻した後に RequestResources で引き直す
			TextData _text = {};
			_text.text = _pText->GetValue();
			_text.fontGUID = _pText->GetFontGUID();
			_text.fontPixelSize = _pText->GetFontPixelSize();
			_text.lineSpacing = _pText->GetLineSpacing();
			_text.charSpacing = _pText->GetCharSpacing();
			_text.textAlign = ToDecorationTextAlign(_pText->GetTextAlign());
			_decoration.body = std::move(_text);
		}

		return _decoration;
	}

	void UIDecoration::ApplyDecoration(UIDecorationElementBase& a_element, const Decoration& a_decoration)
	{
		// 共通 : 番号が無い(0)ものは今の番号のまま
		if (a_decoration.id != 0) a_element.m_id = ElementID(a_decoration.id);
		a_element.m_name = a_decoration.name;
		a_element.m_isVisible = a_decoration.isVisible;
		a_element.m_group = a_decoration.group;

		a_element.m_offsetPos = a_decoration.offsetPos;
		a_element.m_pixelSize = a_decoration.pixelSize;
		a_element.m_rotation = a_decoration.rotation;
		a_element.m_scale = a_decoration.scale;
		a_element.m_pivotPos = a_decoration.pivot;
		a_element.m_layerOffset = a_decoration.layerOffset;
		a_element.m_color = a_decoration.color;

		a_element.m_opTweenAnim = a_decoration.opTweenAnim;
		a_element.m_opOscillationAnim = a_decoration.opOscillationAnim;
		a_element.m_opReaction = a_decoration.opReaction;
		a_element.m_preset = a_decoration.preset;

		// 種類ごとの中身
		if (a_element.GetType() == EElementType::Sprite)
		{
			auto* _pSprite = static_cast<UISpriteElement*>(&a_element);
			const QuadStyle* _pQuad = a_decoration.GetQuad();
			if (_pQuad == nullptr) return;

			_pSprite->SetFill(_pQuad->isFill);
			_pSprite->SetEdgeColor(_pQuad->edgeColor);
			_pSprite->SetEdgePixel(_pQuad->edgePixel);
			_pSprite->SetEdgeSide(_pQuad->edgeSide);

			auto& _opImage = _pSprite->RefImage();
			if (const ImageData* _pImage = a_decoration.GetImage())
			{
				if (!_opImage) _opImage.emplace();
				_opImage->texGUID = _pImage->texGUID;
				_opImage->texRef = _pImage->texRef;
				_opImage->uvOffset = _pQuad->uvOffset;
				_opImage->uvScale = _pQuad->uvScale;
			}
			else
			{
				_opImage.reset();
				_pSprite->SetUVOffset(_pQuad->uvOffset);
				_pSprite->SetUVScale(_pQuad->uvScale);
			}
		}
		else if (a_element.GetType() == EElementType::Text)
		{
			auto* _pText = static_cast<UITextElement*>(&a_element);
			const TextData* _pTextData = a_decoration.GetText();
			if (_pTextData == nullptr) return;

			_pText->SetValue(_pTextData->text);
			_pText->SetFontGUID(_pTextData->fontGUID);
			_pText->SetFontPixelSize(_pTextData->fontPixelSize);
			_pText->SetLineSpacing(_pTextData->lineSpacing);
			_pText->SetCharSpacing(_pTextData->charSpacing);
			_pText->SetTextAlign(ToElementTextAlign(_pTextData->textAlign));
		}
	}

	EElementType UIDecoration::ToElementType(const Decoration& a_decoration)
	{
		return (a_decoration.GetType() == EDecorationType::Text) ? EElementType::Text : EElementType::Sprite;
	}

	//======================================================================================
	// 同期
	//======================================================================================
	bool UIDecoration::IsPresetPending(const PresetLink& a_link, Engine::Resource::ResourceManager* a_pResourceManager)
	{
		if (!a_link.IsLinked() || a_pResourceManager == nullptr) return false;

		// 読み込みを頼むのは SyncPreset に任せる
		if (!a_link.isRequested) return true;
		if (!a_link.presetRef.IsValid() || !a_pResourceManager->IsReady(a_link.presetRef)) return false;

		const Engine::Resource::UIPreset* _pPreset = a_pResourceManager->Get(a_link.presetRef);
		if (_pPreset == nullptr) return false;

		return !a_link.isApplied || a_link.appliedRevision != _pPreset->revision;
	}

	void UIDecoration::SyncPresets(Engine::Resource::ResourceManager* a_pResourceManager)
	{
		if (a_pResourceManager == nullptr) return;

		for (auto& _upElement : m_upElements)
		{
			// 変換は重いので、写し直しを待っているものだけ通す
			if (!IsPresetPending(_upElement->m_preset, a_pResourceManager)) continue;

			Decoration _decoration = ToDecoration(*_upElement);
			const bool _isApplied = SyncPreset(_decoration, a_pResourceManager);

			// 写さなかったときも、読み込みの状態(頼んだ・写した番号)は持ち帰る
			_upElement->m_preset = _decoration.preset;
			if (!_isApplied) continue;

			// 見た目ごと種類が変わったら、同じ番号のまま作り直す
			const EElementType _type = ToElementType(_decoration);
			if (_type != _upElement->GetType())
			{
				std::unique_ptr<UIDecorationElementBase> _upNew = CreateElement(_type);
				if (!_upNew) continue;

				// 旧 Decoration に無い共通の値は引き継ぐ
				_upNew->m_id = _upElement->m_id;
				_upNew->m_isParentColor = _upElement->m_isParentColor;
				_upElement = std::move(_upNew);
			}

			ApplyDecoration(*_upElement, _decoration);
			_upElement->RequestResources(a_pResourceManager);
		}
	}

	//======================================================================================
	// エディター
	//======================================================================================
	bool UIDecoration::DrawElementPresetInspector(UIDecorationElementBase& a_element, const Engine::ECS::EngineServices& a_services)
	{
		// つながりの操作(指し替え・付いていく部分・切り離し・新しく作る)は旧の仕組みのまま。
		// 新しく作るときに入れる中身も、今のエレメントの値から作る
		Decoration _decoration = ToDecoration(a_element);
		const bool _isChanged = DrawPresetInspector(_decoration, a_services);

		// EditPreset の切り替えなど、変化として返らない操作もあるので毎回持ち帰る
		a_element.m_preset = _decoration.preset;
		return _isChanged;
	}

	void UIDecoration::PushElementToPreset(const UIDecorationElementBase& a_element, Engine::Resource::ResourceManager* a_pResourceManager)
	{
		const PresetLink& _link = a_element.m_preset;
		if (!_link.IsLinked() || !_link.isEditPreset) return;

		PushToPreset(ToDecoration(a_element), a_pResourceManager);
	}
}
