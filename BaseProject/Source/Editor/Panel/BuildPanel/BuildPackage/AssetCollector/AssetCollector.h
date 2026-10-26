#pragma once

#include "Editor/EditorCommon.h"

namespace Engine::Resource
{
	class AssetDatabase;
	struct AssetProperty;
}

namespace Editor::Build
{
	struct BuildContext;

	//==========================================================================================
	// 実行フォルダへ入れるアセットを集める
	//
	// Asset 全部は数GBあるので、使うものだけを選ぶ。起点(設定で選ぶ)から、
	// アセットの中に書いてある GUID とパス("Asset/〜")を辿って広げる。
	//
	// 起点
	//   ・起動時に読む設定(Asset/Data) : 最初のシーン・カーソルの絵など
	//   ・ソースコードに直書きされた GUID とパス
	//   ・このセッションで読み込まれたもの(SceneManager の記録)
	//   ・中身を全部入れるフォルダ(シェーダーなど、パスで直に読むもの)
	//
	// 中身の読み方
	//   ・JSON(.oj*)があればそれを文字列として読み、GUID の文字列とパスを拾う
	//   ・バイナリしか無ければ、GUID の16バイトがそのまま並んでいるので、全アセットの GUID と突き合わせる
	//==========================================================================================
	class AssetCollector
	{
	public:

		// 集める。返すのはパス順に並べた GUID
		std::vector<Core::GUID> Collect(const BuildContext& a_context, const std::vector<Core::GUID>& a_recordedGUIDVec);

	private:

		//-------------------------------------------------------------------------------------------
		// 起点
		//-------------------------------------------------------------------------------------------

		// 1件足す : 除外されていなければ辿る列に積む
		void Add(const Core::GUID& a_guid);

		// フォルダの中のアセットを全部足す
		void AddDirectory(const std::filesystem::path& a_dir);

		// 種別のアセットを全部足す
		void AddType(const std::string& a_type);

		//-------------------------------------------------------------------------------------------
		// 中身から参照を拾う
		//-------------------------------------------------------------------------------------------

		// アセットの中身を読む : JSON があればそちら、無ければバイナリ
		void ScanAsset(const Engine::Resource::AssetProperty& a_prop);

		// ファイルを読む : 拡張子が .oj* なら文字列、それ以外はバイナリとして
		void ScanFile(const std::filesystem::path& a_path);

		// 文字列の中の GUID("xxxxxxxx-xxxx-…")とパス("Asset/〜.拡張子")を拾う
		void ScanText(std::string_view a_text);

		// バイナリの中の GUID(16バイト)を拾う
		void ScanBinary(std::string_view a_data);

		// ファイルを丸ごと読む。大きすぎるもの(参照を持たない生データ)は読まずに false
		static bool ReadFile(const std::filesystem::path& a_path, std::string& a_outData);

		//-------------------------------------------------------------------------------------------
		// 判定
		//-------------------------------------------------------------------------------------------

		bool IsExcluded(const Engine::Resource::AssetProperty& a_prop) const;

		// a_filePath(アセットデータベースのパス)が a_dir の下にあるか。大文字小文字は無視
		static bool IsUnderDirectory(const std::string& a_filePath, const std::filesystem::path& a_dir);

	private:

		const BuildContext* m_pContext = nullptr;
		const Engine::Resource::AssetDatabase* m_pAssetDatabase = nullptr;

		std::unordered_set<Core::GUID> m_allGUIDSet = {};		// バイナリの突き合わせ用
		std::unordered_set<Core::GUID> m_addedGUIDSet = {};		// 足したもの(除外したものも入れて二度見ない)
		std::vector<Core::GUID> m_collectedGUIDVec = {};		// 入れるもの
		std::vector<Core::GUID> m_scanQueue = {};				// 中身をまだ読んでいないもの
	};
}
