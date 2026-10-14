#include "UIButton.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群

#include "Engine/EditorField/EditorField.h"

//==========================================================================================
// UIButton
//
// 「押されたことを知らせる」だけのUI。何をするかは SetOnClick で外から差し込む。
//
// カーソルの判定・押下の進行・音・状態は UIBase が持っている。
// 元はここに全部あったが、「乗ったら枠を出す」「押したら縮む」を
// ボタン以外のUI(背景・パネル・ミッションの項目)でもやりたくなったので上げた。
// 見た目の変化も飾り側(Decoration の Reaction)へ移したので、
// ここに残っているのは差し込み口だけになっている。
//==========================================================================================
namespace App::Object
{
	void UIButton::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// 判定と押下の進行、飾りの反応
		UIBase::Update(a_context);

		// 押し切られた瞬間だけ呼ぶ
		if (IsClicked() && m_onClick) m_onClick();
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void UIButton::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("Button");
		Engine::EditorField::HelpText("判定・音・状態は上の Interaction、見た目は飾りの Reaction");

		// 差し込まれているかどうかだけ出す(中身はコードなので触れない)
		Engine::EditorField::Value("OnClick", "%s", m_onClick ? "set" : "none");
	}
}
