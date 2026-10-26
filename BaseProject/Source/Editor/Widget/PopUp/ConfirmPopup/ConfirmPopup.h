#pragma once
#include "../Popup.h"

namespace Editor
{
	// 確認の選択肢の見た目
	enum class EConfirmStyle
	{
		Normal,		// ふつうのボタン
		Danger,		// 消す・捨てる(赤)
	};

	//==========================================================================================
	//
	// 押し間違いが痛い操作の前に聞くモーダル
	//
	// 本文と選択肢を並べ、右端には必ず Cancel が付く。
	// 選択肢を押すとその処理を呼んで閉じる。
	//
	// 別のモーダルの中でも使える(Draw を親の中身から呼ぶ)
	//
	//==========================================================================================
	class ConfirmPopup
	{
	public:

		explicit ConfirmPopup(const std::string& a_name);
		~ConfirmPopup() = default;
		NON_COPYABLE_NON_MOVABLE(ConfirmPopup);

		// 本文。a_detail は下に薄く添える説明(空なら出さない)
		void SetMessage(const std::string& a_message, const std::string& a_detail = "");

		// 選択肢を足す(足した順に左から並ぶ)
		void AddChoice(const std::string& a_label, const std::function<void()>& a_func, EConfirmStyle a_style = EConfirmStyle::Normal);

		// 開く要求
		void Open() { m_popup.Open(); }

		// 描画
		void Draw();

	private:

		// 中身
		void DrawContent();

	private:

		// 選択肢1つ
		struct Choice
		{
			std::string label = "";
			std::function<void()> func = nullptr;
			EConfirmStyle style = EConfirmStyle::Normal;
		};

		Popup m_popup;
		std::string m_message;
		std::string m_detail;
		std::vector<Choice> m_choiceVec;
	};
}
