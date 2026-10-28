#include "UIElement.h"

#include "Engine/EditorField/EditorField.h"

namespace App::Object
{
	//======================================================================================
	// カーソルを遮るか
	//--------------------------------------------------------------------------------------
	// 遮る = カーソルへの反応(UIInteraction)を持つこと。持たない UI は取り合いに出ない
	//======================================================================================
	void UIElement::SetBlockCursor(Engine::GameObject::ObjectContext& a_context, bool a_isBlock)
	{
		if (IsBlockCursor() == a_isBlock) return;

		if (!a_isBlock && m_opInteraction) m_opInteraction->ReleaseSounds(a_context);
		ApplyBlockCursor(a_isBlock);

		// 遮るなら当たり判定も受ける(当たる場所は飾りのボディで決める)
		SetHitTarget(a_isBlock);
	}

	void UIElement::ApplyBlockCursor(bool a_isBlock)
	{
		if (a_isBlock)
		{
			if (!m_opInteraction) m_opInteraction.emplace();
		}
		else
		{
			m_opInteraction.reset();
		}
	}

	//======================================================================================
	// アーカイブ
	//--------------------------------------------------------------------------------------
	// 遮るかは基底のぶん(カーソルへの反応の設定を含む)より後ろに書かれる。
	// 読み込みでは、いったん遮る UI にしてから基底を読み(反応の設定を捨てずに読むため)、
	// 遮るかを読み終えてから外すかどうかを決める。
	// 保存値を持たない古いデータは、読む前の値(旧クラスの既定値)のまま
	//======================================================================================
	void UIElement::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		bool _isBlock = IsBlockCursor();
		if (a_ar.IsLoading()) ApplyBlockCursor(true);

		UIBase::Archive(a_ar, a_context);

		// ここから下は UIElement のぶん。足すときはこの区切りの末尾へ
		Engine::Persistence::ArchiveSection _section(a_ar, "UIElement");

		if (!a_ar.IsLegacyLayout()) a_ar.Field("IsBlockCursor", _isBlock);

		if (a_ar.IsLoading()) ApplyBlockCursor(_isBlock);
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void UIElement::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Header("Element");

		bool _isBlock = IsBlockCursor();
		if (Engine::EditorField::Field("BlockCursor", _isBlock)) SetBlockCursor(a_context, _isBlock);
		Engine::EditorField::Tooltip("立てると、手前に居る間は裏の UI へカーソルが届かない(窓の裏のボタンを押させない)。入れ物・見出しは切っておく");

		UIBase::DrawInspector(a_context);
	}
}
