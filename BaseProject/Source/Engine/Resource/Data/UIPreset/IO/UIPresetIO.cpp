#include "UIPresetIO.h"

#include "../../../Manager/AssetDatabase/AssetDatabase.h"

namespace Engine::Resource
{
	UIPreset UIPresetIO::LoadFromFile(const std::string& a_path)
	{
		auto _fileDir = Core::File::GetDirFromPath(a_path);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_path);

		Persistence::Archive _ar(Persistence::Archive::EMode::Load, _fileDir, _fileName, "uipreset");

		UIPreset _preset = {};
		_preset.Archive(_ar);
		return _preset;
	}

	Core::GUID UIPresetIO::Create(
		AssetDatabase& a_assetDB,
		const std::string& a_path,
		const std::string& a_name,
		const std::string& a_decorationJson)
	{
		const std::string _basePath = a_path.empty()
			? std::string(ASSET_DIR) + a_name
			: std::string(ASSET_DIR) + a_path + "/" + a_name;

		// すでにないかチェック
		const Core::GUID _checkGUID = a_assetDB.GetGUIDFromFilePath(_basePath);
		if (_checkGUID != Core::DEFAULT_GUID)
		{
			ENGINE_LOG("すでに作成済みのUIプリセットです : %s", _basePath.c_str());
			return _checkGUID;
		}

		// 先にメタファイルを作って GUID を決めてから書き出す
		// (後から監視が見つけたときは、このメタファイルをそのまま使う)
		const Core::GUID _guid = a_assetDB.AddMetaData(_basePath, "UIPreset");

		UIPreset _preset(a_name);
		_preset.decorationJson = a_decorationJson;
		_preset.Save(_basePath);

		return _guid;
	}
}
