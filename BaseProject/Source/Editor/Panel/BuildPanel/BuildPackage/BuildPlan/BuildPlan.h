#pragma once

#include "Editor/EditorCommon.h"

namespace Editor::Build
{
	//==========================================================================================
	// 書き出しの1件 : ファイルのコピーか、作った中身の書き込み
	//==========================================================================================
	struct BuildPlanEntry
	{
		std::filesystem::path srcPath = "";		// コピー元。空なら text を書く
		std::filesystem::path dstPath = "";		// 書き出し先
		std::string text = "";					// 書き込む中身(書き換えた .assetmeta や設定ファイル)
		uintmax_t byteSize = 0;					// 進み具合の表示用
	};

	//==========================================================================================
	// 提出用パッケージの書き出し予定
	//
	// 何をどこへ置くかを先に全部決めて(Add〜)、まとめて実行する(Execute)。
	// 決めるのはメインスレッド(アセットデータベースを引くため)、
	// 実行はファイルを写すだけなのでワーカースレッドで回す(数GBあるとエディターが止まるため)。
	// 決める途中で見つけた注意(未変換で入れられないアセットなど)もここへ溜める。
	//==========================================================================================
	class BuildPlan
	{
	public:

		//-------------------------------------------------------------------------------------------
		// 予定を積む : 同じ書き出し先は1回だけ(後から積んだものは無視)
		//-------------------------------------------------------------------------------------------

		// ファイルを1つ写す。コピー元が無ければ積まずに false
		bool AddCopyFile(const std::filesystem::path& a_src, const std::filesystem::path& a_dst);

		// フォルダの中を写す。コピー元が無ければ積まずに false
		//   a_extensionVec      : 写す拡張子("." 付き・大文字小文字は無視)。空なら全部
		//   a_excludeDirNameVec : この名前のフォルダは中ごと飛ばす
		bool AddCopyDirectory(
			const std::filesystem::path& a_src,
			const std::filesystem::path& a_dst,
			const std::vector<std::string>& a_extensionVec = {},
			const std::vector<std::string>& a_excludeDirNameVec = {}
		);

		// 作った中身をファイルとして書く
		void AddWriteText(const std::filesystem::path& a_dst, std::string a_text);

		// 注意を残す(UTF-8)
		void AddWarning(std::string a_message);

		//-------------------------------------------------------------------------------------------
		// 実行 : ワーカースレッドから呼ぶ
		//-------------------------------------------------------------------------------------------

		// 積んだものを全部書き出す。1件ずつ a_doneCount / a_doneBytes を進め、a_isCancel が立ったら抜ける
		// 失敗はそこで止めずに a_outErrorVec へ溜めて続ける。全部書けたら true
		bool Execute(
			std::atomic<size_t>& a_doneCount,
			std::atomic<uintmax_t>& a_doneBytes,
			const std::atomic_bool& a_isCancel,
			std::vector<std::string>& a_outErrorVec
		) const;

		//-------------------------------------------------------------------------------------------
		// 中身
		//-------------------------------------------------------------------------------------------

		const std::vector<BuildPlanEntry>& GetEntryVec() const { return m_entryVec; }
		const std::vector<std::string>& GetWarningVec() const { return m_warningVec; }
		uintmax_t GetTotalBytes() const { return m_totalBytes; }

	private:

		// 書き出し先がまだ積まれていなければ true(積んだ印も付ける)
		bool ReserveDestination(const std::filesystem::path& a_dst);

	private:

		std::vector<BuildPlanEntry> m_entryVec = {};
		std::vector<std::string> m_warningVec = {};
		uintmax_t m_totalBytes = 0;

		// 積んだ書き出し先(小文字にした文字列) : Windows は大文字小文字を区別しないので
		std::unordered_set<std::wstring> m_destinationSet = {};
	};
}
