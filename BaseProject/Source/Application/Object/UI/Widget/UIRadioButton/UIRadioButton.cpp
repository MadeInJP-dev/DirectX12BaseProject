#include "UIRadioButton.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群

#include "Engine/EditorField/EditorField.h"

//==========================================================================================
// UIRadioButton
//
// 押されたことを知らせるところまでは UIButton と同じで、判定・音・状態は UIBase が持つ。
// ここが足すのは「選ばれている」と、それに合わせた飾りの出し分けだけ。
// 乗った・押したの見た目は今までどおり飾りの Reaction で作る。
//==========================================================================================
namespace App::Object
{
	void UIRadioButton::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// 判定と押下の進行、飾りの反応
		UIBase::Update(a_context);

		ApplySelectedVisible();
	}

	// 飾りの isVisible は保存される値だが、毎フレーム入れ直すので残っても害はない
	void UIRadioButton::ApplySelectedVisible()
	{
		ForEachDecorationSet([&](Decoration::UIDecoration& a_decoration)
			{
				for (size_t _i = 0; _i < a_decoration.GetElementCount(); ++_i)
				{
					Decoration::UIDecorationElementBase& _element = a_decoration.RefElement(_i);
					const uint32_t _id = _element.GetId().value;

					const auto _contains = [&](const std::vector<uint32_t>& a_idVec)
						{
							return std::find(a_idVec.begin(), a_idVec.end(), _id) != a_idVec.end();
						};

					if (_contains(m_selectedDecorationIdVec))			_element.SetVisible(m_isSelected);
					else if (_contains(m_unselectedDecorationIdVec))	_element.SetVisible(!m_isSelected);
				}
			});
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void UIRadioButton::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// 位置・色・飾りなどの共通ぶん
		UIBase::Archive(a_ar, a_context);

		// ここから下は UIRadioButton のぶん。足すときはこの区切りの末尾へ
		Engine::Persistence::ArchiveSection _section(a_ar, "UIRadioButton");

		a_ar.VectorField("SelectedDecorations", m_selectedDecorationIdVec);
		a_ar.VectorField("UnselectedDecorations", m_unselectedDecorationIdVec);
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void UIRadioButton::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("RadioButton");
		Engine::EditorField::HelpText("どれを選ばれているにするかは持ち主(UIRadioGroup など)が決める。押されても自分では切り替えない");

		DrawDecorationIdList("SelectedDecorations", m_selectedDecorationIdVec);
		Engine::EditorField::Tooltip("選ばれている間だけ出す飾り(塗りの帯・明るい文字など)");

		DrawDecorationIdList("UnselectedDecorations", m_unselectedDecorationIdVec);
		Engine::EditorField::Tooltip("選ばれていない間だけ出す飾り(暗い文字など)。どちらにも入れない飾りは常に出る");

		// 実行中の状態は表示のみ
		Engine::EditorField::Value("Selected", "%s", m_isSelected ? "true" : "false");
	}

	void UIRadioButton::DrawDecorationIdList(const char* a_label, std::vector<uint32_t>& a_inoutIdVec)
	{
		Engine::EditorField::TreeScope _tree{ a_label };
		if (!_tree) return;

		int _removeIndex = -1;
		for (size_t _i = 0; _i < a_inoutIdVec.size(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(_i));
			DrawDecorationPicker("Decoration", a_inoutIdVec[_i]);
			Engine::EditorField::SameLine();
			if (Engine::EditorField::DeleteSmallButton("x")) _removeIndex = static_cast<int>(_i);
		}
		if (_removeIndex >= 0)
		{
			a_inoutIdVec.erase(a_inoutIdVec.begin() + _removeIndex);
		}
		if (Engine::EditorField::CreateSmallButton("Add"))
		{
			a_inoutIdVec.push_back(0);
		}
	}
}
