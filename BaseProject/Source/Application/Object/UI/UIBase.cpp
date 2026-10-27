#include "UIBase.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/UIMaterial/UIMaterial.h"
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"	// 項目移動の前・次を GUID で引く
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Input/InputDeviceManager.h"		// 最後に触ったデバイス(コントローラー用の飾りへ切り替える)

#include "Application/InstanceResource/UserDataResource.h"	// HUD を出すか(プレイヤーの設定)
#include "Application/Game/UserData/UserData.h"

namespace App::Object
{
	namespace
	{
		//----------------------------------------------------------------------------------
		// 画面の上での置き場所(位置・回転・倍率・Z順)
		//
		// UI の行列は「倍率 → Z軸回り → 平行移動」だけでできているので、
		// 掛け合わせても必ずこの4つへ戻せる(倍率は縦横同じ)
		//----------------------------------------------------------------------------------
		struct UIPlacement
		{
			Math::Vector2 pos = {};		// px
			float rotation = 0.0f;		// 度(UIAnchor::RotateDeg と同じ向き)
			float scale = 1.0f;
			float layer = 0.0f;			// Z順(行列の Z の移動に入れる。倍率は掛からないので親子で足し算になる)
		};

		// 置き場所から行列を組む。行ベクトルなので点は 倍率 → 回転 → 移動 の順に掛かり、
		// UIAnchor::ToScreenPos と同じ点へ移る(1行目・2行目の回し方は RotateDeg と同じ向き)。
		// Z は倍率を掛けずに移動だけ持たせるので、子の Z順は「親の Z順 + 自分の Z順」になる
		Math::Matrix MakeUIMatrix(const UIPlacement& a_placement)
		{
			const float _rad = DirectX::XMConvertToRadians(a_placement.rotation);
			const float _cos = std::cos(_rad) * a_placement.scale;
			const float _sin = std::sin(_rad) * a_placement.scale;

			return Math::Matrix(
				 _cos, _sin, 0.0f, 0.0f,
				-_sin, _cos, 0.0f, 0.0f,
				 0.0f, 0.0f, 1.0f, 0.0f,
				 a_placement.pos.x, a_placement.pos.y, a_placement.layer, 1.0f);
		}

		// 行列から置き場所へ戻す
		UIPlacement DecomposeUIMatrix(const Math::Matrix& a_matrix)
		{
			UIPlacement _placement = {};
			_placement.pos = { a_matrix._41, a_matrix._42 };
			_placement.scale = std::sqrt(a_matrix._11 * a_matrix._11 + a_matrix._12 * a_matrix._12);
			_placement.rotation = DirectX::XMConvertToDegrees(std::atan2(a_matrix._12, a_matrix._11));
			_placement.layer = a_matrix._43;
			return _placement;
		}

		// 親の置き場所の中の点(親から見た値)を、画面の点へ
		Math::Vector2 ToParentScreen(const UIPlacement& a_parent, const Math::Vector2& a_localPos)
		{
			return a_parent.pos + UIAnchor::RotateDeg(a_localPos * a_parent.scale, a_parent.rotation);
		}

		// 画面の点を、親の置き場所の中の点(親から見た値)へ。倍率が0の親の中には戻せないので false
		bool ToParentLocal(const UIPlacement& a_parent, const Math::Vector2& a_screenPos, Math::Vector2& a_outLocalPos)
		{
			constexpr float SCALE_EPSILON = 1e-6f;
			if (std::fabs(a_parent.scale) < SCALE_EPSILON) return false;

			a_outLocalPos = UIAnchor::RotateDeg(a_screenPos - a_parent.pos, -a_parent.rotation) / a_parent.scale;
			return true;
		}
	}

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
		// 出す組(キーボード用 / コントローラー用)を決める。
		// 判定の大きさも描画も同じ組を見るので、どちらよりも先に済ませる
		UpdateShownDecorationSet(a_context);

		// プリセットが変わっていれば、判定の大きさを測る前に写しておく
		SyncDecorationPresets(a_context);

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

		// 取り合いは画面での Z順(親の Z順込み)で比べる
		m_opInteraction->ClaimCursor(a_context, this, CalcWorldAnchor().layer, _isReceivable, _isInside);
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

		// 飾りは今の状態を受け取って、そこへ寄っていく。
		// 出していない組も進める(デバイスを持ち替えた瞬間に、止まっていた所から動き出さないように)
		const Decoration::EUIState _state = GetUIState();

		ForEachDecorationSet([&](std::vector<Decoration::Decoration>& a_decorationVec)
			{
				for (Decoration::Decoration& _decoration : a_decorationVec)
				{
					Decoration::AdvanceAnimation(_decoration, _state, a_context.dt);
				}
			});

		// 出し入れと合図の演出も、飾りと同じく出していないフレームも進める
		// (消える演出が終わったことを知るのはここ)
		m_transition.Advance(a_context.dt);
	}

	Decoration::EUIState UIBase::GetUIState() const
	{
		// 押せない UI は自分では何もされることがない
		const Decoration::EUIState _selfState = m_opInteraction ? m_opInteraction->GetState() : Decoration::EUIState::Normal;
		if (_selfState != Decoration::EUIState::Normal || !m_isReactToChildren) return _selfState;

		return FindChildrenState(this);
	}

	// 子(孫も)のうち、乗られている・押されているものの状態。押しているほうを優先する。
	// 子の状態は子の Update で進むので、並びによっては1フレーム前の値になる(見た目だけなので構わない)
	Decoration::EUIState UIBase::FindChildrenState(const Engine::GameObject::BaseObject* a_pObject)
	{
		Decoration::EUIState _state = Decoration::EUIState::Normal;
		for (const Engine::GameObject::BaseObject* _pChild : a_pObject->GetChildren())
		{
			const UIBase* _pUI = Core::TypeInfo::Cast<const UIBase>(_pChild);
			if (_pUI && !_pUI->m_isVisible) continue;

			if (_pUI && _pUI->IsPressed()) return Decoration::EUIState::Pressed;
			if (_pUI && _pUI->IsHovered()) _state = Decoration::EUIState::Hovered;

			const Decoration::EUIState _childState = FindChildrenState(_pChild);
			if (_childState == Decoration::EUIState::Pressed) return _childState;
			if (_childState == Decoration::EUIState::Hovered) _state = _childState;
		}
		return _state;
	}

	void UIBase::SetInteractable(bool a_isInteractable)
	{
		if (m_opInteraction) m_opInteraction->isInteractable = a_isInteractable;
	}

	//======================================================================================
	// 表示の切り替え
	//--------------------------------------------------------------------------------------
	// 切り替わったときだけ演出を流す。進行役が毎フレーム同じ値を入れてくる
	// (一覧のスクロールバーなど)ので、同じ値では何もしない
	//======================================================================================
	void UIBase::SetVisible(bool a_isVisible)
	{
		if (m_isVisible == a_isVisible) return;

		m_isVisible = a_isVisible;
		m_transition.OnVisibleChanged(a_isVisible);
	}

	//======================================================================================
	// 実際に出ているか : 自分の指定と、上にある親全員の表示
	//======================================================================================
	bool UIBase::IsVisibleInHierarchy(const Engine::GameObject::ObjectContext& /*a_context*/) const
	{
		if (!m_isVisible) return false;
		return IsParentsVisible();
	}

	//======================================================================================
	// 描くか
	//--------------------------------------------------------------------------------------
	// 出ているかどうか(IsVisibleInHierarchy)とは分けてある。
	// 消える演出の最中は描くが押せず、合図の間だけ描く UI は出ていても描かないため
	//======================================================================================
	bool UIBase::IsDrawnInHierarchy(const Engine::GameObject::ObjectContext& a_context) const
	{
		if (!IsSelfDrawn()) return false;
		return IsParentsDrawn(a_context);
	}

	bool UIBase::IsSelfDrawn() const
	{
		if (!m_isVisible && !m_transition.IsHiding()) return false;
		if (m_transition.isPulseOnly && !m_transition.IsPulsePlaying()) return false;
		return true;
	}

	//======================================================================================
	// 上にある親が全員出ているか(押せるかの判断用)
	//--------------------------------------------------------------------------------------
	// UI に限らず、親の IsVisible を見る(MissionSelect のような進行役も出し入れを持つ)。
	// 出し入れを持たない親(既定で常に表示)は素通しになる。
	// (輪は BaseObject::SetParent が作らせないので、根まで辿れば必ず止まる)
	//======================================================================================
	bool UIBase::IsParentsVisible() const
	{
		for (const Engine::GameObject::BaseObject* _pParent = GetParent(); _pParent; _pParent = _pParent->GetParent())
		{
			if (!_pParent->IsVisible()) return false;
		}
		return true;
	}

	//======================================================================================
	// 上にある親が全員描かれているか(描くかの判断用)
	//--------------------------------------------------------------------------------------
	// UI の親は自分と同じ決まりで見る : 消える演出の最中は描かれているので、子も一緒に描いて
	// 一緒に薄れさせる(ApplyTransition が親の濃さを掛ける)。HUD として隠れている親の子も隠す
	//======================================================================================
	bool UIBase::IsParentsDrawn(const Engine::GameObject::ObjectContext& a_context) const
	{
		for (const Engine::GameObject::BaseObject* _pParent = GetParent(); _pParent; _pParent = _pParent->GetParent())
		{
			if (const UIBase* _pParentUI = Core::TypeInfo::Cast<const UIBase>(_pParent))
			{
				if (!_pParentUI->IsSelfDrawn()) return false;
				if (_pParentUI->IsHiddenAsHUD(a_context)) return false;
				continue;
			}

			if (!_pParent->IsVisible()) return false;
		}
		return true;
	}

	float UIBase::CalcParentsTransitionAlpha() const
	{
		float _alpha = 1.0f;
		for (const Engine::GameObject::BaseObject* _pParent = GetParent(); _pParent; _pParent = _pParent->GetParent())
		{
			if (const UIBase* _pParentUI = Core::TypeInfo::Cast<const UIBase>(_pParent))
			{
				_alpha *= _pParentUI->m_transition.CalcValue().alpha;
			}
		}
		return _alpha;
	}

	//======================================================================================
	// 画面での置き場所
	//--------------------------------------------------------------------------------------
	// 親の行列から親の置き場所を取り出し、自分のアンカーをその中へ置く。
	// 自分の行列ごと掛けて戻さないのは、回転の値(370度など入れた値)を
	// 丸めずにそのまま残すため。結果は CalcWorldMatrix を戻したものと同じになる
	//======================================================================================
	UIAnchor UIBase::CalcWorldAnchor() const
	{
		UIAnchor _world = m_anchor;
		_world.pixelPos = m_anchor.pixelPos + m_runtimeOffset;

		const Engine::GameObject::BaseObject* _pParent = GetParent();
		if (!_pParent) return _world;

		const Math::Matrix _parentWorld = _pParent->CalcWorldMatrix();

		// 置き場所を持たない親(進行役など)だけを辿ったときは何も変わらない
		if (_parentWorld == Math::Matrix::Identity()) return _world;

		const UIPlacement _parent = DecomposeUIMatrix(_parentWorld);
		_world.pixelPos = ToParentScreen(_parent, _world.pixelPos);
		_world.rotation = m_anchor.rotation + _parent.rotation;
		_world.scale = m_anchor.scale * _parent.scale;
		_world.layer = m_anchor.layer + _parent.layer;
		return _world;
	}

	void UIBase::SetWorldPixelPos(const Math::Vector2& a_screenPos)
	{
		Math::Vector2 _localPos = a_screenPos;

		if (const Engine::GameObject::BaseObject* _pParent = GetParent())
		{
			const UIPlacement _parent = DecomposeUIMatrix(_pParent->CalcWorldMatrix());
			if (!ToParentLocal(_parent, a_screenPos, _localPos)) return;
		}

		// 実行中だけのずらしは保存しないので、アンカーには入れない
		m_anchor.pixelPos = _localPos - m_runtimeOffset;
	}

	void UIBase::SetWorldPixelRect(const Math::Vector2& a_topLeft, const Math::Vector2& a_size)
	{
		m_anchor.pixelSize = a_size;
		SetWorldPixelPos({
			a_topLeft.x + a_size.x * m_anchor.pivot.x,
			a_topLeft.y + a_size.y * m_anchor.pivot.y
		});
	}

	//======================================================================================
	// 行列 : 子の UI はこれに追従する
	//======================================================================================
	Math::Matrix UIBase::CalcLocalMatrix() const
	{
		UIPlacement _placement = {};
		_placement.pos = m_anchor.pixelPos + m_runtimeOffset;
		_placement.rotation = m_anchor.rotation;
		_placement.scale = m_anchor.scale;
		_placement.layer = m_anchor.layer;
		return MakeUIMatrix(_placement);
	}

	//======================================================================================
	// 親が変わっても画面での置き場所を保つ
	//--------------------------------------------------------------------------------------
	// 画面での置き場所を古い親の中で求め、新しい親の中の値へ戻す。
	// 回転・倍率・Z順は差だけを足し引きする(親が回っていなければ入れた値がそのまま残る)
	//======================================================================================
	void UIBase::RebaseLocal(const Math::Matrix& a_oldParentWorld, const Math::Matrix& a_newParentWorld)
	{
		if (a_oldParentWorld == a_newParentWorld) return;

		const UIPlacement _oldParent = DecomposeUIMatrix(a_oldParentWorld);
		const UIPlacement _newParent = DecomposeUIMatrix(a_newParentWorld);

		const Math::Vector2 _screenPos = ToParentScreen(_oldParent, m_anchor.pixelPos + m_runtimeOffset);

		Math::Vector2 _localPos = {};
		if (!ToParentLocal(_newParent, _screenPos, _localPos)) return;

		m_anchor.pixelPos = _localPos - m_runtimeOffset;
		m_anchor.rotation = m_anchor.rotation + _oldParent.rotation - _newParent.rotation;
		m_anchor.scale = m_anchor.scale * _oldParent.scale / _newParent.scale;
		m_anchor.layer = m_anchor.layer + _oldParent.layer - _newParent.layer;
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

		// 画面での置き場所(実行中だけのずらし・親の行列込み)
		const UIAnchor _anchor = CalcWorldAnchor();

		a_outRect.rotation = _anchor.rotation;
		a_outRect.padding = m_opInteraction->hitPadding;

		// アンカーに大きさが入っていても、今の絵へ追従させる指示があれば飾りを優先する
		const bool _isUseDecorationBounds = _isHitFollowAnim || !_anchor.HasSize();

		if (_isUseDecorationBounds)
		{
			Math::Vector2 _center = {};
			Math::Vector2 _size = {};
			if (CalcDecorationBounds(_center, _size, _isHitFollowAnim))
			{
				a_outRect.pixelPos = _anchor.ToScreenPos(_center);	// 範囲の中心を指す点。回転と倍率はアンカーのものが乗る
				a_outRect.pixelSize = _size * _anchor.scale;
				a_outRect.pivot = { 0.5f, 0.5f };						// 中心を出しているのでピボットは中央
				return true;
			}

			// 測れる飾りが1つも無ければアンカーの矩形へ戻る(文字だけのUIなど)
		}

		if (!_anchor.HasSize()) return false;

		a_outRect.pixelPos = _anchor.pixelPos;
		a_outRect.pixelSize = _anchor.pixelSize;
		a_outRect.pivot = _anchor.pivot;
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
	// エディターのシーンビューで選ぶか
	//======================================================================================
	bool UIBase::IsPickedAt(const Engine::GameObject::ObjectContext& a_context, const Math::Vector2& a_uiPos) const
	{
		if (!IsDrawnInHierarchy(a_context)) return false;

		// 押せる UI は、押したときと同じ範囲で選ぶ(切り抜きもそこで見る)
		if (m_opInteraction) return IsPointInsideSelf(a_uiPos);

		// 押せない UI : 見えていないところでは選ばない
		if (m_opClipRect)
		{
			const Engine::Graphics::UIClipRect& _clip = m_opClipRect.value();
			if (a_uiPos.x < _clip.min.x || a_uiPos.y < _clip.min.y) return false;
			if (a_uiPos.x > _clip.max.x || a_uiPos.y > _clip.max.y) return false;
		}

		// 見えている飾りの範囲(無ければアンカーの矩形)
		const UIAnchor _anchor = CalcWorldAnchor();

		Math::Vector2 _center = {};
		Math::Vector2 _size = {};
		if (CalcDecorationBounds(_center, _size))
		{
			return UIAnchor::IsPointInside(
				a_uiPos, _anchor.ToScreenPos(_center), _size * _anchor.scale, { 0.5f, 0.5f }, _anchor.rotation, {});
		}

		if (!_anchor.HasSize()) return false;
		return UIAnchor::IsPointInside(a_uiPos, _anchor.pixelPos, _anchor.pixelSize, _anchor.pivot, _anchor.rotation, {});
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

		// 判定も見えている組に合わせる(コントローラー用は絵の大きさが違うことがある)
		for (const Decoration::Decoration& _decoration : GetShownDecorationVec())
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
		return AddDecorationTo(m_decorationVec, a_type);
	}

	Decoration::Decoration& UIBase::AddDecorationTo(std::vector<Decoration::Decoration>& a_decorationVec, Decoration::EDecorationType a_type)
	{
		// 番号は配列が伸びる前に決める(伸びた後だと、作りかけの 0 番を数えてしまう)
		const uint32_t _id = MakeNewDecorationId();

		Decoration::Decoration& _decoration = a_decorationVec.emplace_back();
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
		_decoration.name = std::string(_typeName) + std::to_string(a_decorationVec.size());

		return _decoration;
	}

	Decoration::Decoration* UIBase::FindDecoration(const std::string& a_name)
	{
		const int _index = FindDecorationIndex(a_name);
		return (_index >= 0) ? &RefShownDecorationVec()[_index] : nullptr;
	}

	Decoration::Decoration* UIBase::FindDecorationById(uint32_t a_id)
	{
		const int _index = FindDecorationIndexById(a_id);
		return (_index >= 0) ? &RefShownDecorationVec()[_index] : nullptr;
	}

	const Decoration::Decoration* UIBase::FindDecorationById(uint32_t a_id) const
	{
		const int _index = FindDecorationIndexById(a_id);
		return (_index >= 0) ? &GetShownDecorationVec()[_index] : nullptr;
	}

	int UIBase::FindDecorationIndexById(uint32_t a_id) const
	{
		if (a_id == 0) return -1;

		const std::vector<Decoration::Decoration>& _decorationVec = GetShownDecorationVec();
		for (size_t _i = 0; _i < _decorationVec.size(); ++_i)
		{
			if (_decorationVec[_i].id == a_id) return static_cast<int>(_i);
		}
		return -1;
	}

	uint32_t UIBase::MakeNewDecorationId() const
	{
		// 今ある番号の最大の次。消した飾りの番号は使い回さない
		// (消したことに気付かず指したままの側が、別の飾りを掴んでしまわないように)。
		// 組をまたいで数える : 片方の組にだけ足した飾りが、もう片方の別の飾りと同じ番号にならないように
		uint32_t _maxId = 0;
		for (const Decoration::Decoration& _decoration : m_decorationVec)
		{
			_maxId = std::max(_maxId, _decoration.id);
		}
		if (m_opGamepadDecorationVec)
		{
			for (const Decoration::Decoration& _decoration : *m_opGamepadDecorationVec)
			{
				_maxId = std::max(_maxId, _decoration.id);
			}
		}
		return _maxId + 1;
	}

	// 番号の重なりは組の中だけで見る(コントローラー用は既定の組と同じ番号を持つのが正しい形)
	void UIBase::AssignDecorationIds()
	{
		ForEachDecorationSet([this](std::vector<Decoration::Decoration>& a_decorationVec)
			{
				AssignDecorationIdsIn(a_decorationVec);
			});
	}

	void UIBase::AssignDecorationIdsIn(std::vector<Decoration::Decoration>& a_decorationVec)
	{
		for (size_t _i = 0; _i < a_decorationVec.size(); ++_i)
		{
			Decoration::Decoration& _decoration = a_decorationVec[_i];

			// 前にある飾りと同じ番号なら、後ろのほうを振り直す
			bool _isDuplicated = false;
			for (size_t _j = 0; _j < _i; ++_j)
			{
				if (a_decorationVec[_j].id == _decoration.id) { _isDuplicated = true; break; }
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
		const std::vector<Decoration::Decoration>& _decorationVec = GetShownDecorationVec();
		if (_decorationVec.empty()) return;
		if (!IsDrawnInHierarchy(a_context)) return;
		if (IsHiddenAsHUD(a_context)) return;
		if (!a_context.pServices || !a_context.pServices->pMainEngine) return;
		if (!a_context.pServices->pResourceManager) return;

		auto* _pGE = a_context.pServices->pMainEngine->RefGraphicsEngine();
		if (!_pGE) return;

		// 描くのは PreUpdate を回さない場面(エディターで止めている間など)もあるので、ここでも写す
		SyncDecorationPresets(a_context);

		const Decoration::ParentTransform _parentTr = MakeParentTransform();
		Decoration::ParentOption _parentOp = MakeParentOption();
		_parentOp.pMaterial = FindReadyMaterial(a_context.pServices->pResourceManager);
		const Decoration::DrawOverride _override = ApplyTransition(a_override);

		// 配列の順に積む : 後ろにあるものほど手前に出る
		for (const Decoration::Decoration& _decoration : _decorationVec)
		{
			Decoration::DrawDecoration(
				_pGE,
				a_context.pServices->pResourceManager,
				_decoration,
				_parentTr,
				_parentOp,
				_override);
		}
	}

	void UIBase::DrawDecorationAt(
		Engine::GameObject::ObjectContext& a_context,
		size_t a_index,
		const Decoration::DrawOverride& a_override)
	{
		const std::vector<Decoration::Decoration>& _decorationVec = GetShownDecorationVec();
		if (a_index >= _decorationVec.size()) return;
		if (!IsDrawnInHierarchy(a_context)) return;
		if (IsHiddenAsHUD(a_context)) return;
		if (!a_context.pServices || !a_context.pServices->pMainEngine) return;
		if (!a_context.pServices->pResourceManager) return;

		auto* _pGE = a_context.pServices->pMainEngine->RefGraphicsEngine();
		if (!_pGE) return;

		SyncDecorationPresets(a_context);

		Decoration::ParentOption _parentOp = MakeParentOption();
		_parentOp.pMaterial = FindReadyMaterial(a_context.pServices->pResourceManager);

		Decoration::DrawDecoration(
			_pGE,
			a_context.pServices->pResourceManager,
			_decorationVec[a_index],
			MakeParentTransform(),
			_parentOp,
			ApplyTransition(a_override));
	}

	//======================================================================================
	// 演出の値を掛ける
	//--------------------------------------------------------------------------------------
	// 継承先が渡した差し替え(位置・UV・色など)はそのまま残し、倍率と色の a にだけ掛ける。
	// 色の a には親の UI の演出の濃さも掛ける(親が薄れて消えるとき、子も一緒に薄れる)。
	// 倍率は DrawOverride::scale へ入れるので、飾りの位置のずれにも掛かる
	// (枠と文字がばらけずに、UI がまとまって伸び縮みする)
	//======================================================================================
	Decoration::DrawOverride UIBase::ApplyTransition(const Decoration::DrawOverride& a_override) const
	{
		const UITransitionValue _value = m_transition.CalcValue();

		Decoration::DrawOverride _override = a_override;
		_override.scale *= _value.scale;
		_override.tint.a *= _value.alpha * CalcParentsTransitionAlpha();

		// 消え具合は「残っている割合」を掛け合わせる(継承先が渡した分と演出の分)
		_override.dissolve = 1.0f - (1.0f - a_override.dissolve) * (1.0f - _value.dissolve);
		return _override;
	}

	//======================================================================================
	// マテリアル
	//--------------------------------------------------------------------------------------
	// UI 単位で1つ持ち、飾り全部に同じ効果を掛ける(ParentOption::pMaterial)。
	// 読み終わるまでは効果なしで描く(届くまで描かないと、出た瞬間に UI が見えない)
	//======================================================================================
	void UIBase::SetMaterialGUID(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid)
	{
		m_materialGUID = a_guid;
		RequestMaterial(a_context);
	}

	void UIBase::RequestMaterial(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pResourceManager) return;

		m_materialRef = m_materialGUID.IsValid()
			? a_context.pServices->pResourceManager->RequestLoad<Engine::Resource::UIMaterial>(m_materialGUID)
			: Engine::ResourceRef<Engine::Resource::UIMaterial>();
	}

	const Engine::Resource::UIMaterial* UIBase::FindReadyMaterial(Engine::Resource::ResourceManager* a_pResourceManager) const
	{
		if (!a_pResourceManager || !m_materialRef.IsValid()) return nullptr;
		if (!a_pResourceManager->IsReady(m_materialRef)) return nullptr;

		return a_pResourceManager->Get(m_materialRef);
	}

	//======================================================================================
	// HUD として隠れているか
	//--------------------------------------------------------------------------------------
	// プレイヤーの設定はユーザーデータ(ワールドの UserDataResource から引く)。
	// 引けないワールド(エディターの確認用など)では隠さない
	//======================================================================================
	bool UIBase::IsHiddenAsHUD(const Engine::GameObject::ObjectContext& a_context) const
	{
		if (!m_isHUD) return false;

		// 上へ重ねたシーン(リザルト)がまとめて隠している
		if (s_isHUDSuppressed) return true;

		const auto* _pUserDataResource = InstanceResource::UserDataResource::Find(a_context.pWorld);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return false;

		return !_pUserDataResource->pUserData->GetGraphicsSettings().isHUDVisible;
	}

	Decoration::ParentTransform UIBase::MakeParentTransform() const
	{
		// 画面での置き場所(実行中だけのずらし・親の行列込み)から出す
		return CalcWorldAnchor().MakeParentTransform();
	}

	Decoration::ParentOption UIBase::MakeParentOption() const
	{
		Decoration::ParentOption _parentOp = m_anchor.MakeParentOption();
		_parentOp.opClipRect = m_opClipRect;
		return _parentOp;
	}

	int UIBase::FindDecorationIndex(const std::string& a_name) const
	{
		const std::vector<Decoration::Decoration>& _decorationVec = GetShownDecorationVec();
		for (size_t _i = 0; _i < _decorationVec.size(); ++_i)
		{
			if (_decorationVec[_i].name == a_name) return static_cast<int>(_i);
		}
		return -1;
	}

	//======================================================================================
	// コントローラー用の飾りの出し分け
	//--------------------------------------------------------------------------------------
	// 見るのは InputDeviceManager の「最後に触ったデバイス」。
	// 項目移動(UINavigationGroup)のように Tab や矢印でも切り替えるのではなく、
	// 実際にコントローラーを触っているかだけで決める(キーボードで項目を動かしても、
	// 出す絵はキーボードのまま)。まだ何も触っていない(None)ならキーボードとみなす
	//======================================================================================
	std::vector<Decoration::Decoration>& UIBase::RefShownDecorationVec()
	{
		return IsShowingGamepadDecorations() ? *m_opGamepadDecorationVec : m_decorationVec;
	}

	const std::vector<Decoration::Decoration>& UIBase::GetShownDecorationVec() const
	{
		return IsShowingGamepadDecorations() ? *m_opGamepadDecorationVec : m_decorationVec;
	}

	void UIBase::UpdateShownDecorationSet(const Engine::GameObject::ObjectContext& a_context)
	{
		// エディターで確かめている間は、そちらの指定を優先する
		switch (m_decorationPreview)
		{
		case EDecorationPreview::KeyboardMouse: m_isShowGamepad = false; return;
		case EDecorationPreview::Gamepad:       m_isShowGamepad = true;  return;
		case EDecorationPreview::Auto:
		default: break;
		}

		if (!a_context.pServices || !a_context.pServices->pInputManager) return;

		using Engine::Input::EInputDevice;
		const EInputDevice _device = a_context.pServices->pInputManager->GetInputDeviceManager().GetActiveDevice();
		m_isShowGamepad = (_device == EInputDevice::Gamepad);
	}

	// 番号ごと写す : 同じ役の飾りが両方の組で同じ番号を持つように(上の説明を参照)
	void UIBase::CreateGamepadDecorations()
	{
		if (m_opGamepadDecorationVec) return;
		m_opGamepadDecorationVec = m_decorationVec;
	}

	void UIBase::RemoveGamepadDecorations()
	{
		m_opGamepadDecorationVec = std::nullopt;
		m_isEditGamepadSet = false;
		m_editDecorationIndex = -1;
	}

	//======================================================================================
	// プリセット
	//--------------------------------------------------------------------------------------
	// 指しているプリセットの番号が変わった飾りだけ写し直す(変わっていなければ番号を見るだけ)。
	// 写した飾りは見た目(テクスチャ・フォント)が変わっているかもしれないので引き直す
	//======================================================================================
	void UIBase::SyncDecorationPresets(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pResourceManager) return;
		auto* _pResourceManager = a_context.pServices->pResourceManager;

		// 出していない組も写す(持ち替えたときに古い見た目が一瞬出ないように)
		ForEachDecorationSet([_pResourceManager](std::vector<Decoration::Decoration>& a_decorationVec)
			{
				for (Decoration::Decoration& _decoration : a_decorationVec)
				{
					if (Decoration::SyncPreset(_decoration, _pResourceManager))
					{
						Decoration::RequestResources(_decoration, _pResourceManager);
					}
				}
			});
	}

	void UIBase::RequestDecorationResources(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pResourceManager) return;
		auto* _pResourceManager = a_context.pServices->pResourceManager;

		ForEachDecorationSet([_pResourceManager](std::vector<Decoration::Decoration>& a_decorationVec)
			{
				for (Decoration::Decoration& _decoration : a_decorationVec)
				{
					Decoration::RequestResources(_decoration, _pResourceManager);
				}
			});
	}

	//======================================================================================
	// 項目移動の前・次
	//======================================================================================
	const Core::GUID& UIBase::GetNavPrevGUID() const
	{
		static const Core::GUID EMPTY_GUID = {};
		return m_opInteraction ? m_opInteraction->navPrevGUID : EMPTY_GUID;
	}

	const Core::GUID& UIBase::GetNavNextGUID() const
	{
		static const Core::GUID EMPTY_GUID = {};
		return m_opInteraction ? m_opInteraction->navNextGUID : EMPTY_GUID;
	}

	void UIBase::SetNavPrevGUID(const Core::GUID& a_guid)
	{
		if (m_opInteraction) m_opInteraction->navPrevGUID = a_guid;
	}

	void UIBase::SetNavNextGUID(const Core::GUID& a_guid)
	{
		if (m_opInteraction) m_opInteraction->navNextGUID = a_guid;
	}

	UIBase* UIBase::FindNavPrev(const Engine::GameObject::ObjectContext& a_context) const
	{
		return FindUI(a_context, GetNavPrevGUID());
	}

	UIBase* UIBase::FindNavNext(const Engine::GameObject::ObjectContext& a_context) const
	{
		return FindUI(a_context, GetNavNextGUID());
	}

	//======================================================================================
	// 項目移動の左・右
	//======================================================================================
	const Core::GUID& UIBase::GetNavLeftGUID() const
	{
		static const Core::GUID EMPTY_GUID = {};
		return m_opInteraction ? m_opInteraction->navLeftGUID : EMPTY_GUID;
	}

	const Core::GUID& UIBase::GetNavRightGUID() const
	{
		static const Core::GUID EMPTY_GUID = {};
		return m_opInteraction ? m_opInteraction->navRightGUID : EMPTY_GUID;
	}

	void UIBase::SetNavLeftGUID(const Core::GUID& a_guid)
	{
		if (m_opInteraction) m_opInteraction->navLeftGUID = a_guid;
	}

	void UIBase::SetNavRightGUID(const Core::GUID& a_guid)
	{
		if (m_opInteraction) m_opInteraction->navRightGUID = a_guid;
	}

	UIBase* UIBase::FindNavLeft(const Engine::GameObject::ObjectContext& a_context) const
	{
		return FindUI(a_context, GetNavLeftGUID());
	}

	UIBase* UIBase::FindNavRight(const Engine::GameObject::ObjectContext& a_context) const
	{
		return FindUI(a_context, GetNavRightGUID());
	}

	// ポインタは持たずに毎回引く。相手が消されたときに古いポインタを踏まないように
	// (引くのは GUID の対応表なので軽い)
	UIBase* UIBase::FindUI(const Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid)
	{
		if (!a_context.pObjectManager || !a_guid.IsValid()) return nullptr;

		return Core::TypeInfo::Cast<UIBase>(a_context.pObjectManager->FindByGUID(a_guid));
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
		const bool _hasDecorationArray = ArchiveDecorationVec(a_ar, "Decorations", m_decorationVec);

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		a_ar.Field("HitFollowAnim", _interaction.isHitFollowAnim);

		// 区切りを持たない古い .ob* には無いので読まない(読まなければクラスの既定値のまま)
		if (!a_ar.IsLegacyLayout()) a_ar.Field("IsHUD", m_isHUD);
		if (!a_ar.IsLegacyLayout()) a_ar.Field("IsScanLine", m_anchor.isScanLine);

		// 出し入れと合図の演出。自分の区切りを持つので、この後ろへ足しても読み出しはずれない
		if (!a_ar.IsLegacyLayout())
		{
			const bool _isLoaded = m_transition.Archive(a_ar);
			if (a_ar.IsLoading()) m_isTransitionLoaded = _isLoaded;
		}

		// 飾り全部に掛ける効果(UIマテリアル)
		if (!a_ar.IsLegacyLayout()) a_ar.GUIDField("MaterialGUID", m_materialGUID);

		// 項目移動の前・次の UI。押せない UI も並びを保つために空の GUID を書き、読んだ値は捨てる
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.GUIDField("NavPrevGUID", _interaction.navPrevGUID);
			a_ar.GUIDField("NavNextGUID", _interaction.navNextGUID);
		}

		// コントローラー用の飾り(任意)。持つかどうかの印を先に書き、持たない UI も
		// 並びを保つために空の配列を書く(読んだ空の配列は、印が立っていなければ捨てる)
		if (!a_ar.IsLegacyLayout())
		{
			bool _hasGamepadDecorations = m_opGamepadDecorationVec.has_value();
			a_ar.Field("HasGamepadDecorations", _hasGamepadDecorations);

			std::vector<Decoration::Decoration> _unusedDecorationVec = {};
			std::vector<Decoration::Decoration>& _gamepadDecorationVec =
				(a_ar.IsLoading() || !m_opGamepadDecorationVec) ? _unusedDecorationVec : *m_opGamepadDecorationVec;
			ArchiveDecorationVec(a_ar, "GamepadDecorations", _gamepadDecorationVec);

			if (a_ar.IsLoading())
			{
				m_opGamepadDecorationVec = _hasGamepadDecorations
					? std::optional<std::vector<Decoration::Decoration>>(std::move(_unusedDecorationVec))
					: std::nullopt;
			}
		}

		// 項目移動の左・右の UI。前・次と同じく、押せない UI も空の GUID を書いて並びを保つ
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.GUIDField("NavLeftGUID", _interaction.navLeftGUID);
			a_ar.GUIDField("NavRightGUID", _interaction.navRightGUID);
		}

		// 子の状態にも反応するか
		if (!a_ar.IsLegacyLayout()) a_ar.Field("ReactToChildren", m_isReactToChildren);

		m_anchor.editSize = m_anchor.pixelSize;

		if (!a_ar.IsLoading()) return;

		MigrateLegacyTexture(_hasDecorationArray);
		AssignDecorationIds();

		// 読み込み時は復元したGUIDでテクスチャ・フォント・マテリアルを引き直す。
		// 実体が届くのを待つ必要はないので、要求だけ出して先へ進む
		// (描画側は IsReady を見て、まだのフレームは描かない)
		RequestDecorationResources(a_context);
		RequestMaterial(a_context);
	}

	//======================================================================================
	// 飾りの配列1つぶん
	//--------------------------------------------------------------------------------------
	// 既定の組とコントローラー用の組で同じ形にしてある
	//======================================================================================
	bool UIBase::ArchiveDecorationVec(Engine::Persistence::Archive& a_ar, const std::string& a_name, std::vector<Decoration::Decoration>& a_decorationVec)
	{
		size_t _decorationCount = a_decorationVec.size();
		if (!a_ar.BeginArray(a_name, _decorationCount)) return false;

		a_decorationVec.resize(_decorationCount);

		for (size_t _i = 0; _i < _decorationCount; ++_i)
		{
			if (!a_ar.BeginObject(_i)) continue;

			// 飾り1つごとに区切る。飾りへフィールドを足しても、
			// 後ろの飾りと UIBase の残り(HitFollowAnim など)の読み出しがずれない
			{
				Engine::Persistence::ArchiveSection _decorationSection(a_ar, "Decoration");
				Decoration::ArchiveDecoration(a_ar, a_decorationVec[_i]);
			}

			a_ar.EndObject();
		}
		a_ar.EndArray();
		return true;
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
