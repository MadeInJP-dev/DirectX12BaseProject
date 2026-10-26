#include "NameInputPopup.h"

namespace Editor
{
	namespace
	{
		// ボタンの幅(決定とやめるを同じ幅で並べる)
		const Math::Vector2 BUTTON_SIZE = { 120.0f, 0.0f };
	}

	NameInputPopup::NameInputPopup(const std::string& a_name, const std::string& a_fieldLabel, const std::string& a_confirmLabel)
		: m_popup(a_name)
		, m_fieldLabel(a_fieldLabel)
		, m_confirmLabel(a_confirmLabel)
	{
		m_popup.SetContent([this]() { DrawContent(); });
	}

	void NameInputPopup::Open(const std::string& a_initialName)
	{
		m_input = a_initialName;
		m_popup.Open();
	}

	void NameInputPopup::Draw()
	{
		m_popup.Draw();
	}

	void NameInputPopup::DrawContent()
	{
		// 開いた瞬間から打てるようにする(欄を押してから打つ手間を省く)
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

		const bool _isEnterPressed = Engine::EditorField::ConfirmField(m_fieldLabel.c_str(), m_input);

		if (m_hint)
		{
			const std::string _hint = m_hint(m_input);
			if (!_hint.empty()) Engine::EditorField::HelpText("%s", _hint.c_str());
		}

		const bool _isEmpty = m_input.empty();

		Engine::EditorField::Line();

		ImGui::BeginDisabled(_isEmpty);
		const bool _isConfirmPressed = Engine::EditorField::Button(m_confirmLabel.c_str(), BUTTON_SIZE);
		ImGui::EndDisabled();

		if ((_isConfirmPressed || _isEnterPressed) && !_isEmpty)
		{
			// 失敗(同名がある等)のときは閉じない。直して打ち直せるようにする
			if (!m_onConfirm || m_onConfirm(m_input))
			{
				m_popup.Close();
			}
		}

		Engine::EditorField::SameLine();
		if (Engine::EditorField::Button("Cancel", BUTTON_SIZE)) m_popup.Close();
	}
}
