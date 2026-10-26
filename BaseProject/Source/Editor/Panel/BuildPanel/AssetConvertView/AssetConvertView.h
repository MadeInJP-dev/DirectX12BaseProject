#pragma once

#include "Editor/EditorCommon.h"

namespace Editor
{
	struct EditorContext;
}

namespace Editor::Build
{
	//==========================================================================================
	// アセットコンバートのポップアップの中身
	//
	// 対象(このセッションで読み込んだもの / すべてのアセット)を一覧にし、
	// 1件ずつ、または未変換をまとめてコンバートする。
	//
	// コンバートは読み込みと保存を伴うのでメインスレッドでしか回せない。
	// まとめてのときは1フレームに時間の許す分だけ進め(Update)、進み具合を出す。
	// 一覧は開いたとき・対象を変えたとき・終わったときに作り直す(毎フレーム全件のファイルを見ない)
	//==========================================================================================
	class AssetConvertView
	{
	public:

		// 1件コンバートする : アセット種別ごとの処理は Converter/ 以下。成功したら true
		static bool ConvertAsset(EditorContext& a_editContext, const Core::GUID& a_guid);

		// まとめてのコンバートを進める。ポップアップを閉じていても毎フレーム呼ぶ
		void Update(EditorContext& a_editContext);

		// ポップアップの中身
		void Draw(EditorContext& a_editContext);

		// 次の Draw で一覧を作り直す(開いたときに呼ぶ)
		void RequestRefresh() { m_isTargetDirty = true; }

		// まとめてのコンバート中か : 書き出しと同時に回さない
		bool IsConverting() const { return m_isConverting; }

	private:

		// 対象
		enum class EScope
		{
			Recorded,		// このセッションで読み込んだもの
			All,			// アセットデータベースのすべて
		};

		// 一覧の1行
		struct Target
		{
			Core::GUID guid = {};
			std::string label = "";			// "[種別] パス"(UTF-8)
			bool hasConverter = false;		// 書き出し方の決まっている種別か
			bool hasFile = false;			// 元のファイルがあるか(古いメタファイルだけのものは変換できない)
			bool isConverted = false;
		};

	private:

		// 一覧を作り直す
		void RefreshTargets(EditorContext& a_editContext);

		// 未変換のものをまとめてのコンバートに積んで始める
		void StartConvertAll();

		// まとめてのコンバートの進み具合と結果
		void DrawProgress();

		// 一覧の表
		void DrawTargetTable(EditorContext& a_editContext);

		// 変換できるのにまだのものか
		static bool IsUnconverted(const Target& a_target)
		{
			return a_target.hasConverter && a_target.hasFile && !a_target.isConverted;
		}

	private:

		//-------------------------------------------------------------------------------------------
		// 一覧
		//-------------------------------------------------------------------------------------------

		EScope m_scope = EScope::Recorded;
		bool m_isShowUnconvertedOnly = false;
		bool m_isTargetDirty = true;
		std::vector<Target> m_targetVec = {};
		size_t m_unconvertedCount = 0;

		//-------------------------------------------------------------------------------------------
		// まとめてのコンバート : 一覧(m_targetVec)の添え字で持つので、回している間は一覧を作り直さない
		//-------------------------------------------------------------------------------------------

		bool m_isConverting = false;
		bool m_isCancelRequested = false;
		std::vector<size_t> m_queueVec = {};
		size_t m_queueIndex = 0;
		size_t m_successCount = 0;
		std::vector<std::string> m_failedLabelVec = {};
		std::string m_resultText = "";			// 終わったときの結果(次に始めるまで残す)
	};
}
