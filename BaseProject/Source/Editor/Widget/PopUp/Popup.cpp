#include "Popup.h"
namespace Editor
{
	void Editor::Popup::Draw()
	{
		// 描画要求が来たらポップアップウィンドウを開く
		if (m_isOpenRequested)
		{
			ImGui::OpenPopup(m_name.c_str());
			m_isOpenRequested = false;

			m_isOpen = true;
		}

		// ポップアップウィンドウを描画
		if (ImGui::BeginPopupModal(
			m_name.c_str(),
			&m_isOpen,
			ImGuiWindowFlags_AlwaysAutoResize
		))
		{
			// 処理があれば関数呼び出す
			if (m_content)
			{
				m_content();
			}

			ImGui::EndPopup();
		}
	}
}