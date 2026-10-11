#pragma once

#include "ScrollPanelBase.h"

#include <span>

namespace App::Object
{
	//===============================================================================================
	// プルダウンの操作 : 選択欄を押すと候補が開き、候補を押すとそれに決まって閉じる
	//
	// 選択肢の値の型を知らない部分だけを持つ(値の表は OptionEnumSelector が持つ)。
	// 行の UI(OptionDropdownUI)もここが持ち、並べ方・保存・インスペクターは
	// RefUI / GetUI を ScrollPanelBase の PlaceDropdown / ArchiveDropdown / DrawDropdownInspector へ渡す。
	//
	//   開く   … 選択欄を押す
	//   閉じる … 候補を押す(選んで閉じる) / 選択欄をもう一度押す / 外を押す / 戻るキー
	//
	// 開いている間は、戻るキーで設定画面ごと閉じないよう、パネルの IsCapturingInput で知らせること
	//===============================================================================================
	class OptionDropdown
	{
	public:

		/// <summary>
		/// 更新 : 開閉と、候補の選択
		/// </summary>
		/// <param name="a_names">選択肢の表示名(上から候補のボタンへ書き込む)</param>
		/// <returns>選ばれた候補の添え字。選ばれなければ -1</returns>
		int Update(Engine::GameObject::ObjectContext& a_context, std::span<const char* const> a_names);

		// 選択欄へ今の選択の名前を書き込む
		void SetFieldText(Engine::GameObject::ObjectContext& a_context, const std::string& a_text) const;

		// 開いているか
		bool IsOpen() const { return m_isOpen; }

		// 閉じる(パネルを閉じたときなど)
		void Close() { m_isOpen = false; }

		// 行の UI(見出し・選択欄・候補)
		const OptionDropdownUI& GetUI() const { return m_ui; }
		OptionDropdownUI& RefUI() { return m_ui; }

	private:

		// カーソルが選択欄か候補の上に居るか(外を押したかを見るため)
		bool IsCursorOnDropdown(Engine::GameObject::ObjectContext& a_context) const;

		// 候補の出し入れと名前の書き込み
		void ApplyCandidates(Engine::GameObject::ObjectContext& a_context, std::span<const char* const> a_names) const;

	private:

		OptionDropdownUI m_ui = {};		// 行の UI(保存はパネルが ArchiveDropdown で行う)
		bool m_isOpen = false;			// 候補が開いているか(保存しない)
	};
}
