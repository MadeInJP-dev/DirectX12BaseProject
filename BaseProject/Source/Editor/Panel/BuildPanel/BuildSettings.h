#pragma once

#include "Editor/EditorCommon.h"

namespace Editor::Build
{
	//==========================================================================================
	// 提出用パッケージの書き出し設定
	//
	// 既定値は見本(<名前>/ソース・実行・Library)の形。提出の形が変わったらパネルから直す。
	// 文字列はすべて UTF-8(ImGui と JSON のまま)。パスにするときは Core::File::FromUTF8 を通す。
	// 相対パスは作業ディレクトリ(BaseProject/)から。
	//==========================================================================================
	struct BuildSettings
	{
		//-------------------------------------------------------------------------------------------
		// 書き出し先 : <outputDirectory>/<packageName>/ 以下に作る
		//-------------------------------------------------------------------------------------------

		std::string outputDirectory = "";
		std::string packageName = "名前";

		// 作るもの
		bool isWriteSource = true;			// ソースフォルダ
		bool isWriteLibrary = true;			// ライブラリ(ソースからビルドするのに要る)
		bool isWriteExecute = true;			// 実行フォルダ

		// パッケージの中のフォルダ名
		std::string sourceDirectoryName = "ソース";
		std::string executeDirectoryName = "実行";
		std::string libraryDirectoryName = "Library";	// vcxproj は ..\Library を見るので、ソースと同じ階層に置く

		//-------------------------------------------------------------------------------------------
		// ソースフォルダ
		//-------------------------------------------------------------------------------------------

		std::string sourceDirectory = "Source";
		// ソースフォルダの直下へ置くファイル
		std::vector<std::string> projectFileVec = { "BaseProject.sln", "BaseProject.vcxproj", "BaseProject.vcxproj.filters", ".editorconfig" };
		// シェーダーのソース : vcxproj が FxCompile で持っているので、ソースフォルダにも要る
		std::string shaderDirectory = "Asset/Shader";
		std::vector<std::string> shaderSourceExtensionVec = { ".hlsl", ".hlsli" };

		// ライブラリ : フォルダ名がこの中にあるものは入れない
		std::string libraryDirectory = "../Library";
		std::vector<std::string> libraryExcludeDirectoryVec = { "_Backup" };

		//-------------------------------------------------------------------------------------------
		// 実行フォルダ
		//-------------------------------------------------------------------------------------------

		std::string exePath = "x64/Release/BaseProject.exe";
		std::string exeName = "";				// 置くときの名前。空なら元の名前
		bool isCopyDll = true;					// exe と同じフォルダの .dll も置く
		std::vector<std::string> extraFileVec = {};	// 実行フォルダへそのまま置くもの(説明書.pdf など)

		// 実行フォルダの EngineData に書くビルドモード(EBuildConfiguration の値)。-1 ならそのまま
		int executeBuildMode = static_cast<int>(EBuildConfiguration::Shipping);

		//-------------------------------------------------------------------------------------------
		// 実行フォルダのアセット : バイナリ(.ob* / .dds / .cso など)と .assetmeta だけを置く
		//-------------------------------------------------------------------------------------------

		bool isConvertBeforeBuild = true;		// 未変換・古いバイナリを書き出しの前にコンバートする

		// 入れるアセットの集め方 : ここを起点に、中に書いてある GUID とパスを辿る
		bool isIncludeRecordedAssets = true;	// このセッションで読み込まれたもの(SceneManager の記録)
		bool isScanSourceReferences = true;		// ソースコードに直書きされた GUID とパス
		bool isIncludeAllScenes = false;		// シーンを全部入れる(どこからも辿れないシーンも入る)
		std::vector<std::string> alwaysIncludeDirectoryVec = { "Asset/Shader", "Asset/RenderingPipeline" };	// 中のアセットを全部入れる
		std::vector<std::string> excludeDirectoryVec = {};
		std::vector<std::string> excludeTypeVec = { "ObjectPrefab" };	// エディターで置くときにしか読まない

		// 起動時に読む設定(アセットデータベースの外)。中の .ob* を置く
		std::vector<std::string> dataDirectoryVec = { "Asset/Data/Engine", "Asset/Data/Game", "Asset/Data/User" };

		// JSON 固定で読むもの : バイナリが無いので .oj* のまま置く
		// (.ojoptn は OptionManager、.ojrpipe は RenderingPipelineAssetIO が JSON で読む)
		std::vector<std::string> jsonKeepExtensionVec = { ".ojoptn", ".ojrpipe" };
	};

	/// 書き出し設定を読み込む。ファイルが無ければ既定値のまま
	///
	/// @param a_outSettings 読み込み先。書いていない項目は既定値が残る
	/// @return 読めたら true
	bool LoadBuildSettings(BuildSettings& a_outSettings);

	/// 書き出し設定を保存する
	///
	/// @return 書けたら true
	bool SaveBuildSettings(const BuildSettings& a_settings);
}
