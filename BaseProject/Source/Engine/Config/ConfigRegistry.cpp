#include "ConfigRegistry.h"

#pragma warning(push, 0)
#include <nlohmannJSON/json.hpp>
#pragma warning(pop)

namespace Engine::Config
{
	const char* GetConfigCategoryName(EConfigCategory a_category)
	{
		switch (a_category)
		{
		case EConfigCategory::Project:	return "Project";
		case EConfigCategory::Window:	return "Window";
		case EConfigCategory::Graphics:	return "Graphics";
		case EConfigCategory::Input:	return "Input";
		case EConfigCategory::Game:		return "Game";
		case EConfigCategory::Debug:	return "Debug";
		default:						return "Unknown";
		}
	}

	ConfigRegistry::ConfigRegistry()
		: m_upSavedJson(std::make_unique<nlohmann::json>(nlohmann::json::object()))
	{}

	ConfigRegistry::~ConfigRegistry() = default;

	void ConfigRegistry::Load(const std::string& a_filePath)
	{
		m_filePath = a_filePath;

		std::ifstream _ifs(m_filePath);
		if (!_ifs.is_open())
		{
			ENGINE_WARNING("[Config] %s が無いので既定値で始める", m_filePath.c_str());
			*m_upSavedJson = nlohmann::json::object();
			return;
		}

		// 壊れていても止めない(既定値で起動し、保存で直る)
		*m_upSavedJson = nlohmann::json::parse(_ifs, nullptr, false);
		if (m_upSavedJson->is_discarded() || !m_upSavedJson->is_object())
		{
			ENGINE_WARNING("[Config] %s を読めなかったので既定値で始める", m_filePath.c_str());
			*m_upSavedJson = nlohmann::json::object();
		}
	}

	void ConfigRegistry::Unregister(const std::string& a_name)
	{
		auto _itr = std::find_if(m_sectionVec.begin(), m_sectionVec.end(),
			[&a_name](const ConfigSection& a_section) { return a_section.name == a_name; });
		if (_itr == m_sectionVec.end()) return;

		// 持ち主はこの後に壊れるので、値はここで覚えておく
		StoreSection(*_itr, *m_upSavedJson);
		m_sectionVec.erase(_itr);
	}

	void ConfigRegistry::Save()
	{
		if (m_filePath.empty()) return;

		for (const auto& _section : m_sectionVec)
		{
			StoreSection(_section, *m_upSavedJson);
		}

		std::ofstream _ofs(m_filePath);
		if (!_ofs.is_open())
		{
			ENGINE_WARNING("[Config] %s へ書けなかった", m_filePath.c_str());
			return;
		}
		_ofs << m_upSavedJson->dump(4);
	}

	void ConfigRegistry::Reload()
	{
		Load(m_filePath);
		for (const auto& _section : m_sectionVec)
		{
			LoadSection(_section);
		}
	}

	bool ConfigRegistry::IsDirty() const
	{
		// 今の値を書き出してみて、最後に読んだ・書いた中身と比べる
		nlohmann::json _current = *m_upSavedJson;
		for (const auto& _section : m_sectionVec)
		{
			StoreSection(_section, _current);
		}
		return _current != *m_upSavedJson;
	}

	void ConfigRegistry::RegisterSection(ConfigSection&& a_section)
	{
		// 同じ名前を2回登録するのは持ち主の作り直し。古い方は捨てる
		std::erase_if(m_sectionVec, [&a_section](const ConfigSection& a_other) { return a_other.name == a_section.name; });

		LoadSection(a_section);

		// 設定画面では分類の順に並べる(同じ分類の中は登録順)
		auto _itr = std::upper_bound(m_sectionVec.begin(), m_sectionVec.end(), a_section.category,
			[](EConfigCategory a_category, const ConfigSection& a_other) { return a_category < a_other.category; });
		m_sectionVec.insert(_itr, std::move(a_section));
	}

	void ConfigRegistry::LoadSection(const ConfigSection& a_section) const
	{
		// 無いグループは読まない : 設定は既定値のまま
		if (!m_upSavedJson->contains(a_section.name)) return;

		Persistence::Archive _ar(Persistence::Archive::EMode::Load, *m_upSavedJson);
		if (_ar.BeginGroup(a_section.name))
		{
			a_section.archiveFunc(_ar);
			_ar.EndGroup();
		}
	}

	void ConfigRegistry::StoreSection(const ConfigSection& a_section, nlohmann::json& a_json)
	{
		nlohmann::json _written = nlohmann::json::object();
		{
			Persistence::Archive _ar(Persistence::Archive::EMode::Save, _written);
			if (_ar.BeginGroup(a_section.name))
			{
				a_section.archiveFunc(_ar);
				_ar.EndGroup();
			}
		}
		a_json[a_section.name] = std::move(_written[a_section.name]);
	}
}
