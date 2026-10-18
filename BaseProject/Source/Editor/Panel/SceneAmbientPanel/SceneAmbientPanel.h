#pragma once
#include "../IPanel.h"
#include "Editor/EditorCommon.h"

namespace Editor
{
	//======================================================================================
	// SceneAmbientPanel
	//
	// 今のシーン(一番上)の環境設定(Engine::Scene::SceneAmbient)を触るパネル。
	// 環境光・平行光・影・フォグ・ボリュメトリックフォグ・空はシーンの持ち物なので、
	// パスやオブジェクトではなくここから編集し、シーンと一緒に保存する。
	// シーンの音量(Engine::Scene::SceneAudio)も同じくシーンの持ち物なので、ここで触る。
	//======================================================================================
	class SceneAmbientPanel : public IPanel
	{
	public:
		~SceneAmbientPanel() override = default;

		const char* GetName() const override { return "SceneAmbientPanel"; };
		void OnDrawImGui(EditorContext& a_editContext) override;
	};
}
