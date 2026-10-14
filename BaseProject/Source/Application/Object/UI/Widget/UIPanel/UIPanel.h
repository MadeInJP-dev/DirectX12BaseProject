#pragma once

#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	/// <summary>
	/// UIをまとめて出し入れするための入れ物
	/// </summary>
	/// <remarks>
	/// ヒエラルキーでこのパネルの下(子・孫)に置いた UI は、パネルの表示に従う。
	/// パネルを SetVisible(false) にすれば、下にあるものは自分の Visible に関係なく
	/// 描かれず、押せなくもなる。進行役(HomeSequence など)が画面の切り替えで
	/// ボタンを1つずつ出し入れしていたのを、パネル1つの切り替えで済ませるためのもの。
	///
	/// ・伝わるのは表示だけ。位置・回転・倍率は伝わらない(子は自分の PixelPos のまま)
	/// ・パネルの下にパネルを入れてよい。上にあるパネルが1つでも隠れていれば出ない
	/// ・パネルでない UI を親にしても何も伝わらない(エディターでまとめるだけの親子のため)
	/// ・パネル自身も飾りを持てる。背景の板などを置いておけば、中身と一緒に出入りする
	/// ・押せない(カーソルの取り合いに出ない)ので、下のボタンを塞ぐことはない。
	///   背後のボタンを押させたくないときは、押せる UI(UIImage)を間に敷くこと
	/// </remarks>
	class UIPanel final : public UIBase
	{
	public:

		// 押せない UI として作る
		UIPanel() : UIBase(false) {}

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "UIPanel"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;
	};
}
