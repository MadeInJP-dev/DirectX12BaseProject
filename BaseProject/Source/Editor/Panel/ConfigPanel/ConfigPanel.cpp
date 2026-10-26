#include "ConfigPanel.h"

//==========================================================================================
// ConfigPanel
//
// プロジェクト全体に効く設定を並べるだけのパネル。値の持ち主はそれぞれのシステムで、
// 何がどこにあるかは ConfigRegistry へ登録されたものから引く(ここに設定の種類を書かない)。
//
// 環境光・平行光・フォグ・空は「シーンごとに変わるもの」なので、ここではなく
// シーン(Engine::Scene::SceneAmbient)が持つ。編集は SceneSettingsPanel から。
//==========================================================================================
namespace Editor
{
	namespace
	{
		constexpr float CATEGORY_LIST_WIDTH = 120.0f;

		// 大文字小文字を区別せずに含むか
		bool ContainsIgnoreCase(const std::string& a_text, const std::string& a_word)
		{
			if (a_word.empty()) return true;
			auto _itr = std::search(a_text.begin(), a_text.end(), a_word.begin(), a_word.end(),
				[](char a_l, char a_r) { return std::tolower(static_cast<unsigned char>(a_l)) == std::tolower(static_cast<unsigned char>(a_r)); });
			return _itr != a_text.end();
		}
	}

	void ConfigPanel::OnDrawImGui(EditorContext& a_editContext)
	{
		auto* _pServices = a_editContext.pServices;
		auto* _pRegistry = _pServices ? _pServices->pConfigRegistry : nullptr;
		if (!_pRegistry)
		{
			Engine::EditorField::HelpText("No config");
			return;
		}

		DrawToolbar(*_pRegistry);
		ImGui::Separator();

		// 左 : 分類 / 右 : 設定
		if (ImGui::BeginChild("##ConfigCategory", ImVec2(CATEGORY_LIST_WIDTH, 0.0f), ImGuiChildFlags_Borders))
		{
			DrawCategoryList(*_pRegistry);
		}
		ImGui::EndChild();

		ImGui::SameLine();

		if (ImGui::BeginChild("##ConfigSections", ImVec2(0.0f, 0.0f)))
		{
			DrawSections(*_pRegistry, *_pServices);
		}
		ImGui::EndChild();
	}

	void ConfigPanel::DrawToolbar(Engine::Config::ConfigRegistry& a_registry)
	{
		const bool _isDirty = a_registry.IsDirty();

		// 終了時にも保存されるが、落ちたときに失わないよう手でも保存できるようにしておく
		ImGui::BeginDisabled(!_isDirty);
		if (ImGui::Button("保存")) a_registry.Save();
		ImGui::SameLine();
		if (ImGui::Button("元に戻す")) a_registry.Reload();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("保存してある値へ戻す");
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (_isDirty)
		{
			Engine::EditorField::WarningText("未保存の変更あり(終了時にも保存されます)");
		}
		else
		{
			Engine::EditorField::HelpText("保存済み");
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##ConfigFilter", "設定の名前・持ち主で絞り込み", &m_filter);
	}

	void ConfigPanel::DrawCategoryList(const Engine::Config::ConfigRegistry& a_registry)
	{
		if (ImGui::Selectable("All", !m_opCategory.has_value())) m_opCategory = std::nullopt;

		// 登録のある分類だけを出す
		for (size_t _index = 0; _index < static_cast<size_t>(Engine::Config::EConfigCategory::Count); ++_index)
		{
			const auto _category = static_cast<Engine::Config::EConfigCategory>(_index);
			const auto& _sections = a_registry.GetSections();
			const bool _hasSection = std::any_of(_sections.begin(), _sections.end(),
				[_category](const Engine::Config::ConfigSection& a_section) { return a_section.category == _category; });
			if (!_hasSection) continue;

			const bool _isSelected = (m_opCategory == _category);
			if (ImGui::Selectable(Engine::Config::GetConfigCategoryName(_category), _isSelected)) m_opCategory = _category;
		}
	}

	void ConfigPanel::DrawSections(const Engine::Config::ConfigRegistry& a_registry, const Engine::ECS::EngineServices& a_services)
	{
		bool _isAnyShown = false;
		std::optional<Engine::Config::EConfigCategory> _opPrevCategory = std::nullopt;

		for (const auto& _section : a_registry.GetSections())
		{
			if (!IsShown(_section)) continue;
			_isAnyShown = true;

			// 全部を出しているときは分類の切れ目に見出しを入れる(登録は分類の順に並んでいる)
			if (!m_opCategory && _opPrevCategory != _section.category)
			{
				ImGui::SeparatorText(Engine::Config::GetConfigCategoryName(_section.category));
				_opPrevCategory = _section.category;
			}

			ImGui::PushID(_section.name.c_str());
			if (ImGui::CollapsingHeader(_section.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				Engine::EditorField::HelpText("持ち主 : %s", _section.owner.c_str());
				ImGui::Indent();
				_section.drawEditFunc(a_services);
				ImGui::Unindent();
				ImGui::Spacing();
			}
			ImGui::PopID();
		}

		if (!_isAnyShown) Engine::EditorField::HelpText("該当する設定はありません");
	}

	bool ConfigPanel::IsShown(const Engine::Config::ConfigSection& a_section) const
	{
		if (m_opCategory && a_section.category != *m_opCategory) return false;
		return ContainsIgnoreCase(a_section.name, m_filter) || ContainsIgnoreCase(a_section.owner, m_filter);
	}
}
