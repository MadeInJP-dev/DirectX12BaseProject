#include "NumericMutate.h"

#include "Engine/EditorField/EditorField.h"

#include "Application/Object/ObjectPicker.h"
#include "Application/Object/UI/UIBase.h"
#include "Application/Object/UI/Widget/UIGauge/UIGauge.h"

namespace App::Object
{
	//======================================================================================
	// 並べる
	//======================================================================================
	void NumericMutate::Place(Engine::GameObject::ObjectContext& a_context,
		const Math::Vector2& a_rowTopLeft, const Math::Vector2& a_rowSize,
		const Engine::Graphics::UIClipRect* a_pClip)
	{
		m_rowTopLeft = a_rowTopLeft;
		m_rowSize = a_rowSize;
		m_opClipRect = a_pClip ? std::optional<Engine::Graphics::UIClipRect>(*a_pClip) : std::nullopt;

		ApplyModeVisible(a_context);

		PlaceRowItem(a_context, m_gauge);

		if (m_mode == ENumericMutateMode::Button)
		{
			PlaceRowItem(a_context, m_down);
			PlaceRowItem(a_context, m_up);
			return;
		}

		// 溝 : 行の左端からずらし、縦は行の真ん中に合わせる
		const Math::Vector2 _barTopLeft = {
			m_rowTopLeft.x + m_barOffsetX,
			m_rowTopLeft.y + (m_rowSize.y - m_barSize.y) * 0.5f
		};
		m_scrollBar.Place(a_context, _barTopLeft, m_barSize, m_rate, a_pClip);
	}

	void NumericMutate::PlaceRowItem(Engine::GameObject::ObjectContext& a_context, const RowItem& a_item) const
	{
		UIBase* _pUI = UIBase::FindUI(a_context, a_item.guid);
		if (!_pUI) return;

		// 大きさは UI ごとに違ってよいので、位置だけ入れる(判定はその UI の設定のまま)。
		// ピボットのずらしには行の大きさを使う
		const UIAnchor& _anchor = _pUI->GetAnchor();
		_pUI->SetWorldPixelPos({
			m_rowTopLeft.x + a_item.offsetX + m_rowSize.x * _anchor.pivot.x,
			m_rowTopLeft.y + m_rowSize.y * _anchor.pivot.y
		});

		if (m_opClipRect) _pUI->SetClipRect(*m_opClipRect);
		else _pUI->ClearClipRect();
	}

	void NumericMutate::ApplyModeVisible(Engine::GameObject::ObjectContext& a_context)
	{
		const bool _isButton = (m_mode == ENumericMutateMode::Button);

		if (UIBase* _pDown = UIBase::FindUI(a_context, m_down.guid)) _pDown->SetVisible(_isButton);
		if (UIBase* _pUp = UIBase::FindUI(a_context, m_up.guid)) _pUp->SetVisible(_isButton);
		m_scrollBar.SetVisible(a_context, !_isButton);
	}

	//======================================================================================
	// 操作
	//======================================================================================
	bool NumericMutate::Update(Engine::GameObject::ObjectContext& a_context, float& a_inoutValue, float a_min, float a_max, float a_step)
	{
		const float _before = a_inoutValue;
		const float _range = a_max - a_min;

		if (m_mode == ENumericMutateMode::Button)
		{
			// TODO : 押しっぱなしで送り続ける
			if (const int _step = UpdateButtons(a_context); _step != 0)
			{
				a_inoutValue = std::clamp(a_inoutValue + a_step * static_cast<float>(_step), a_min, a_max);
			}
		}
		else
		{
			// 範囲の中の割合で動かす。吸い付きはバーの分割の設定
			float _rate = (_range > 0.0f) ? std::clamp((a_inoutValue - a_min) / _range, 0.0f, 1.0f) : 0.0f;
			if (m_scrollBar.Update(a_context, _rate))
			{
				a_inoutValue = a_min + _rate * _range;
			}
		}

		// つまみは受け取った値の位置へ(持ち主が丸めた値もそのまま映る)
		m_rate = (_range > 0.0f) ? std::clamp((a_inoutValue - a_min) / _range, 0.0f, 1.0f) : 0.0f;
		if (m_mode == ENumericMutateMode::ScrollBar) m_scrollBar.PlaceThumb(a_context, m_rate);

		// 表示 : ゲージの塗りは範囲の中の割合、数値の飾りには値がそのまま出る
		if (auto* _pGauge = Core::TypeInfo::Cast<UIGauge>(UIBase::FindUI(a_context, m_gauge.guid)))
		{
			_pGauge->SetValue(a_inoutValue, a_max, a_min);
		}

		return a_inoutValue != _before;
	}

	int NumericMutate::UpdateButtons(Engine::GameObject::ObjectContext& a_context) const
	{
		int _step = 0;

		UIBase* _pDown = UIBase::FindUI(a_context, m_down.guid);
		if (_pDown && _pDown->IsClicked()) --_step;

		UIBase* _pUp = UIBase::FindUI(a_context, m_up.guid);
		if (_pUp && _pUp->IsClicked()) ++_step;

		return _step;
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void NumericMutate::Archive(Engine::Persistence::Archive& a_ar, const std::string& a_name)
	{
		a_ar.Field(a_name + "Mode", m_mode);

		ArchiveRowItem(a_ar, a_name + "Gauge", m_gauge);
		ArchiveRowItem(a_ar, a_name + "Down", m_down);
		ArchiveRowItem(a_ar, a_name + "Up", m_up);

		m_scrollBar.Archive(a_ar, a_name + "Bar");
		a_ar.Field(a_name + "BarOffsetX", m_barOffsetX);
		a_ar.Field(a_name + "BarSize", m_barSize);
	}

	void NumericMutate::ArchiveRowItem(Engine::Persistence::Archive& a_ar, const std::string& a_name, RowItem& a_item)
	{
		a_ar.GUIDField(a_name + "GUID", a_item.guid);
		a_ar.Field(a_name + "OffsetX", a_item.offsetX);
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void NumericMutate::DrawInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label)
	{
		Engine::EditorField::TreeScope _tree{ a_label };
		if (!_tree) return;

		Engine::EditorField::Field("Mode", m_mode);
		Engine::EditorField::Tooltip("Button : 下げる・上げるボタンで刻みずつ / ScrollBar : 横のバーを掴んで動かす");

		DrawRowItemInspector(a_context, "Gauge", m_gauge);
		Engine::EditorField::Tooltip("今の値を出す UIGauge(Source は Manual にしておく)。行の土台");

		if (m_mode == ENumericMutateMode::Button)
		{
			DrawRowItemInspector(a_context, "Down", m_down);
			DrawRowItemInspector(a_context, "Up", m_up);
			return;
		}

		m_scrollBar.DrawInspector(a_context, "Bar");
		Engine::EditorField::Field("BarOffsetX", m_barOffsetX, 0.5f);
		Engine::EditorField::Tooltip("溝の左端 : 行の左端からのずれ(px)");
		Engine::EditorField::Field("BarSize", m_barSize, 0.5f, 0.0f, 4096.0f);
		Engine::EditorField::Tooltip("溝の大きさ(px)。縦は行の真ん中に合わせる。つまみの太さも同じになる");
	}

	void NumericMutate::DrawRowItemInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label, RowItem& a_item)
	{
		Engine::EditorField::IDScope _id(a_label);

		Picker::DrawCombo<UIBase>(a_label, a_context.pObjectManager, a_item.guid);
		Engine::EditorField::Field("OffsetX", a_item.offsetX, 0.5f);
		Engine::EditorField::Tooltip("行の左端からのずれ(px)");
	}
}
