#include "UIDecorationElementBase.h"

#include "Application/Object/UI/Decoration/Internal/DecorationInternal.h"

#include "Engine/EditorField/EditorField.h"

namespace App::Object::Decoration
{
	namespace
	{
		// 楕円の輪郭を何点で近似するか(エディターの表示用)
		constexpr int ELLIPSE_OUTLINE_POINT = 32;

		//----------------------------------------------------------------------------------
		// ボディ1つぶんの形 : 画面のピクセル座標
		//----------------------------------------------------------------------------------
		struct HitShape
		{
			Math::Vector2 anchorPos = {};	// ピボットが乗る位置
			Math::Vector2 size = {};		// 大きさ
			float rotation = 0.0f;			// 回転(度)
		};

		// 余白込みの矩形の範囲(アンカーからのずれ、回転前)
		void CalcLocalBounds(
			const HitShape& a_shape,
			const Math::Vector2& a_pivot,
			const Math::Vector2& a_padding,
			Math::Vector2& a_outMin,
			Math::Vector2& a_outMax)
		{
			a_outMin = { -a_pivot.x * a_shape.size.x - a_padding.x, -a_pivot.y * a_shape.size.y - a_padding.y };
			a_outMax = { (1.0f - a_pivot.x) * a_shape.size.x + a_padding.x, (1.0f - a_pivot.y) * a_shape.size.y + a_padding.y };
		}

		bool IsInsideShape(
			const Math::Vector2& a_pos,
			const HitShape& a_shape,
			const Math::Vector2& a_pivot,
			const UIHitBody& a_body)
		{
			Math::Vector2 _min = {};
			Math::Vector2 _max = {};
			CalcLocalBounds(a_shape, a_pivot, a_body.padding, _min, _max);
			if (_max.x <= _min.x || _max.y <= _min.y) return false;

			// 形の回転を戻して、回っていない矩形として調べる
			const Math::Vector2 _local = Internal::RotateDeg(a_pos - a_shape.anchorPos, -a_shape.rotation);

			switch (a_body.shape)
			{
			case EHitShape::Ellipse:
			{
				const Math::Vector2 _center = (_min + _max) * 0.5f;
				const Math::Vector2 _radius = (_max - _min) * 0.5f;
				const float _dx = (_local.x - _center.x) / _radius.x;
				const float _dy = (_local.y - _center.y) / _radius.y;
				return _dx * _dx + _dy * _dy <= 1.0f;
			}

			case EHitShape::Rect:
			default:
				return _local.x >= _min.x && _local.x <= _max.x && _local.y >= _min.y && _local.y <= _max.y;
			}
		}

		// 親の矩形 : 親のピボットの位置に、親の大きさ・回転のまま置く(倍率は掛けない。アンカーの判定と同じ)
		HitShape MakeParentShape(const ParentTransform& a_parentTr, const ParentOption& a_parentOp)
		{
			HitShape _shape = {};
			_shape.anchorPos = a_parentTr.pixelPos;
			_shape.size = a_parentOp.parentSize;
			_shape.rotation = a_parentTr.rotation;
			return _shape;
		}

		// 素の形 : 描くときと同じく、ずれは倍率を掛けてから親の回転で回す
		HitShape MakeBaseShape(
			const ParentTransform& a_parentTr,
			float a_scale,
			const Math::Vector2& a_offsetPos,
			const Math::Vector2& a_pixelSize,
			float a_rotation)
		{
			HitShape _shape = {};
			_shape.anchorPos = a_parentTr.pixelPos + Internal::RotateDeg(a_offsetPos * a_scale, a_parentTr.rotation);
			_shape.size = a_pixelSize * a_scale;
			_shape.rotation = a_parentTr.rotation + a_rotation;
			return _shape;
		}
	}

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

		// 当たり判定のボディ : 「持っているか」を先に書いてから中身を書く
		bool _hasHitBody = m_opHitBody.has_value();
		a_ar.Field("HasHitBody", _hasHitBody);
		if (_hasHitBody)
		{
			if (!m_opHitBody) m_opHitBody.emplace();
			a_ar.Field("HitShape", m_opHitBody->shape);
			a_ar.Field("HitPadding", m_opHitBody->padding);
			a_ar.Field("HitFollowAnim", m_opHitBody->isFollowAnim);
			a_ar.Field("HitUseParentRect", m_opHitBody->isUseParentRect);
		}
		else
		{
			m_opHitBody.reset();
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

			// 文字の大きさは文字側が決めるので、矩形の大きさは出さない。
			// ただしボディを持つときは、ボディの大きさとして要る
			if (IsUsePixelSize() || m_opHitBody)
			{
				if (Engine::EditorField::Field("PixelSize", m_pixelSize, 1.0f, 0.0f, 8192.0f)) _isChanged = true;
				if (!IsUsePixelSize()) Engine::EditorField::Tooltip("見た目には使わず、当たり判定のボディの大きさだけに使う");
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
		// 当たり判定
		//----------------------------------------------------------------------------------
		Engine::EditorField::Header("Hit Body");

		bool _hasHitBody = m_opHitBody.has_value();
		if (Engine::EditorField::Field("HitBody", _hasHitBody))
		{
			if (_hasHitBody) m_opHitBody.emplace();
			else             m_opHitBody.reset();
			_isChanged = true;
		}
		Engine::EditorField::Tooltip("持たせると、持ち主の UI(HitTarget を立てたもの)がカーソルの重なりを問い合わせる");

		if (m_opHitBody)
		{
			UIHitBody& _body = *m_opHitBody;

			Engine::EditorField::IndentScope _indent;
			Engine::EditorField::IDScope _id("HitBody");

			if (Engine::EditorField::Field("Shape", _body.shape)) _isChanged = true;
			if (Engine::EditorField::Field("Padding", _body.padding, 0.5f)) _isChanged = true;
			Engine::EditorField::Tooltip("余白(px)。負で狭くなる");
			if (Engine::EditorField::Field("UseParentRect", _body.isUseParentRect)) _isChanged = true;
			Engine::EditorField::Tooltip("このエレメントではなく、UI(アンカー)の矩形に当てる。並べる側が UI の大きさを動かすときも追従する");

			if (!_body.isUseParentRect)
			{
				if (Engine::EditorField::Field("FollowAnim", _body.isFollowAnim)) _isChanged = true;
				Engine::EditorField::Tooltip("アニメーション・反応で大きくなった形にも当てる(素の形より痩せない)");
			}
		}

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
	// 当たり判定
	//======================================================================================
	bool UIDecorationElementBase::IsHit(const Math::Vector2& a_pos, const ParentTransform& a_parentTr, const ParentOption& a_parentOp) const
	{
		if (!m_opHitBody || !m_isVisible) return false;
		const UIHitBody& _body = *m_opHitBody;

		// 親の矩形に当てるものは、エレメントの置き場所もアニメーションも見ない
		if (_body.isUseParentRect)
		{
			return IsInsideShape(a_pos, MakeParentShape(a_parentTr, a_parentOp), a_parentOp.parentPivot, _body);
		}

		const float _scale = a_parentTr.scale * m_scale;

		// ---- 素の形 ----
		const HitShape _shape = MakeBaseShape(a_parentTr, _scale, m_offsetPos, m_pixelSize, m_rotation);
		if (IsInsideShape(a_pos, _shape, m_pivotPos, _body)) return true;

		if (!_body.isFollowAnim) return false;

		// ---- 今の形(アニメーション・反応を掛けたもの) : 描画と同じ合成結果を使う ----
		const Internal::AnimResult _anim = Internal::MakeAnimResult(m_opTweenAnim, m_opOscillationAnim, m_opReaction);

		HitShape _animShape = {};
		_animShape.anchorPos = a_parentTr.pixelPos + Internal::RotateDeg((m_offsetPos + _anim.positionAdd) * _scale, a_parentTr.rotation);
		_animShape.size = _shape.size * _anim.scaleMul;
		_animShape.rotation = _shape.rotation + _anim.rotationAdd;
		return IsInsideShape(a_pos, _animShape, m_pivotPos, _body);
	}

	bool UIDecorationElementBase::CalcHitOutline(const ParentTransform& a_parentTr, const ParentOption& a_parentOp, std::vector<Math::Vector2>& a_outPointVec) const
	{
		a_outPointVec.clear();
		if (!m_opHitBody) return false;
		const UIHitBody& _body = *m_opHitBody;

		const HitShape _shape = _body.isUseParentRect
			? MakeParentShape(a_parentTr, a_parentOp)
			: MakeBaseShape(a_parentTr, a_parentTr.scale * m_scale, m_offsetPos, m_pixelSize, m_rotation);
		const Math::Vector2& _pivot = _body.isUseParentRect ? a_parentOp.parentPivot : m_pivotPos;

		Math::Vector2 _min = {};
		Math::Vector2 _max = {};
		CalcLocalBounds(_shape, _pivot, _body.padding, _min, _max);

		// アンカーからのずれを回して画面へ置く
		const auto _push = [&](const Math::Vector2& a_local)
			{
				a_outPointVec.push_back(_shape.anchorPos + Internal::RotateDeg(a_local, _shape.rotation));
			};

		if (_body.shape == EHitShape::Ellipse)
		{
			constexpr float TWO_PI = 6.283185307f;
			const Math::Vector2 _center = (_min + _max) * 0.5f;
			const Math::Vector2 _radius = (_max - _min) * 0.5f;
			for (int _i = 0; _i < ELLIPSE_OUTLINE_POINT; ++_i)
			{
				const float _rad = TWO_PI * static_cast<float>(_i) / static_cast<float>(ELLIPSE_OUTLINE_POINT);
				_push({ _center.x + _radius.x * std::cos(_rad), _center.y + _radius.y * std::sin(_rad) });
			}
		}
		else
		{
			_push({ _min.x, _min.y });
			_push({ _max.x, _min.y });
			_push({ _max.x, _max.y });
			_push({ _min.x, _max.y });
		}
		return true;
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
