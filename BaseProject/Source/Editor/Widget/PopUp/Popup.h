#pragma once
namespace Editor
{
	class Popup
	{
	public:

		explicit Popup(const std::string& a_name) : m_name(a_name) {}
		~Popup() = default;

		// 表示する内容
		void SetContent(const std::function<void()>& a_func) { m_content = a_func; }

		// 開く要求
		void Open() { m_isOpenRequested = true; }

		// 描画
		void Draw();
	private:
		
		// ウィンドウ情報
		std::string m_name;					// 名前
		std::function<void()> m_content;	// 中身の処理

		bool m_isOpenRequested = false;		// 開く要求
		bool m_isOpen = false;				// 開いているかどうか
	};
}