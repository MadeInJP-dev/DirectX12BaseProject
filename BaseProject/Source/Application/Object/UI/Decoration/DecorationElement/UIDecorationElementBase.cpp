#include "UIDecorationElementBase.h"

#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"

#include "Engine/EditorField/EditorField.h"

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
	// インスペクター
	//======================================================================================
	bool UIDecorationElementBase::DrawInspector(const Engine::ECS::EngineServices& a_services)
	{
		bool _isChanged = false;
		const PresetLink& _link = m_preset;

		//----------------------------------------------------------------------------------
		// 共通
		//----------------------------------------------------------------------------------
		if (Engine::EditorField::Field("Visible", m_isVisible)) _isChanged = true;
		if (Engine::EditorField::Field("Name", m_name)) _isChanged = true;
		Engine::EditorField::Value("Type", "%s", GetTypeName());
		Engine::EditorField::Value("ID", "%u", m_id.value);
		Engine::EditorField::Tooltip("他からエレメントを指すときの番号(名前を変えても変わらない)");

		int _group = static_cast<int>(m_group);
		if (Engine::EditorField::Field("Group", _group, 1, 0, 15))
		{
			m_group = static_cast<uint32_t>(std::max(_group, 0));
			_isChanged = true;
		}
		Engine::EditorField::Tooltip("HUDがエレメントを出し分けるための札(DrawOverride::isUseGroup で絞り込む)");

		Engine::EditorField::Header("Transform (親からの相対)");

		if (Engine::EditorField::Field("OffsetPos", m_offsetPos, 1.0f)) _isChanged = true;
		Engine::EditorField::Tooltip("親のピボット位置からのずれ(px)。シーンビューの小さいハンドルでも動かせる");

		{
			Engine::EditorField::DisabledScope _disabled(_link.IsLocked(EPresetPart::SIZE));

			// 文字の大きさは文字側が決めるので、矩形の大きさは出さない
			if (IsUsePixelSize())
			{
				if (Engine::EditorField::Field("PixelSize", m_pixelSize, 1.0f, 0.0f, 8192.0f)) _isChanged = true;
			}
		}

		if (Engine::EditorField::Field("Rotation", m_rotation, 0.1f, -360.0f, 360.0f)) _isChanged = true;
		{
			Engine::EditorField::DisabledScope _disabled(_link.IsLocked(EPresetPart::SIZE));
			if (Engine::EditorField::Field("Scale", m_scale, 0.01f, 0.0f, 64.0f)) _isChanged = true;
			if (Engine::EditorField::Field("Pivot (0-1)", m_pivotPos, 0.01f, 0.0f, 1.0f)) _isChanged = true;
		}
		if (Engine::EditorField::Field("LayerOffset", m_layerOffset, 0.1f)) _isChanged = true;
		Engine::EditorField::Tooltip("親のレイヤーへ足す。大きいほど手前");
		{
			Engine::EditorField::DisabledScope _disabled(_link.IsLocked(EPresetPart::LOOK));
			if (Engine::EditorField::Field("Color", m_color)) _isChanged = true;
		}
		if (Engine::EditorField::Field("ParentColor", m_isParentColor)) _isChanged = true;
		Engine::EditorField::Tooltip("切ると親の色を受けない(演出の濃さとアニメは受ける)");

		//----------------------------------------------------------------------------------
		// 種類ごと
		//----------------------------------------------------------------------------------
		if (DrawBodyInspector(a_services)) _isChanged = true;

		//----------------------------------------------------------------------------------
		// カーソルへの反応・アニメーション
		//----------------------------------------------------------------------------------
		{
			Engine::EditorField::DisabledScope _disabled(_link.IsLocked(EPresetPart::REACTION));
			if (Internal::DrawReactionInspector(m_opReaction)) _isChanged = true;
		}
		{
			Engine::EditorField::DisabledScope _disabled(_link.IsLocked(EPresetPart::ANIMATION));
			if (Internal::DrawAnimationInspector(m_opTweenAnim, m_opOscillationAnim)) _isChanged = true;
		}

		return _isChanged;
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
