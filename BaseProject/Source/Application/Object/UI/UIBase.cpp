#include "UIBase.h"

#include "UIPanel/UIPanel.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"	// 親(パネル)を GUID で引く

namespace App::Object
{
	UIBase::UIBase() : UIBase(true)
	{
	}

	UIBase::UIBase(bool a_isInteractive)
	{
		if (a_isInteractive) m_opInteraction.emplace();
	}

	void UIBase::Release(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_opInteraction) m_opInteraction->ReleaseSounds(a_context);
	}

	//======================================================================================
	// 更新前 : カーソルの上に居ると名乗る
	//--------------------------------------------------------------------------------------
	// 判定の矩形はここで組み立てる(飾りの範囲を使うことがあるため)。
	// 名乗りそのものと、受け取り手の決め方は UIInteraction / UICursorResource
	//======================================================================================
	void UIBase::PreUpdate(Engine::GameObject::ObjectContext& a_context)
	{
		if (!m_opInteraction) return;

		bool _isReceivable = m_opInteraction->IsReceivable(a_context, IsVisibleInHierarchy(a_context));

		Math::Vector2 _cursorPos = {};
		bool _isInside = false;
		if (_isReceivable)
		{
			// カーソルの位置が取れない(最小化中など)フレームは名乗らない
			_isReceivable = UIInteraction::CalcCursorUIPos(a_context, _cursorPos);
			if (_isReceivable) _isInside = IsPointInsideSelf(_cursorPos);
		}

		m_opInteraction->ClaimCursor(a_context, this, m_anchor.layer, _isReceivable, _isInside);
	}

	//======================================================================================
	// 更新 : 押下の進行と、飾りのアニメーション
	//--------------------------------------------------------------------------------------
	// 飾りは出していないフレームも進める。止めてしまうと、
	// 出した瞬間に前回止まったところから続いてしまう。
	//
	// 継承先で Update を持つ場合は、先頭で UIBase::Update を呼ぶこと
	//======================================================================================
	void UIBase::Update(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_opInteraction)
		{
			const bool _isReceivable = m_opInteraction->IsReceivable(a_context, IsVisibleInHierarchy(a_context));
			m_opInteraction->Advance(a_context, this, _isReceivable);
		}

		// 飾りは今の状態を受け取って、そこへ寄っていく
		const Decoration::EUIState _state = GetUIState();

		for (Decoration::Decoration& _decoration : m_decorationVec)
		{
			Decoration::AdvanceAnimation(_decoration, _state, a_context.dt);
		}
	}

	Decoration::EUIState UIBase::GetUIState() const
	{
		// 押せない UI は何もされることがない
		if (!m_opInteraction) return Decoration::EUIState::Normal;

		return m_opInteraction->GetState();
	}

	void UIBase::SetInteractable(bool a_isInteractable)
	{
		if (m_opInteraction) m_opInteraction->isInteractable = a_isInteractable;
	}

	//======================================================================================
	// 実際に出ているか
	//--------------------------------------------------------------------------------------
	// 上へ辿って、途中にある UIPanel が1つでも隠れていれば出ていない。
	// 親がパネルでないところ(ボタンの下にまとめただけの画像など)は何も伝えないので、
	// エディターの並びのために付けた既存の親子で表示が変わることはない。
	//
	// 親子が輪になっていても止まるよう、辿る深さに上限を設けてある
	//======================================================================================
	bool UIBase::IsVisibleInHierarchy(const Engine::GameObject::ObjectContext& a_context) const
	{
		if (!m_isVisible) return false;
		if (!a_context.pObjectManager) return true;

		constexpr int DEPTH_MAX = 32;

		Core::GUID _parentGUID = GetParentGUID();
		for (int _depth = 0; _depth < DEPTH_MAX && _parentGUID.IsValid(); ++_depth)
		{
			const Engine::GameObject::BaseObject* _pParent = a_context.pObjectManager->FindByGUID(_parentGUID);
			if (!_pParent) break;

			if (const UIPanel* _pPanel = Core::TypeInfo::Cast<const UIPanel>(_pParent))
			{
				if (!_pPanel->IsVisible()) return false;
			}

			_parentGUID = _pParent->GetParentGUID();
		}
		return true;
	}

	//======================================================================================
	// 自分の判定矩形の内側か
	//--------------------------------------------------------------------------------------
	// 判定そのものは UIAnchor::IsPointInside。ここは矩形を組み立てて渡すだけ。
	//
	// 使う矩形は3通り :
	//   HitFollowAnim が立っている … 飾りの今の範囲(アニメーション・反応込み)から作る
	//   PixelSize が入っている     … アンカーの矩形をそのまま使う(判定を絵とずらしたいとき用)
	//   PixelSize が 0            … 飾りが占めている範囲(素の大きさ)から作る
	//
	// 飾りから作る道を用意してあるのは、見た目を飾り側へ移したことで
	// アンカーの大きさを入れ忘れやすくなったため。
	// 幅0の矩形はどこにも当たらないので、そのままだと
	// 「置いて飾りを付けたのに、乗っても何も起きない」になる。
	//
	// HitFollowAnim は絵の大きさが動くもの用。アンカーの矩形は動かないので、
	// 切ったままだと大きくなった絵のふちがどこにも当たらない
	//======================================================================================
	bool UIBase::CalcHitRect(HitRect& a_outRect) const
	{
		if (!m_opInteraction) return false;

		const bool _isHitFollowAnim = m_opInteraction->isHitFollowAnim;

		a_outRect.rotation = m_anchor.rotation;
		a_outRect.padding = m_opInteraction->hitPadding;

		// アンカーに大きさが入っていても、今の絵へ追従させる指示があれば飾りを優先する
		const bool _isUseDecorationBounds = _isHitFollowAnim || !m_anchor.HasSize();

		if (_isUseDecorationBounds)
		{
			Math::Vector2 _center = {};
			Math::Vector2 _size = {};
			if (CalcDecorationBounds(_center, _size, _isHitFollowAnim))
			{
				a_outRect.pixelPos = m_anchor.ToScreenPos(_center);	// 範囲の中心を指す点。回転と倍率はアンカーのものが乗る
				a_outRect.pixelSize = _size * m_anchor.scale;
				a_outRect.pivot = { 0.5f, 0.5f };						// 中心を出しているのでピボットは中央
				return true;
			}

			// 測れる飾りが1つも無ければアンカーの矩形へ戻る(文字だけのUIなど)
		}

		if (!m_anchor.HasSize()) return false;

		a_outRect.pixelPos = m_anchor.pixelPos;
		a_outRect.pixelSize = m_anchor.pixelSize;
		a_outRect.pivot = m_anchor.pivot;
		return true;
	}

	bool UIBase::IsPointInsideSelf(const Math::Vector2& a_uiPos) const
	{
		// 切り抜かれて見えていないところは押せない。
		// スクロールで範囲の外へ出た項目が、範囲の外(見出しや別のボタンの上)で押されないようにする
		if (m_opClipRect)
		{
			const Engine::Graphics::UIClipRect& _clip = m_opClipRect.value();
			if (a_uiPos.x < _clip.min.x || a_uiPos.y < _clip.min.y) return false;
			if (a_uiPos.x > _clip.max.x || a_uiPos.y > _clip.max.y) return false;
		}

		HitRect _rect = {};
		if (!CalcHitRect(_rect)) return false;

		return UIAnchor::IsPointInside(
			a_uiPos,
			_rect.pixelPos,
			_rect.pixelSize,
			_rect.pivot,
			_rect.rotation,
			_rect.padding);
	}

	//======================================================================================
	// 飾りが占めている範囲
	//--------------------------------------------------------------------------------------
	// a_isIncludeAnim を立てると、素の矩形に加えて
	// 「アニメーション・反応を掛けた今の矩形」も範囲へ入れる(2つの合併)。
	//
	// 今の矩形だけに差し替えないのは、乗ると縮む反応を付けたときに
	// 「乗る→縮んで外れる→戻って乗る」が毎フレーム入れ替わってちらつくため。
	// 合併にしておけば判定が素の大きさより痩せないので、広がる側だけが効く
	//======================================================================================
	bool UIBase::CalcDecorationBounds(
		Math::Vector2& a_outCenter,
		Math::Vector2& a_outSize,
		bool a_isIncludeAnim) const
	{
		bool _hasAny = false;
		float _minX = 0.0f, _minY = 0.0f, _maxX = 0.0f, _maxY = 0.0f;

		// 矩形1つぶんを範囲へ足す : 回転を掛けた4隅を取り、それを囲む矩形にする
		const auto _addRect = [&](
			const Math::Vector2& a_offset,
			const Math::Vector2& a_size,
			const Math::Vector2& a_pivot,
			float a_rotation)
		{
			if (a_size.x <= 0.0f || a_size.y <= 0.0f) return;

			const Math::Vector2 _topLeft = {
				-a_pivot.x * a_size.x,
				-a_pivot.y * a_size.y
			};
			const Math::Vector2 _cornerArray[4] = {
				_topLeft,
				{ _topLeft.x + a_size.x, _topLeft.y },
				{ _topLeft.x,            _topLeft.y + a_size.y },
				{ _topLeft.x + a_size.x, _topLeft.y + a_size.y },
			};

			for (const Math::Vector2& _corner : _cornerArray)
			{
				const Math::Vector2 _point = a_offset + UIAnchor::RotateDeg(_corner, a_rotation);

				if (!_hasAny)
				{
					_minX = _maxX = _point.x;
					_minY = _maxY = _point.y;
					_hasAny = true;
					continue;
				}

				_minX = std::min(_minX, _point.x);
				_minY = std::min(_minY, _point.y);
				_maxX = std::max(_maxX, _point.x);
				_maxY = std::max(_maxY, _point.y);
			}
		};

		for (const Decoration::Decoration& _decoration : m_decorationVec)
		{
			if (!_decoration.isVisible) continue;

			// 文字は大きさをフォントから組み立てるので、ここでは測れない。
			// 文字だけのUIに判定を持たせたいときは PixelSize を入れること
			if (_decoration.GetType() == Decoration::EDecorationType::Text) continue;

			const Math::Vector2 _size = _decoration.pixelSize * _decoration.scale;
			if (_size.x <= 0.0f || _size.y <= 0.0f) continue;

			// 素の矩形
			_addRect(_decoration.offsetPos, _size, _decoration.pivot, _decoration.rotation);

			if (!a_isIncludeAnim) continue;

			//----------------------------------------------------------------------
			// 今の矩形 : 描画と同じ合成結果を使う
			//
			// 反応で消している飾り(visibleRate が 0 に寄っているもの)も
			// 大きさとしては数える。透明な枠のぶんまで判定が伸びるのが困るなら、
			// その飾りの isVisible を切ること
			//----------------------------------------------------------------------
			const Decoration::DecorationTransform _now =
				Decoration::CalcCurrentTransform(_decoration);

			_addRect(
				_decoration.offsetPos + _now.offsetAdd,
				_size * _now.scaleMul,
				_decoration.pivot,
				_decoration.rotation + _now.rotationAdd);
		}

		if (!_hasAny) return false;

		a_outCenter = { (_minX + _maxX) * 0.5f, (_minY + _maxY) * 0.5f };
		a_outSize   = { _maxX - _minX, _maxY - _minY };

		return true;
	}

	void UIBase::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		// 出していないもの(パネルごと隠れているものも)は DrawDecorations が弾く
		DrawDecorations(a_context);
	}

	//======================================================================================
	// 飾りの操作
	//======================================================================================
	Decoration::Decoration& UIBase::AddDecoration(Decoration::EDecorationType a_type)
	{
		// 番号は配列が伸びる前に決める(伸びた後だと、作りかけの 0 番を数えてしまう)
		const uint32_t _id = MakeNewDecorationId();

		Decoration::Decoration& _decoration = m_decorationVec.emplace_back();
		_decoration.id = _id;
		_decoration.SetType(a_type);

		// 名前が全部同じだと一覧で見分けられないので、種類と番号を入れておく
		const char* _typeName = "Decoration";
		switch (a_type)
		{
		case Decoration::EDecorationType::Image:   _typeName = "Image";   break;
		case Decoration::EDecorationType::Text:    _typeName = "Text";    break;
		case Decoration::EDecorationType::Polygon:
		default:                                   _typeName = "Polygon"; break;
		}
		_decoration.name = std::string(_typeName) + std::to_string(m_decorationVec.size());

		return _decoration;
	}

	Decoration::Decoration* UIBase::FindDecoration(const std::string& a_name)
	{
		for (Decoration::Decoration& _decoration : m_decorationVec)
		{
			if (_decoration.name == a_name) return &_decoration;
		}
		return nullptr;
	}

	Decoration::Decoration* UIBase::FindDecorationById(uint32_t a_id)
	{
		const int _index = FindDecorationIndexById(a_id);
		return (_index >= 0) ? &m_decorationVec[_index] : nullptr;
	}

	const Decoration::Decoration* UIBase::FindDecorationById(uint32_t a_id) const
	{
		const int _index = FindDecorationIndexById(a_id);
		return (_index >= 0) ? &m_decorationVec[_index] : nullptr;
	}

	int UIBase::FindDecorationIndexById(uint32_t a_id) const
	{
		if (a_id == 0) return -1;

		for (size_t _i = 0; _i < m_decorationVec.size(); ++_i)
		{
			if (m_decorationVec[_i].id == a_id) return static_cast<int>(_i);
		}
		return -1;
	}

	uint32_t UIBase::MakeNewDecorationId() const
	{
		// 今ある番号の最大の次。消した飾りの番号は使い回さない
		// (消したことに気付かず指したままの側が、別の飾りを掴んでしまわないように)
		uint32_t _maxId = 0;
		for (const Decoration::Decoration& _decoration : m_decorationVec)
		{
			_maxId = std::max(_maxId, _decoration.id);
		}
		return _maxId + 1;
	}

	void UIBase::AssignDecorationIds()
	{
		for (size_t _i = 0; _i < m_decorationVec.size(); ++_i)
		{
			Decoration::Decoration& _decoration = m_decorationVec[_i];

			// 前にある飾りと同じ番号なら、後ろのほうを振り直す
			bool _isDuplicated = false;
			for (size_t _j = 0; _j < _i; ++_j)
			{
				if (m_decorationVec[_j].id == _decoration.id) { _isDuplicated = true; break; }
			}

			if (_decoration.id == 0 || _isDuplicated)
			{
				_decoration.id = MakeNewDecorationId();
			}
		}
	}

	//======================================================================================
	// 飾りの描画
	//======================================================================================
	void UIBase::DrawDecorations(
		Engine::GameObject::ObjectContext& a_context,
		const Decoration::DrawOverride& a_override)
	{
		// 出さない指示はここでまとめて弾く。
		// 継承先が Draw を自前で持っていても、切れば(パネルごと隠せば)必ず消えるようにするため
		if (m_decorationVec.empty()) return;
		if (!IsVisibleInHierarchy(a_context)) return;
		if (!a_context.pServices || !a_context.pServices->pMainEngine) return;
		if (!a_context.pServices->pResourceManager) return;

		auto* _pGE = a_context.pServices->pMainEngine->RefGraphicsEngine();
		if (!_pGE) return;

		const Decoration::ParentTransform _parentTr = m_anchor.MakeParentTransform();
		const Decoration::ParentOption _parentOp = MakeParentOption();

		// 配列の順に積む : 後ろにあるものほど手前に出る
		for (const Decoration::Decoration& _decoration : m_decorationVec)
		{
			Decoration::DrawDecoration(
				_pGE,
				a_context.pServices->pResourceManager,
				_decoration,
				_parentTr,
				_parentOp,
				a_override);
		}
	}

	void UIBase::DrawDecorationAt(
		Engine::GameObject::ObjectContext& a_context,
		size_t a_index,
		const Decoration::DrawOverride& a_override)
	{
		if (a_index >= m_decorationVec.size()) return;
		if (!IsVisibleInHierarchy(a_context)) return;
		if (!a_context.pServices || !a_context.pServices->pMainEngine) return;
		if (!a_context.pServices->pResourceManager) return;

		auto* _pGE = a_context.pServices->pMainEngine->RefGraphicsEngine();
		if (!_pGE) return;

		Decoration::DrawDecoration(
			_pGE,
			a_context.pServices->pResourceManager,
			m_decorationVec[a_index],
			m_anchor.MakeParentTransform(),
			MakeParentOption(),
			a_override);
	}

	Decoration::ParentOption UIBase::MakeParentOption() const
	{
		Decoration::ParentOption _parentOp = m_anchor.MakeParentOption();
		_parentOp.opClipRect = m_opClipRect;
		return _parentOp;
	}

	int UIBase::FindDecorationIndex(const std::string& a_name) const
	{
		for (size_t _i = 0; _i < m_decorationVec.size(); ++_i)
		{
			if (m_decorationVec[_i].name == a_name) return static_cast<int>(_i);
		}
		return -1;
	}

	void UIBase::RequestDecorationResources(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pResourceManager) return;

		for (Decoration::Decoration& _decoration : m_decorationVec)
		{
			Decoration::RequestResources(_decoration, a_context.pServices->pResourceManager);
		}
	}

	//======================================================================================
	// シリアライズ
	//--------------------------------------------------------------------------------------
	// 前半はテクスチャを1枚だけ持っていた頃の並びをそのまま残してある。
	// 順番を崩すと、区切りを持たない古い .ob* が読めなくなるため。
	// 飾りの配列は末尾へ足し、配列を持たない古いシーンだけ TexGUID から作り直す
	//======================================================================================
	void UIBase::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// UIBase のぶんを1つの区切りにまとめる。
		// 派生のぶんは派生側で別の区切りに入れるので、ここへ足しても派生の読み出しはずれない
		Engine::Persistence::ArchiveSection _section(a_ar, "UIBase");

		// ---- 旧形式の名残(読み書きは続けるが、使うのは引き継ぎのときだけ) ----
		a_ar.GUIDField("TexGUID", m_legacyTexGUID);

		// ---- アンカー ----
		a_ar.Field("Color", m_anchor.color);

		a_ar.Field("PosPixel", m_anchor.pixelPos);
		a_ar.Field("SizePixel", m_anchor.pixelSize);
		a_ar.Field("m_rotation", m_anchor.rotation);
		a_ar.Field("m_pivot", m_anchor.pivot);
		a_ar.Field("m_uvOffset", m_legacyUvOffset);
		a_ar.Field("m_layer", m_anchor.layer);
		a_ar.Field("m_scale", m_anchor.scale);

		// 湾曲(UI全体に1本の弧として掛かる)
		a_ar.Field("CurveCenter", m_anchor.curveCenter);
		a_ar.Field("CurveRadius", m_anchor.curveRadius);
		a_ar.Field("CurveAngle", m_anchor.curveAngle);

		// 出し分けの状態。※ 追加は必ずここより上でなく区切りの末尾へ
		//    (区切りの中は並び順で読むので、間に挟むと既存のデータがずれる)
		a_ar.Field("IsVisible", m_isVisible);

		// ---- カーソルへの反応 ----
		// 押せない UI(HUD など)も、並びを保つために既定値を書き、読んだ値は捨てる
		UIInteraction _unusedInteraction = {};
		UIInteraction& _interaction = m_opInteraction ? *m_opInteraction : _unusedInteraction;
		_interaction.ArchiveSettings(a_ar);

		// ---- 飾り ----
		size_t _decorationCount = m_decorationVec.size();
		const bool _hasDecorationArray = a_ar.BeginArray("Decorations", _decorationCount);
		if (_hasDecorationArray)
		{
			m_decorationVec.resize(_decorationCount);

			for (size_t _i = 0; _i < _decorationCount; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;

				// 飾り1つごとに区切る。飾りへフィールドを足しても、
				// 後ろの飾りと UIBase の残り(HitFollowAnim など)の読み出しがずれない
				{
					Engine::Persistence::ArchiveSection _decorationSection(a_ar, "Decoration");
					Decoration::ArchiveDecoration(a_ar, m_decorationVec[_i]);
				}

				a_ar.EndObject();
			}
			a_ar.EndArray();
		}

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		a_ar.Field("HitFollowAnim", _interaction.isHitFollowAnim);

		m_anchor.editSize = m_anchor.pixelSize;

		if (!a_ar.IsLoading()) return;

		MigrateLegacyTexture(_hasDecorationArray);
		AssignDecorationIds();

		// 読み込み時は復元したGUIDでテクスチャ・フォントを引き直す。
		// 実体が届くのを待つ必要はないので、要求だけ出して先へ進む
		// (描画側は IsReady を見て、まだのフレームは描かない)
		RequestDecorationResources(a_context);
	}

	//======================================================================================
	// 旧形式からの引き継ぎ
	//--------------------------------------------------------------------------------------
	// 飾りの配列を持たないシーンだけが対象。
	// 既に画像の飾りを持っていれば、その画像へ保存されていたGUIDを移す
	// (作り直すと、入れてあった大きさや色まで消えてしまうため)。
	// 無ければここで1つ作る。継承先の PostDeserialize は
	// この後に走るので、飾りが埋まっているのを見て何もしない
	//======================================================================================
	void UIBase::MigrateLegacyTexture(bool a_hasDecorationArray)
	{
		if (a_hasDecorationArray || !m_legacyTexGUID.IsValid()) return;

		Decoration::ImageData* _pImage = nullptr;
		for (Decoration::Decoration& _decoration : m_decorationVec)
		{
			_pImage = _decoration.RefImage();
			if (_pImage) break;
		}

		if (_pImage == nullptr)
		{
			Decoration::Decoration& _decoration = AddDecoration(Decoration::EDecorationType::Image);
			_decoration.pixelSize = m_anchor.pixelSize;
			_decoration.pivot = m_anchor.pivot;
			_pImage = _decoration.RefImage();
		}

		_pImage->texGUID = m_legacyTexGUID;
		_pImage->quad.uvOffset = m_legacyUvOffset;
	}
}
