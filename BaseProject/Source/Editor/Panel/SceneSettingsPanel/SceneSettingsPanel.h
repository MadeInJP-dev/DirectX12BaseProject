#pragma once
#include "../IPanel.h"
#include "Editor/EditorCommon.h"

namespace Engine::Scene
{
	class BaseScene;
}

namespace Editor
{
	//======================================================================================
	// SceneSettingsPanel
	//
	// 今のシーン(一番上)の持ち物をまとめて触るパネル。どれもシーンと一緒に保存する。
	//
	//   Ambient : 環境設定(Engine::Scene::SceneAmbient)。環境光・平行光・影・フォグ・
	//             ボリュメトリックフォグ・空。パスやオブジェクトではなくここから編集する
	//   Sound   : シーンの音量(Engine::Scene::SceneAudio)
	//   Physics : 物理の設定(Engine::Scene::ScenePhysics。重力)
	//   Preload : 設定(Engine::Scene::SceneConfig)。先読み一覧の確認・手直しと、
	//             次に開いたときに一覧を計測し直すフラグ(ファイルはシーンと別)
	//
	// 以前は環境設定と設定が別のパネルで、どちらも「どのシーンか」を出すだけの
	// 同じ頭を持っていたので1つにした
	//======================================================================================
	class SceneSettingsPanel : public IPanel
	{
	public:
		~SceneSettingsPanel() override = default;

		const char* GetName() const override { return "Scene Settings"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::RightBottom; }
		void OnDrawImGui(EditorContext& a_editContext) override;

	private:

		// シーンの名前 : GUID からファイルを引いて出す(ファイルが無ければ GUID)
		std::string GetSceneName(const EditorContext& a_editContext, const Engine::Scene::BaseScene* a_pScene) const;

		// タブの中身
		void DrawAmbientTab(EditorContext& a_editContext, Engine::Scene::BaseScene* a_pScene);
		void DrawPreloadTab(EditorContext& a_editContext, Engine::Scene::BaseScene* a_pScene);
	};
}
