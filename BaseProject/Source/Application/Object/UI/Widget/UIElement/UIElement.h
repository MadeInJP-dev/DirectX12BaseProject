#pragma once

#include "Application/Object/UI/UIBase.h"

namespace Engine::GameObject
{
	class ObjectMetaRegistry;
}

namespace App::Object
{
	/// <summary>
	/// 振る舞いを持たない UI : 置いて見せるだけのもの・ほかの UI をまとめる入れ物
	/// </summary>
	/// <remarks>
	/// 見た目(画像・文字・板ポリ)はすべて飾り(Decoration)で決まるので、
	/// 見た目ごとのクラスは持たない。背景・ロゴ・見出し・窓・入れ物はどれもこれで作る。
	/// 押されて何かをするならボタン(UIButton)、値を出すならゲージ(UIGauge)を使う。
	///
	/// ・カーソルを遮るか(BlockCursor)は1つずつの設定。
	///   立てると手前に居る間は裏の UI へカーソルが届かない(開いた窓の裏のボタンを押させない)。
	///   切っておけば、カーソルの取り合いに出ないので下の UI を塞がない(入れ物・見出しなど)。
	///
	/// ・下(子)に置いた UI は、位置・回転・倍率・Z順・表示がこの UI に従う(UIBase の決まり)
	///
	/// 何を別の UI にし、何を飾りにするかは 環境/Docs/UIGuidelines.md
	/// </remarks>
	class UIElement final : public UIBase
	{
	public:

		// 既定はカーソルを遮らない
		UIElement() : UIBase(false) {}

		//=======================================================================
		// カーソルを遮るか
		//=======================================================================
		bool IsBlockCursor() const { return HasInteraction(); }

		/// <summary>
		/// カーソルを遮るかを切り替える
		/// </summary>
		/// <remarks>切るときは、借りている音(乗ったとき・押したとき)を返してから外す</remarks>
		void SetBlockCursor(Engine::GameObject::ObjectContext& a_context, bool a_isBlock);

		//=======================================================================
		// オブジェクト
		//=======================================================================

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "UIElement"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		/// <summary>
		/// カーソルへの反応を持たせる・外す(音は触らない)
		/// </summary>
		/// <remarks>まだ音を借りていないとき(生成直後・読み込み中)だけ使う</remarks>
		void ApplyBlockCursor(bool a_isBlock);
	};
}
