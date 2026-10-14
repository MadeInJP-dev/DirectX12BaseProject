#include "UIMaterialIO.h"

#include "../../../Manager/AssetDatabase/AssetDatabase.h"

namespace Engine::Resource
{
	UIMaterial UIMaterialIO::LoadFromFile(const std::string& a_path, ResourceManager& a_resourceManager)
	{
		auto _fileDir = Core::File::GetDirFromPath(a_path);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_path);

		Persistence::Archive _ar(Persistence::Archive::EMode::Load, _fileDir, _fileName, "uimat");

		UIMaterial _material = {};
		_material.Archive(_ar);

		// GUID しか入っていないので、ノイズのテクスチャをここで引き当てる
		_material.ResolveReferences(a_resourceManager);

		return _material;
	}

	void UIMaterialIO::Create(AssetDatabase& a_assetDB, const std::string& a_path, const std::string& a_name)
	{
		// ディレクトリ
		static const std::string ASSET_DIR = "Asset/UIMaterial/";
		auto _basePath = ASSET_DIR + a_path + "/" + a_name;

		// すでにないかチェック
		Core::GUID _checkGUID = a_assetDB.GetGUIDFromFilePath(_basePath);
		if (_checkGUID != Core::DEFAULT_GUID)
		{
			ENGINE_LOG("すでに作成済みのUIマテリアルです : %s", _basePath.c_str());
			return;
		}

		// 書き出すだけでよい。
		// メタファイルとGUIDは、AssetDatabase の監視が新しいファイルを見つけて用意する
		UIMaterial _material(a_name);
		_material.Save(_basePath);
	}
}
