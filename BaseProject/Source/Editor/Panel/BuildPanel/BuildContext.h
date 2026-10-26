#pragma once

#include "Editor/EditorCommon.h"

namespace Engine::Resource
{
	class AssetDatabase;
}

namespace Editor::Build
{
	struct BuildSettings;

	//==========================================================================================
	// 1回の書き出しで使う情報
	//
	// 設定(BuildSettings)の文字列をパスへ直したものと、実行フォルダへ入れるアセット。
	// 作るのは BuildPanel、各フォルダの書き出し(SourceDirectoryWrite など)は読むだけ。
	// コピー元の相対パスは作業ディレクトリ(BaseProject/)から。
	//==========================================================================================
	struct BuildContext
	{
		const BuildSettings* pSettings = nullptr;
		const Engine::Resource::AssetDatabase* pAssetDatabase = nullptr;

		// コピー元
		std::filesystem::path srcSourceDirectory = "Source";		// ソース
		std::filesystem::path srcShaderDirectory = "Asset/Shader";	// シェーダーのソース
		std::filesystem::path srcLibraryDirectory = "../Library";	// ライブラリ
		std::filesystem::path srcExePath = "";						// 実行ファイル

		// 書き出し先
		std::filesystem::path packageDirectory = "";		// <出力先>/<名前>
		std::filesystem::path sourceDirectory = "";			// <名前>/ソース
		std::filesystem::path executeDirectory = "";		// <名前>/実行
		std::filesystem::path libraryDirectory = "";		// <名前>/Library

		// 実行フォルダへ入れるアセット(AssetCollector が集めたもの)
		std::vector<Core::GUID> assetGUIDVec = {};
	};
}
