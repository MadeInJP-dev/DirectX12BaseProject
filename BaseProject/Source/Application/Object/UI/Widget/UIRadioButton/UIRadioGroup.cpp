#include "UIRadioGroup.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/EditorField/EditorField.h"

#include "Application/Object/ObjectPicker.h"
#include "Application/Object/UI/Widget/UIRadioButton/UIRadioButton.h"

namespace App::Object
{
	//======================================================================================
	// 並べる
	//======================================================================================
	void UIRadioGroup::Place(Engine::GameObject::ObjectContext& a_context,
		const Math::Vector2& a_rowTopLeft, const Math::Vector2& a_rowSize,
		const Engine::Graphics::UIClipRect* a_pClip) const
	{
		for (const Item& _item : m_itemVec)
		{
			UIBase* _pUI = UIBase::FindUI(a_context, _item.guid);
			if (!_pUI) continue;

			// 大きさはボタンの設定のまま、位置だけ入れる。ピボットのずらしには行の大きさを使う
			const UIAnchor& _anchor = _pUI->GetAnchor();
			_pUI->SetWorldPixelPos({
				a_rowTopLeft.x + _item.offsetX + a_rowSize.x * _anchor.pivot.x,
				a_rowTopLeft.y + a_rowSize.y * _anchor.pivot.y
			});

			if (a_pClip) _pUI->SetClipRect(*a_pClip);
			else _pUI->ClearClipRect();
		}
	}

	//======================================================================================
	// 操作
	//======================================================================================
	int UIRadioGroup::Update(Engine::GameObject::ObjectContext& a_context, int a_selectedIndex) const
	{
		int _clicked = -1;
		for (size_t _i = 0; _i < m_itemVec.size(); ++_i)
		{
			auto* _pButton = Core::TypeInfo::Cast<UIRadioButton>(UIBase::FindUI(a_context, m_itemVec[_i].guid));
			if (!_pButton) continue;

			if (_pButton->IsClicked() && _clicked < 0) _clicked = static_cast<int>(_i);
		}

		// 押されたものがあればそれを、無ければ持ち主の値を選ばれているにする。
		// 押した同じフレームに見た目が切り替わるように(持ち主が値を書くのはこの後)
		const int _shown = (_clicked >= 0) ? _clicked : a_selectedIndex;
		for (size_t _i = 0; _i < m_itemVec.size(); ++_i)
		{
			auto* _pButton = Core::TypeInfo::Cast<UIRadioButton>(UIBase::FindUI(a_context, m_itemVec[_i].guid));
			if (_pButton) _pButton->SetSelected(static_cast<int>(_i) == _shown);
		}
		return _clicked;
	}

	bool UIRadioGroup::IsNavFocused(const Engine::GameObject::ObjectContext& a_context) const
	{
		for (const Item& _item : m_itemVec)
		{
			const UIBase* _pUI = UIBase::FindUI(a_context, _item.guid);
			if (_pUI && _pUI->IsNavFocused()) return true;
		}
		return false;
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void UIRadioGroup::Archive(Engine::Persistence::Archive& a_ar, const std::string& a_name)
	{
		size_t _count = m_itemVec.size();
		if (!a_ar.BeginArray(a_name + "RadioButtons", _count)) return;

		m_itemVec.resize(_count);
		for (size_t _i = 0; _i < _count; ++_i)
		{
			if (!a_ar.BeginObject(_i)) continue;

			a_ar.GUIDField("GUID", m_itemVec[_i].guid);
			a_ar.Field("OffsetX", m_itemVec[_i].offsetX);
			a_ar.EndObject();
		}
		a_ar.EndArray();
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void UIRadioGroup::DrawInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label)
	{
		Engine::EditorField::TreeScope _tree{ a_label };
		if (!_tree) return;

		Engine::EditorField::HelpText("UIRadioButton を選択肢の並びで指す(上から 0, 1, ...)");

		int _removeIndex = -1;
		for (size_t _i = 0; _i < m_itemVec.size(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(_i));
			const std::string _label = "Button" + std::to_string(_i);

			Picker::DrawCombo<UIRadioButton>(_label.c_str(), a_context.pObjectManager, m_itemVec[_i].guid);
			Engine::EditorField::SameLine();
			if (Engine::EditorField::DeleteSmallButton("x")) _removeIndex = static_cast<int>(_i);

			Engine::EditorField::Field("OffsetX", m_itemVec[_i].offsetX, 0.5f);
			Engine::EditorField::Tooltip("行の左端からのずれ(px)");
		}
		if (_removeIndex >= 0)
		{
			m_itemVec.erase(m_itemVec.begin() + _removeIndex);
		}
		if (Engine::EditorField::CreateSmallButton("Add Button"))
		{
			m_itemVec.push_back({});
		}
	}
}
