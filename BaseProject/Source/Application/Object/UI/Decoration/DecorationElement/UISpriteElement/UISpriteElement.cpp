#include "UISpriteElement.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Engine/ECS/System/SystemContext.h"	// EngineServices
#include "Engine/EditorField/EditorField.h"

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

		// 画像1枚ぶんのインスペクター : 付けるかどうか・テクスチャ・UV
		//
		// a_isLockContent / a_isLockLook はプリセットで触れなくする部分
		// (テクスチャは中身、付ける・外す と UV は見た目)
		bool DrawImageUI(
			const char* a_label,
			std::optional<UISpriteElement::Image>& a_opImage,
			const Engine::ECS::EngineServices& a_services,
			bool a_isLockContent,
			bool a_isLockLook,
			bool a_isShowUV)
		{
			bool _isChanged = false;
			Engine::EditorField::IDScope _id(a_label);

			{
				Engine::EditorField::DisabledScope _disabled(a_isLockLook);
				bool _hasImage = a_opImage.has_value();
				if (Engine::EditorField::Field(a_label, _hasImage))
				{
					if (_hasImage) a_opImage.emplace();
					else           a_opImage.reset();
					_isChanged = true;
				}
			}
			if (!a_opImage.has_value()) return _isChanged;

			UISpriteElement::Image& _image = *a_opImage;
			Engine::EditorField::IndentScope _indent;

			{
				Engine::EditorField::DisabledScope _disabled(a_isLockContent);
				if (Engine::EditorField::AssetField(a_services, "Texture", "Texture", _image.texGUID))
				{
					if (a_services.pResourceManager)
					{
						_image.texRef = _image.texGUID.IsValid()
							? a_services.pResourceManager->RequestLoad<Engine::Resource::Texture>(_image.texGUID)
							: Engine::ResourceRef<Engine::Resource::Texture>();
					}
					_isChanged = true;
				}
			}
			if (_image.texRef.IsValid()) Engine::EditorField::Image(a_services, _image.texRef.GetRaw(), 128, 128);

			if (a_isShowUV)
			{
				Engine::EditorField::DisabledScope _disabled(a_isLockLook);
				if (Engine::EditorField::Field("UVOffset", _image.uvOffset, 0.01f)) _isChanged = true;
				if (Engine::EditorField::Field("UVScale", _image.uvScale, 0.01f)) _isChanged = true;
				Engine::EditorField::Tooltip("1枚に並べた絵から1コマ切り出すときの倍率 (uv * UVScale + UVOffset)");
			}

			return _isChanged;
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

	//======================================================================================
	// インスペクター
	//======================================================================================
	bool UISpriteElement::DrawBodyInspector(const Engine::ECS::EngineServices& a_services)
	{
		bool _isChanged = false;
		const bool _isLockLook = m_preset.IsLocked(EPresetPart::LOOK);
		const bool _isLockContent = m_preset.IsLocked(EPresetPart::CONTENT);

		Engine::EditorField::Header("Sprite");

		// ---- 画像 ----
		if (DrawImageUI("Image", m_image, a_services, _isLockContent, _isLockLook, true)) _isChanged = true;
		Engine::EditorField::Tooltip("切ると組み込みの白テクスチャを Color で染めて出す");

		if (!m_image)
		{
			Engine::EditorField::DisabledScope _disabled(_isLockLook);
			if (Engine::EditorField::Field("UVOffset", m_uvOffset, 0.01f)) _isChanged = true;
			if (Engine::EditorField::Field("UVScale", m_uvScale, 0.01f)) _isChanged = true;
		}

		// ---- 枠 ----
		{
			Engine::EditorField::DisabledScope _disabled(_isLockLook);
			Engine::EditorField::Header("Edge");

			if (Engine::EditorField::Field("Fill", m_isFill)) _isChanged = true;
			Engine::EditorField::Tooltip("切ると枠だけになる");

			if (Engine::EditorField::Field("EdgePixel", m_edgePixel, 0.5f, 0.0f, 256.0f)) _isChanged = true;
			if (m_edgePixel > 0.0f)
			{
				if (Engine::EditorField::Field("EdgeColor", m_edgeColor)) _isChanged = true;
				if (Engine::EditorField::FlagsField("EdgeSide", m_edgeSide)) _isChanged = true;
			}
		}

		// ---- 効果に使う画像 ----
		Engine::EditorField::Header("Effect Image");

		if (DrawImageUI("NoiseImage", m_noiseImage, a_services, _isLockLook, _isLockLook, false)) _isChanged = true;
		Engine::EditorField::Tooltip("UIマテリアルの効果(ディゾルブなど)が引くノイズを差し替える");

		if (DrawImageUI("MaskImage", m_maskImage, a_services, _isLockLook, _isLockLook, true)) _isChanged = true;
		Engine::EditorField::Tooltip("描画側に受け口がまだ無いので、今は保存されるだけ");

		return _isChanged;
	}
}
