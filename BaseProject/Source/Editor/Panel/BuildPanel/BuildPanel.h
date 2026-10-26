#pragma once

#include "../IPanel.h"
#include "Editor/EditorCommon.h"

#include "BuildSettings.h"
#include "BuildContext.h"
#include "AssetConvertView/AssetConvertView.h"

namespace Editor
{
	namespace Build
	{
		class BuildPlan;
	}

	//==========================================================================================
	// リリース用、提出用のビルドを書き出すためのクラス
	//
	// <出力先>/<名前>/ に提出用パッケージを作る(既定は見本と同じ形)。
	//   ソース/  : Source・プロジェクト一式・シェーダーのソース(SourceDirectoryWrite)
	//   Library/ : ライブラリ
	//   実行/    : exe・dll・使うアセットのバイナリと .assetmeta(ExecuteDirectoryWrite)
	// 何をどこへ置くかは BuildSettings で変えられ、Asset/Data/Editor/BuildSettings.ojbuild に残る。
	//
	// 流れ : 使うアセットを集める → 未変換をコンバート → 置くものを決める(メインスレッド)
	//        → ファイルを写す(ワーカースレッド。数GBあるので止めない)
	//==========================================================================================
	class BuildPanel : public IPanel
	{
	public:

		BuildPanel();
		~BuildPanel() override;

		const char* GetName() const override { return "Build"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Bottom; }
		void OnDrawImGui(EditorContext& a_editContext) override;

	private:

		//-------------------------------------------------------------------------------------------
		// ビルド設定準備
		//-------------------------------------------------------------------------------------------

		// 親ディレクトリ選択
		void SelectBuildDirectory();

		// 書き出し設定の編集。変えたら true
		bool DrawSettings(EditorContext& a_editContext);

		// 実行フォルダの EngineData に書く起動設定(プリセットと各項目)の編集。変えたら true
		bool DrawExecuteBuildConfig();

		//-------------------------------------------------------------------------------------------
		// ビルド書き出し : 選択された親ディレクトリ以下に作成する
		//-------------------------------------------------------------------------------------------

		// 設定から1回ぶんの情報を作る。書き出せない設定なら false にして a_outError に理由を入れる
		bool CreateContext(EditorContext& a_editContext, Build::BuildContext& a_outContext, std::string& a_outError) const;

		// 使うアセットを集め、置くものを決める。a_isConvert なら未変換・古いものを途中でコンバートする
		void PreparePlan(EditorContext& a_editContext, bool a_isConvert, Build::BuildContext& a_context, Build::BuildPlan& a_outPlan);

		// 置くものを決めるだけ(書き出さない・コンバートしない)
		void PreviewPackage(EditorContext& a_editContext);

		// 提出用パッケージを生成 : 書き出し先が空でなければ、先に確認を出す
		void RequestBuildPackage(EditorContext& a_editContext);

		// 書き出しを始める。a_isClean なら書き出し先を消してから
		bool BuildPackage(EditorContext& a_editContext, bool a_isClean);

		// 書き出し中の表示と、終わったときの片付け
		void UpdateBuildThread();
		void DrawBuildState();

		// 結果(注意・失敗・入れるアセット)の表示
		void DrawReport(EditorContext& a_editContext);

	private:

		//-------------------------------------------------------------------------------------------
		// 設定
		//-------------------------------------------------------------------------------------------

		Build::BuildSettings m_settings = {};
		bool m_isSettingsDirty = false;			// 変えたが保存していない(入力中は保存しない)

		//-------------------------------------------------------------------------------------------
		// 確認・書き出しの結果
		//-------------------------------------------------------------------------------------------

		std::unique_ptr<Build::BuildPlan> m_upReportPlan;		// 最後に決めた予定(確認 / 書き出し)
		std::vector<Core::GUID> m_reportAssetVec = {};			// そのとき入れたアセット
		std::vector<std::string> m_buildErrorVec = {};			// 書き出しで失敗したもの(スレッドが書き、終わってから読む)
		std::string m_reportTitle = "";							// 結果の見出し
		std::filesystem::path m_reportPackageDirectory = "";	// 書き出し先(フォルダを開く用)

		//-------------------------------------------------------------------------------------------
		// 書き出しスレッド
		//-------------------------------------------------------------------------------------------

		std::thread m_buildThread;
		std::atomic_bool m_isBuilding = false;
		std::atomic_bool m_isCancelRequested = false;
		std::atomic<size_t> m_doneCount = 0;
		std::atomic<uintmax_t> m_doneBytes = 0;
		bool m_isBuildSucceeded = false;		// スレッドが書き、終わってから読む

		//-------------------------------------------------------------------------------------------
		// ポップアップ
		//-------------------------------------------------------------------------------------------

		// アセットコンバート用ポップアップと、その中身(一覧・まとめてのコンバート)
		Popup m_assetConvertPopup{"AssetConvertPanel"};
		Build::AssetConvertView m_assetConvertView = {};

		// 書き出し先が空でないときの確認。
		// 書き出しには毎フレームの EditorContext が要るので、ポップアップの中では答えを覚えるだけにして
		// 描き終わったあとに書き出す
		ConfirmPopup m_overwritePopup{"書き出し先の確認"};
		bool m_isOverwriteAnswered = false;		// 確認で「書き出す」と答えた
		bool m_isOverwriteClean = false;		// そのとき書き出し先を消してから書き出すか
	};
}
