#pragma once
#include "../Popup.h"

namespace Editor
{
	//==========================================================================================
	//
	// 名前を1つ打って決めるモーダル
	//
	// 「名前を付けて保存」「新しく作る」のように、名前の欄・決定・やめるだけの窓が
	// 何か所もあったのでまとめたもの。
	//
	// ・開いた瞬間に欄へフォーカスが入る。Enter でも決定できる
	// ・名前が空のあいだは決定を押せない
	// ・決定の処理が false を返したら閉じない(同名がある等。理由はログか説明欄に出す)
	//
	//==========================================================================================
	class NameInputPopup
	{
	public:

		// a_name         : ポップアップの名前(見出し)
		// a_fieldLabel   : 欄のラベル
		// a_confirmLabel : 決定ボタンの文字
		NameInputPopup(const std::string& a_name, const std::string& a_fieldLabel, const std::string& a_confirmLabel);
		~NameInputPopup() = default;
		NON_COPYABLE_NON_MOVABLE(NameInputPopup);

		// 決定したときの処理。true を返すと閉じる
		void SetOnConfirm(const std::function<bool(const std::string&)>& a_func) { m_onConfirm = a_func; }

		// 欄の下に出す説明を、入力中の名前から作る(保存先など)。空を返せば出さない
		void SetHint(const std::function<std::string(const std::string&)>& a_func) { m_hint = a_func; }

		// 開く要求。a_initialName が欄の初期値になる
		void Open(const std::string& a_initialName = "");

		// 描画
		void Draw();

	private:

		// 中身
		void DrawContent();

	private:

		Popup m_popup;
		std::string m_fieldLabel;
		std::string m_confirmLabel;
		std::string m_input;			// 入力中の名前

		std::function<bool(const std::string&)> m_onConfirm;
		std::function<std::string(const std::string&)> m_hint;
	};
}
