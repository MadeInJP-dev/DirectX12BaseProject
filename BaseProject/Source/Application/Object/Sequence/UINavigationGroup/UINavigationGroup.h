#pragma once

#include "Engine/GameObject/BaseObject/BaseObject.h"

namespace App::Object
{
	class UIBase;

	//===========================================================================
	// UIグループ : キー・パッドで UI を順に選ぶ(項目移動)
	//
	// 各 UI が持つ「前・次の UI」(UIBase::GetNavPrevGUID / GetNavNextGUID)をたどって、
	// 選んでいる UI を移す。つながりは UI 側が持つので、ここが保存するのは最初の UI だけ。
	// 今どれを選んでいるかは実行中だけ覚える(保存しない)。
	//
	//   RB(NextItem。キーボードは割り当て無し)          … 次へ
	//   LB(PrevItem)                                   … 前へ
	//   矢印キー / 左スティック / 十字キー(BottonMove) … 上・左で前、下・右で次
	//   Enter / A(Decide)                              … 選んでいる UI を押す(押すのは UIInteraction)
	//
	// ・マウスを動かす・クリックするとカーソル操作へ戻り、選択の表示は消える。
	//   カーソル操作の間は、カーソルが乗った UI を「今選んでいるもの」として覚えておき、
	//   次にキーを押したときはそこから動く。切り替えた最初の1回は選択を見せるだけで動かさない。
	// ・選ばれている UI は「乗っている」(IsHovered)と同じ扱いになる。
	//   飾りの Hovered・乗った音・進行役の「乗ったら説明を出す」がそのまま効く。
	// ・選べるのは、出ていて(上の親も含む)触れる(Interactable)押せる UI だけ。
	//   選べないものは飛ばして、その先へ進む。
	// ・選べる UI が1つも無い間(パネルごと隠れているなど)は何もしない。
	//   また出てきたときは最初の UI から選び直す(SetCurrentGUID で決めてあればそちら)。
	// ・同じシーンで複数のグループが同時に出ているときは、Priority の大きい1つだけが動く
	//   (一覧の上に確認の箱を出すときなど)。取り合いは UINavigationResource
	//===========================================================================
	class UINavigationGroup : public Engine::GameObject::BaseObject
	{
	public:

		// 更新前処理 : 入力を見て選択を移し、選んでいる UI を名乗る
		// (UI が Update で「選ばれているか」を見るので、それより前に済ませる)
		void PreUpdate(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		// 最初の UI
		const Core::GUID& GetFirstGUID() const { return m_firstGUID; }

		// 今選んでいる UI(まだ一度も出ていなければ空)
		const Core::GUID& GetCurrentGUID() const { return m_currentGUID; }

		/// <summary>
		/// 選ぶ UI を外から決める
		/// </summary>
		/// <remarks>
		/// 進行役が画面を開くときに、最初に選んでおく項目を変えたいとき用。
		/// グループが隠れている間に決めておけば、出てきたときに最初の UI より優先する
		/// </remarks>
		void SetCurrentGUID(const Core::GUID& a_guid);

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "UINavigationGroup"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		/// <summary>
		/// 今選ぶ UI を決め直す
		/// </summary>
		/// <returns>選べる UI が1つも無ければ nullptr</returns>
		/// <remarks>
		/// 出てきた直後なら最初の UI(外から決めてあればそれ)から。
		/// 選んでいた UI が選べなくなっていたら(隠れた・無効にされた)、次へ進めたものにする
		/// </remarks>
		UIBase* ResolveCurrent(const Engine::GameObject::ObjectContext& a_context);

		/// <summary>
		/// a_pFrom から a_step の向きへたどって、最初に選べる UI
		/// </summary>
		/// <param name="a_step">正で次、負で前</param>
		/// <returns>一周しても無い・端で途切れたら nullptr(a_pFrom 自身は返さない)</returns>
		UIBase* FindSelectable(const Engine::GameObject::ObjectContext& a_context, const UIBase* a_pFrom, int a_step) const;

		/// <summary>
		/// a_pFrom の前の UI
		/// </summary>
		/// <remarks>
		/// UI の Prev を使う。空なら最初の UI から Next をたどって、Next が a_pFrom のものを前とみなす
		/// (Next だけつないでも上・左で戻れるように)
		/// </remarks>
		UIBase* FindPrev(const Engine::GameObject::ObjectContext& a_context, const UIBase* a_pFrom) const;

		// 最初の UI から Next をたどった中で、カーソルが乗っている UI(無ければ nullptr)
		UIBase* FindHoveredInChain(const Engine::GameObject::ObjectContext& a_context) const;

		// 選べるか : 押せて、触れて、出ている(上の親も含む)
		static bool IsSelectable(const Engine::GameObject::ObjectContext& a_context, const UIBase* a_pUI);

		// インスペクター : 最初の UI から Next をたどった並び(つなぎ方の確認用)
		void DrawChainInspector(Engine::GameObject::ObjectContext& a_context);

	private:

		//------------------------------------------------------------------------------
		// 設定(保存される)
		//------------------------------------------------------------------------------
		Core::GUID m_firstGUID = {};	// 最初に選ぶ UI。ここから各 UI の Next をたどる
		int m_priority = 0;				// 同時に出ている他のグループとの優先度(大きいほうが動く)

		//------------------------------------------------------------------------------
		// 状態(保存しない)
		//------------------------------------------------------------------------------
		Core::GUID m_currentGUID = {};		// 今選んでいる UI
		bool m_isCurrentRequested = false;	// 外から選ぶ UI を決められたか(出てきたときに最初の UI より優先する)
		bool m_isAvailable = false;			// 前のフレームに選べる UI があったか(出てきた瞬間を知るため)
		bool m_isFocusShown = false;		// 前のフレームに選択を見せていたか(切り替えた最初の1回は動かさないため)
	};
}
