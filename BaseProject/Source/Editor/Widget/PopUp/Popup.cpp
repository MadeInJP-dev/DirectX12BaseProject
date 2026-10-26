#include "Popup.h"
namespace Editor
{
	void Popup::Draw()
	{
		Draw(m_content);
	}

	void Popup::Draw(const std::function<void()>& a_content)
	{
		const std::string _windowName = MakeWindowName();

		// 描画要求が来たらポップアップウィンドウを開く
		if (m_isOpenRequested)
		{
			ImGui::OpenPopup(_windowName.c_str());
			m_isOpenRequested = false;
			m_isCloseRequested = false;

			m_isOpen = true;
		}
		if (!m_isOpen) return;

		// 大きい窓は画面の真ん中に、決めた割合で出す
		if (m_screenRatio > 0.0f)
		{
			const ImVec2 _display = ImGui::GetIO().DisplaySize;
			ImGui::SetNextWindowSize(ImVec2(_display.x * m_screenRatio, _display.y * m_screenRatio), ImGuiCond_Appearing);
			ImGui::SetNextWindowPos(ImVec2(_display.x * 0.5f, _display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}

		// 大きさを決めた窓は動かせるように、決めていない窓は中身に合わせる
		const ImGuiWindowFlags _flags = (m_screenRatio > 0.0f) ? ImGuiWindowFlags_NoCollapse : ImGuiWindowFlags_AlwaysAutoResize;

		// ポップアップウィンドウを描画
		bool _isWindowOpen = true;
		const bool _isVisible = ImGui::BeginPopupModal(_windowName.c_str(), m_isClosable ? &_isWindowOpen : nullptr, _flags);
		if (_isVisible)
		{
			// 処理があれば関数呼び出す
			if (a_content)
			{
				a_content();
			}

			// 閉じるのはこのポップアップの中で(CloseCurrentPopup はここでしか効かない)
			if (m_isCloseRequested)
			{
				ImGui::CloseCurrentPopup();
				m_isCloseRequested = false;
			}

			ImGui::EndPopup();
		}

		// 中身が閉じた・× で閉じられた
		if (!_isVisible || !_isWindowOpen)
		{
			m_isOpen = false;
			if (!_isWindowOpen && m_onClosed) m_onClosed();
		}
	}

	std::string Popup::MakeWindowName() const
	{
		if (m_title.empty()) return m_name;
		return m_title + "###" + m_name;
	}
}
