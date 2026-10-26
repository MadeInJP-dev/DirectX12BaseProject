#pragma once
#include "Editor/EditorCommon.h"
#include "SceneFileMenu/SceneFileMenu.h"

namespace Editor
{
	class IPanel;

	//======================================================================================
	//
	// 画面のいちばん上のメニューバー
	//
	//   File       : シーンの新規・開く・保存(中身は SceneFileMenu)
	//   Window     : パネルの開閉と、配置を初めの形へ戻す Reset Layout
	//   Debug Play : エディターを出したままゲームを動かすモードの入り切り
	//   右端       : いま開いているシーンの名前
	//
	// 以前はメニューがシーンビューの中にしか無く、閉じたパネルを開き直す手段も無かった。
	// どのパネルにも属さない操作はここへ集める
	//
	//======================================================================================
	class MainMenuBar
	{
	public:

		MainMenuBar() = default;
		~MainMenuBar() = default;
		NON_COPYABLE_NON_MOVABLE(MainMenuBar);

		// a_pServices : アプリ寿命のサービス一式(借り物)
		void Init(ECS::EngineServices* a_pServices);

		/// <summary>
		/// メニューバーを描く(ドックの土台の窓の中から呼ぶ)
		/// </summary>
		/// <param name="a_panelVec">Window メニューに並べるパネル</param>
		/// <returns>Reset Layout が押されたら true</returns>
		bool Draw(std::vector<std::unique_ptr<IPanel>>& a_panelVec);

		// メニューから開くポップアップ(窓の外で、毎フレーム呼ぶ)
		void DrawPopups();

	private:

		// Window メニューの中身。Reset Layout が押されたら true
		bool DrawWindowMenu(std::vector<std::unique_ptr<IPanel>>& a_panelVec);

		// デバッグプレイの入り切り
		void DrawDebugPlayButton();

	private:

		SceneFileMenu m_sceneFileMenu;
	};
}
