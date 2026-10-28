#include "UIDecorationElementBase.h"

#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"

namespace App::Object::Decoration
{
	//======================================================================================
	// 更新
	//======================================================================================
	void UIDecorationElementBase::Update(const UpdateContext& a_context)
	{
		Internal::AdvanceAnimation(
			m_opTweenAnim,
			m_opOscillationAnim,
			m_opReaction,
			a_context.parentState,
			a_context.dt);
	}

	//======================================================================================
	// 保存
	//======================================================================================
	void UIDecorationElementBase::Archive(Engine::Persistence::Archive& a_ar)
	{
		a_ar.Field("ID", m_id.value);
		a_ar.StringField("Name", m_name);
		a_ar.Field("IsVisible", m_isVisible);
		a_ar.Field("Group", m_group);

		a_ar.Field("OffsetPos", m_offsetPos);
		a_ar.Field("PixelSize", m_pixelSize);
		a_ar.Field("Rotation", m_rotation);
		a_ar.Field("Scale", m_scale);
		a_ar.Field("Pivot", m_pivotPos);
		a_ar.Field("LayerOffset", m_layerOffset);
		a_ar.Field("Color", m_color);
		a_ar.Field("IsParentColor", m_isParentColor);

		Internal::ArchiveAnimations(a_ar, m_opTweenAnim, m_opReaction, m_opOscillationAnim);

		// プリセット(指していなければ空の GUID)
		a_ar.GUIDField("PresetGUID", m_preset.presetGUID);
		a_ar.Field("PresetFollow", m_preset.follow);
		if (a_ar.IsLoading())
		{
			m_preset.isApplied = false;
			m_preset.isRequested = false;
		}

		// 種類ごとの中身は区切りの中へ : 中身へフィールドを足しても後ろの読み出しがずれない
		{
			Engine::Persistence::ArchiveSection _bodySection(a_ar, "Body");
			ArchiveBody(a_ar);
		}
	}

	//======================================================================================
	// 描画の下ごしらえ
	//======================================================================================
	bool UIDecorationElementBase::IsDrawn(const DrawContext& a_context) const
	{
		if (!m_isVisible) return false;
		if (a_context.pGraphicsEngine == nullptr || a_context.pResourceManager == nullptr) return false;

		// 群で絞られているなら、対象外は描かない
		const DrawOverride& _override = a_context.drawOverride;
		if (_override.isUseGroup && m_group != _override.group) return false;

		return true;
	}

	Internal::Resolved UIDecorationElementBase::Resolve(
		const DrawContext& a_context,
		const Math::Vector2& a_uvOffset,
		const Math::Vector2& a_uvScale) const
	{
		Internal::ResolveSource _source = {};
		_source.offsetPos = m_offsetPos;
		_source.pixelSize = m_pixelSize;
		_source.rotation = m_rotation;
		_source.scale = m_scale;
		_source.layerOffset = m_layerOffset;
		_source.color = m_color;
		_source.isParentColor = m_isParentColor;
		_source.uvOffset = a_uvOffset;
		_source.uvScale = a_uvScale;

		return Internal::Resolve(
			_source,
			Internal::MakeAnimResult(m_opTweenAnim, m_opOscillationAnim, m_opReaction),
			a_context.parentTr,
			a_context.parentOp,
			a_context.drawOverride);
	}

	Math::Vector2 UIDecorationElementBase::CalcLocalTopLeft(const Math::Vector2& a_size) const
	{
		return { -m_pivotPos.x * a_size.x, -m_pivotPos.y * a_size.y };
	}
}
