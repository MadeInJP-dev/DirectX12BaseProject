#include "ConfirmPopup.h"

namespace Editor
{
	namespace
	{
		// ボタンの幅(選択肢と Cancel を同じ幅で並べる)
		const Math::Vector2 BUTTON_SIZE = { 120.0f, 0.0f };
	}

	ConfirmPopup::ConfirmPopup(const std::string& a_name)
		: m_popup(a_name)
	{
		m_popup.SetContent([this]() { DrawContent(); });
	}

	void ConfirmPopup::SetMessage(const std::string& a_message, const std::string& a_detail)
	{
		m_message = a_message;
		m_detail = a_detail;
	}

	void ConfirmPopup::AddChoice(const std::string& a_label, const std::function<void()>& a_func, EConfirmStyle a_style)
	{
		m_choiceVec.push_back({ a_label, a_func, a_style });
	}

	void ConfirmPopup::Draw()
	{
		m_popup.Draw();
	}

	void ConfirmPopup::DrawContent()
	{
		Engine::EditorField::Text("%s", m_message.c_str());
		if (!m_detail.empty()) Engine::EditorField::HelpText("%s", m_detail.c_str());

		Engine::EditorField::Line();

		for (const Choice& _choice : m_choiceVec)
		{
			const bool _isPressed = (_choice.style == EConfirmStyle::Danger)
				? Engine::EditorField::DeleteButton(_choice.label.c_str(), BUTTON_SIZE)
				: Engine::EditorField::Button(_choice.label.c_str(), BUTTON_SIZE);

			if (_isPressed)
			{
				if (_choice.func) _choice.func();
				m_popup.Close();
			}
			Engine::EditorField::SameLine();
		}

		// うっかり開いたときにすぐ戻れるよう、既定のフォーカスは Cancel に置く
		if (Engine::EditorField::Button("Cancel", BUTTON_SIZE)) m_popup.Close();
		if (ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
	}
}
