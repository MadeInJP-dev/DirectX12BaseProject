#include "BuildSettings.h"

// 設定ファイルの読み書きでだけ JSON の実体を触る
#pragma warning(push, 0)
#include <nlohmannJSON/json.hpp>
#pragma warning(pop)

namespace Editor::Build
{
	namespace
	{
		// 置き場 : 起動時に読む設定と同じ Data の下(git で共有される)。
		// 実行フォルダへ入れる Data は BuildSettings::dataDirectoryVec だけなので、ここは入らない
		const std::filesystem::path SETTINGS_PATH = "Asset/Data/Editor/BuildSettings.ojbuild";

		// 書いてあれば読む。無い・型が違う項目は今の値(既定値)のまま
		template<typename T>
		void ReadValue(const nlohmann::json& a_json, const char* a_key, T& a_value)
		{
			const auto _it = a_json.find(a_key);
			if (_it == a_json.end()) return;

			try
			{
				a_value = _it->get<T>();
			}
			catch (const nlohmann::json::exception&)
			{
				ENGINE_WARNING("[Build] 設定の型が違うので既定値を使います : %s", a_key);
			}
		}

		//==================================================================================
		// 項目と保存キーの対応
		//
		// 読むのと書くのとで同じ並びを2回書かないよう、ここに1回だけ書く
		//==================================================================================
		template<typename TSettings, typename TFunc>
		void ForEachBuildSettingsField(TSettings& a_settings, TFunc&& a_func)
		{
			a_func("outputDirectory", a_settings.outputDirectory);
			a_func("packageName", a_settings.packageName);
			a_func("isWriteSource", a_settings.isWriteSource);
			a_func("isWriteLibrary", a_settings.isWriteLibrary);
			a_func("isWriteExecute", a_settings.isWriteExecute);
			a_func("sourceDirectoryName", a_settings.sourceDirectoryName);
			a_func("executeDirectoryName", a_settings.executeDirectoryName);
			a_func("libraryDirectoryName", a_settings.libraryDirectoryName);

			a_func("sourceDirectory", a_settings.sourceDirectory);
			a_func("projectFiles", a_settings.projectFileVec);
			a_func("shaderDirectory", a_settings.shaderDirectory);
			a_func("shaderSourceExtensions", a_settings.shaderSourceExtensionVec);
			a_func("libraryDirectory", a_settings.libraryDirectory);
			a_func("libraryExcludeDirectories", a_settings.libraryExcludeDirectoryVec);

			a_func("exePath", a_settings.exePath);
			a_func("exeName", a_settings.exeName);
			a_func("isCopyDll", a_settings.isCopyDll);
			a_func("extraFiles", a_settings.extraFileVec);
			a_func("executeBuildMode", a_settings.executeBuildMode);

			a_func("isConvertBeforeBuild", a_settings.isConvertBeforeBuild);
			a_func("isIncludeRecordedAssets", a_settings.isIncludeRecordedAssets);
			a_func("isScanSourceReferences", a_settings.isScanSourceReferences);
			a_func("isIncludeAllScenes", a_settings.isIncludeAllScenes);
			a_func("alwaysIncludeDirectories", a_settings.alwaysIncludeDirectoryVec);
			a_func("excludeDirectories", a_settings.excludeDirectoryVec);
			a_func("excludeTypes", a_settings.excludeTypeVec);
			a_func("dataDirectories", a_settings.dataDirectoryVec);
			a_func("jsonKeepExtensions", a_settings.jsonKeepExtensionVec);
		}
	}

	bool LoadBuildSettings(BuildSettings& a_outSettings)
	{
		std::ifstream _ifs(SETTINGS_PATH);
		if (!_ifs.is_open()) return false;

		nlohmann::json _json;
		try
		{
			_ifs >> _json;
		}
		catch (const nlohmann::json::parse_error&)
		{
			ENGINE_WARNING("[Build] 書き出し設定が読めませんでした : %s", SETTINGS_PATH.string().c_str());
			return false;
		}

		ForEachBuildSettingsField(a_outSettings,
			[&_json](const char* a_key, auto& a_value) { ReadValue(_json, a_key, a_value); });
		return true;
	}

	bool SaveBuildSettings(const BuildSettings& a_settings)
	{
		nlohmann::json _json = nlohmann::json::object();
		ForEachBuildSettingsField(a_settings,
			[&_json](const char* a_key, const auto& a_value) { _json[a_key] = a_value; });

		std::error_code _ec;
		std::filesystem::create_directories(SETTINGS_PATH.parent_path(), _ec);

		std::ofstream _ofs(SETTINGS_PATH);
		if (!_ofs.is_open())
		{
			ENGINE_WARNING("[Build] 書き出し設定を保存できませんでした : %s", SETTINGS_PATH.string().c_str());
			return false;
		}
		_ofs << _json.dump(4);
		return true;
	}
}
