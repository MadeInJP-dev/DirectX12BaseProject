//==========================================================================================
// UIBase のエディター用の処理 : 親の飾りへまとめる
//
// 別の UI にしておく理由(環境/Docs/UIGuidelines.md の「別の UI にする条件」)が
// 1つも無い UI を、親の UI の飾りへ移す。
//
// ・まとめられるのは振る舞いを持たない UI(UIElement)だけ。
//   ボタン・ゲージ・HUD は自分の処理を持っているので、飾りにすると動かなくなる。
// ・飾りは画面での見た目が変わらないように、親から見た値へ直して足す。
//   (アンカーの位置・回転・倍率・Z順・色を飾りの値へ畳み込む)
//==========================================================================================
#include "UIBase.h"

#include <nlohmannJSON/json.hpp>

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"	// 飾りを写すときの ResourceRef の複製

#include "Application/Object/UI/Widget/UIElement/UIElement.h"

namespace App::Object
{
	namespace
	{
		// 色の成分を割る : 親の色で割ってから親の色を掛け直すと元の色になるように
		bool DivideColor(const Math::Color& a_value, const Math::Color& a_divisor, Math::Color& a_outColor)
		{
			constexpr float COLOR_EPSILON = 1e-4f;
			if (a_divisor.r < COLOR_EPSILON || a_divisor.g < COLOR_EPSILON ||
				a_divisor.b < COLOR_EPSILON || a_divisor.a < COLOR_EPSILON) return false;

			a_outColor = Math::Color(
				a_value.r / a_divisor.r,
				a_value.g / a_divisor.g,
				a_value.b / a_divisor.b,
				a_value.a / a_divisor.a);
			return true;
		}

		// 他のオブジェクトの保存値に a_guid が書かれているか
		bool IsReferencedByOthers(
			Engine::GameObject::ObjectContext& a_context,
			const Engine::GameObject::BaseObject* a_pSelf,
			const Core::GUID& a_guid)
		{
			if (!a_context.pObjectManager) return false;

			const std::string _guid = a_guid.String();

			for (const auto& _upObject : a_context.pObjectManager->GetObjects())
			{
				if (!_upObject || _upObject.get() == a_pSelf) continue;

				// 親の GUID は個別データの外(GameObjectManager)に書かれるので、ここには入らない
				nlohmann::json _json = {};
				{
					Engine::Persistence::Archive _ar(Engine::Persistence::Archive::EMode::Save, _json);
					_upObject->Archive(_ar, a_context);
				}
				if (_json.dump().find(_guid) != std::string::npos) return true;
			}
			return false;
		}
	}

	//======================================================================================
	// まとめられるか
	//--------------------------------------------------------------------------------------
	// 軽い条件から順に見て、最後に GUID で指されていないかを探す
	//======================================================================================
	bool UIBase::CanMergeIntoParent(Engine::GameObject::ObjectContext& a_context, std::string& a_outReason) const
	{
		const UIBase* _pParent = Core::TypeInfo::Cast<const UIBase>(GetParent());
		if (!_pParent)
		{
			a_outReason = "親が UI ではない";
			return false;
		}

		if (!Core::TypeInfo::Cast<const UIElement>(this))
		{
			a_outReason = "振る舞いを持つクラス(UIElement 以外)";
			return false;
		}

		if (HasInteraction())
		{
			a_outReason = "カーソルを遮っている(BlockCursor)";
			return false;
		}

		if (m_transition.isUseShow || m_transition.isUseHide || m_transition.isPulseOnly)
		{
			a_outReason = "自分の出し入れ・合図の演出を持っている";
			return false;
		}

		// 片方だけが組を2つ持つと、移した飾りがデバイスによって消える(または両方に出る)
		if (HasGamepadDecorations() || _pParent->HasGamepadDecorations())
		{
			a_outReason = "自分か親がコントローラー用の飾りを持っている";
			return false;
		}

		if (!(GetMaterialGUID() == _pParent->GetMaterialGUID()))
		{
			a_outReason = "親と違う UIマテリアルを付けている";
			return false;
		}

		if (m_anchor.curveAngle != 0.0f)
		{
			a_outReason = "自分の湾曲を持っている";
			return false;
		}

		if (m_anchor.isScanLine != _pParent->m_anchor.isScanLine || m_isHUD != _pParent->m_isHUD)
		{
			a_outReason = "スキャンライン・HUD の設定が親と違う";
			return false;
		}

		// 押せる親へ移すと、飾りの反応が親の状態(乗った・押した)で動き出してしまう
		if (_pParent->HasInteraction())
		{
			for (size_t _i = 0; _i < m_decoration.GetElementCount(); ++_i)
			{
				if (m_decoration.GetElement(_i).GetReaction())
				{
					a_outReason = "反応(Reaction)を持つ飾りがあり、親が押せる UI";
					return false;
				}
			}
		}

		// 当たり判定を受ける親へボディを移すと、親の当たる範囲が広がってしまう
		if (_pParent->IsHitTarget() && m_decoration.HasHitBody())
		{
			a_outReason = "当たり判定のボディを持つ飾りがあり、親が当たり判定を受ける UI";
			return false;
		}

		Math::Color _unused = {};
		if (!DivideColor(m_anchor.color, _pParent->m_anchor.color, _unused))
		{
			a_outReason = "親の色に 0 の成分がある(色を畳み込めない)";
			return false;
		}

		if (IsReferencedByOthers(a_context, this, GetGUID()))
		{
			a_outReason = "他のオブジェクトから GUID で指されている";
			return false;
		}

		a_outReason.clear();
		return true;
	}

	//======================================================================================
	// まとめる
	//--------------------------------------------------------------------------------------
	// 飾りは自分のアンカーの上に乗っているので、画面での置き場所を求めてから、
	// 親のアンカーから見た値へ戻す(UIBaseInspector の飾りのギズモと同じ式)。
	//   位置   : 画面の点 → 親の回転を戻して親の倍率で割る
	//   回転   : 自分の回転 - 親の回転 を足す
	//   倍率   : 自分の倍率 / 親の倍率 を掛ける
	//   Z順    : 自分の Z順 - 親の Z順 を足す
	//   色     : 自分の色 / 親の色 を掛ける(描くときに親の色が掛かって元に戻る)
	//   表示   : 自分が隠れていたら、移した飾りも隠す
	//======================================================================================
	bool UIBase::MergeIntoParent(Engine::GameObject::ObjectContext& a_context)
	{
		std::string _reason = {};
		if (!CanMergeIntoParent(a_context, _reason)) return false;

		UIBase* _pParent = Core::TypeInfo::Cast<UIBase>(GetParent());

		const UIAnchor _selfWorld = CalcWorldAnchor();
		const UIAnchor _parentWorld = _pParent->CalcWorldAnchor();

		Math::Color _colorMul = {};
		DivideColor(m_anchor.color, _pParent->m_anchor.color, _colorMul);

		constexpr float SCALE_EPSILON = 1e-6f;
		const float _parentScale = (std::fabs(_parentWorld.scale) > SCALE_EPSILON) ? _parentWorld.scale : 1.0f;

		for (size_t _i = 0; _i < m_decoration.GetElementCount(); ++_i)
		{
			const Decoration::UIDecorationElementBase& _source = m_decoration.GetElement(_i);

			// 番号は親の中で振り直す(親の飾りと重ならないように)
			Decoration::UIDecorationElementBase& _element = _pParent->m_decoration.AddElementCopy(_source);

			// ずれ・倍率には飾り全体の倍率も掛かっているので、畳み込んでから親の値へ直す
			const float _selfScale = _selfWorld.scale * m_decoration.GetScale();
			const Math::Vector2 _screenPos = _selfWorld.pixelPos + UIAnchor::RotateDeg(_source.GetOffsetPos() * _selfScale * _source.GetScale(), _selfWorld.rotation);
			// 画面での大きさが変わらないよう、親の倍率 × 親の飾り全体の倍率で割る
			const float _parentDecorationScale = _pParent->m_decoration.GetScale();
			const float _parentAllScale = (std::fabs(_parentScale * _parentDecorationScale) > SCALE_EPSILON) ? _parentScale * _parentDecorationScale : 1.0f;
			const float _newScale = _source.GetScale() * _selfScale / _parentAllScale;
			const Math::Vector2 _parentLocal = UIAnchor::RotateDeg(_screenPos - _parentWorld.pixelPos, -_parentWorld.rotation) / _parentAllScale;

			_element.SetOffsetPos((std::fabs(_newScale) > SCALE_EPSILON) ? _parentLocal / _newScale : _parentLocal);
			_element.SetRotation(_source.GetRotation() + _selfWorld.rotation - _parentWorld.rotation);
			_element.SetScale(_newScale);
			_element.SetLayerOffset(_source.GetLayerOffset() + _selfWorld.layer - _parentWorld.layer);

			const Math::Color& _color = _source.GetColor();
			_element.SetColor(Math::Color(
				_color.r * _colorMul.r,
				_color.g * _colorMul.g,
				_color.b * _colorMul.b,
				_color.a * _colorMul.a));
			_element.SetVisible(_source.IsVisible() && m_isVisible);
		}

		// 足した飾りのテクスチャ・フォントを親の側で引き直す
		_pParent->RequestDecorationResources(a_context);

		// 自分の下の UI は親へ付け替える(写しを回す : 付け替えで自分の子の一覧が縮むため)
		const std::vector<Engine::GameObject::BaseObject*> _childVec = GetChildren();
		for (Engine::GameObject::BaseObject* _pChild : _childVec)
		{
			if (_pChild) _pChild->SetParent(_pParent);
		}

		ReserveDestroy();
		return true;
	}
}
