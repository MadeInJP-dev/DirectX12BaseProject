#include "UISpriteElement.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"

namespace App::Object::Decoration
{
	namespace
	{
		// 画像1枚ぶんの参照を引き直す
		void RequestImage(std::optional<UISpriteElement::Image>& a_opImage, Engine::Resource::ResourceManager& a_resourceManager)
		{
			if (!a_opImage.has_value()) return;

			UISpriteElement::Image& _image = *a_opImage;
			_image.texRef = _image.texGUID.IsValid()
				? a_resourceManager.RequestLoad<Engine::Resource::Texture>(_image.texGUID)
				: Engine::ResourceRef<Engine::Resource::Texture>();
		}

		// 画像1枚ぶんの保存 : 「持っているか」を先に書いてから中身を書く
		void ArchiveImage(Engine::Persistence::Archive& a_ar, const std::string& a_name, std::optional<UISpriteElement::Image>& a_opImage)
		{
			bool _hasImage = a_opImage.has_value();
			a_ar.Field("Has" + a_name, _hasImage);
			if (!_hasImage)
			{
				a_opImage.reset();
				return;
			}

			if (!a_opImage.has_value()) a_opImage = UISpriteElement::Image();

			UISpriteElement::Image& _image = *a_opImage;
			a_ar.GUIDField(a_name + "TexGUID", _image.texGUID);
			a_ar.Field(a_name + "UVOffset", _image.uvOffset);
			a_ar.Field(a_name + "UVScale", _image.uvScale);
		}
	}

	//======================================================================================
	// 描画
	//======================================================================================
	void UISpriteElement::Draw(const DrawContext& a_context)
	{
		if (!IsDrawn(a_context)) return;

		auto* _pGraphicsEngine = a_context.pGraphicsEngine;
		auto& _resourceManager = *a_context.pResourceManager;

		// 画像を付けていれば、UV は画像のものを使う
		const Math::Vector2& _uvOffset = m_image ? m_image->uvOffset : m_uvOffset;
		const Math::Vector2& _uvScale = m_image ? m_image->uvScale : m_uvScale;

		Internal::Resolved _resolved = Resolve(a_context, _uvOffset, _uvScale);

		// ノイズの差し替え : 効果(UIマテリアル)が掛かっているときだけ意味がある。
		// 届いていなければマテリアルのノイズのまま
		if (m_noiseImage && _resourceManager.IsReady(m_noiseImage->texRef))
		{
			_resolved.effect.noiseTexHandle = m_noiseImage->texRef.GetRaw();
		}

		// 矩形の左上(アンカーからのずれ)
		const Math::Vector2 _localTopLeft = CalcLocalTopLeft(_resolved.size);

		// ---- 塗り ----
		if (m_isFill)
		{
			// 画像が無ければ組み込みの白テクスチャを色で染める。
			// 画像は届くまで描かない(空ハンドルで積むとテクスチャ取得で落ちる)
			Engine::Handle<Engine::Resource::Texture> _texHandle = {};

			if (m_image)
			{
				if (_resourceManager.IsReady(m_image->texRef))
				{
					_texHandle = m_image->texRef.GetRaw();
				}
			}
			else
			{
				_texHandle = Internal::GetWhiteTexture(_resourceManager);
			}

			if (_texHandle.IsValid())
			{
				Internal::SubmitLocalRect(
					_pGraphicsEngine, _texHandle, _resolved,
					_localTopLeft, _resolved.size,
					_resolved.color, _resolved.uvOffset, _resolved.uvScale);
			}
		}

		// ---- 枠 ----
		// 塗りと同じ掛かり具合(親の色・その場の色・アニメ・反応)を通す。
		// ここを飛ばすと、反応で消したはずの枠だけ残ってしまう
		const Math::Color _edgeColor = _resolved.modulate * m_edgeColor;
		Internal::DrawEdge(_pGraphicsEngine, m_edgePixel, m_edgeSide, _resolved, _localTopLeft, _edgeColor);
	}

	//======================================================================================
	// GUIDから参照を引き直す
	//======================================================================================
	void UISpriteElement::RequestResources(Engine::Resource::ResourceManager* a_pResourceManager)
	{
		if (a_pResourceManager == nullptr) return;

		// 実体の到着は待たない。描画側が IsReady を見てスキップする
		RequestImage(m_image, *a_pResourceManager);
		RequestImage(m_noiseImage, *a_pResourceManager);
		RequestImage(m_maskImage, *a_pResourceManager);
	}

	//======================================================================================
	// 保存
	//======================================================================================
	void UISpriteElement::ArchiveBody(Engine::Persistence::Archive& a_ar)
	{
		a_ar.Field("UVOffset", m_uvOffset);
		a_ar.Field("UVScale", m_uvScale);

		a_ar.Field("IsFill", m_isFill);
		a_ar.Field("EdgeColor", m_edgeColor);
		a_ar.Field("EdgePixel", m_edgePixel);
		a_ar.Field("EdgeSide", m_edgeSide);

		ArchiveImage(a_ar, "Image", m_image);
		ArchiveImage(a_ar, "NoiseImage", m_noiseImage);
		ArchiveImage(a_ar, "MaskImage", m_maskImage);
	}
}
