#include "UIPanel.h"

#include "Engine/EditorField/EditorField.h"

namespace App::Object
{
	void UIPanel::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Header("Panel");
		Engine::EditorField::HelpText("ヒエラルキーでこの下に置いた UI は、このパネルの Visible に従います");
		Engine::EditorField::HelpText("伝わるのは表示だけ(位置・回転・倍率は伝わらない)");

		UIBase::DrawInspector(a_context);
	}
}
