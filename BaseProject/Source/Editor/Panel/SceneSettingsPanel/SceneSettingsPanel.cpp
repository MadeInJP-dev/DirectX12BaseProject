#include "SceneSettingsPanel.h"

#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

namespace Editor
{
	void SceneSettingsPanel::OnDrawImGui(EditorContext& a_editContext)
	{
		auto* _pSceneManager = a_editContext.pServices ? a_editContext.pServices->pSceneManager : nullptr;

		Engine::Scene::BaseScene* _pScene = _pSceneManager ? _pSceneManager->RefCurrentTopScene() : nullptr;
		if (!_pScene)
		{
			Engine::EditorField::HelpText("No scene");
			return;
		}

		Engine::EditorField::Value("Scene", "%s", GetSceneName(a_editContext, _pScene).c_str());
		Engine::EditorField::HelpText("シーンを保存すると一緒に保存されます");
		Engine::EditorField::Line();

		if (!ImGui::BeginTabBar("SceneSettingsTabs")) return;

		if (ImGui::BeginTabItem("Ambient"))
		{
			DrawAmbientTab(a_editContext, _pScene);
			ImGui::EndTabItem();
		}

		//------------------------------------------------------------------
		// シーンの音量
		//
		// 環境設定と違い、重ねたシーンでも下のものは使われない。
		// 音はそれぞれ鳴らしたシーンの音量で鳴る
		//------------------------------------------------------------------
		if (ImGui::BeginTabItem("Sound"))
		{
			_pScene->RefAudio().DrawEdit();
			ImGui::EndTabItem();
		}

		//------------------------------------------------------------------
		// シーンの物理の設定(重力)
		//
		// 物理空間はシーンのワールドごとにあるので、重ねたシーンでも下のものは使われない
		//------------------------------------------------------------------
		if (ImGui::BeginTabItem("Physics"))
		{
			_pScene->RefPhysics().DrawEdit();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Preload"))
		{
			DrawPreloadTab(a_editContext, _pScene);
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	std::string SceneSettingsPanel::GetSceneName(const EditorContext& a_editContext, const Engine::Scene::BaseScene* a_pScene) const
	{
		if (!a_editContext.pServices->pAssetDatabase) return a_pScene->GetGUID().String();

		const std::string _path = a_editContext.pServices->pAssetDatabase->GetFilePathFromGUID(a_pScene->GetGUID());
		return _path.empty() ? a_pScene->GetGUID().String() : Core::File::GetFileNameWithoutExtension(_path);
	}

	//======================================================================================
	// 環境設定
	//
	// 重ねたシーン(ポーズ画面など)が環境設定を切っていると、実際に絵へ出ているのは
	// その下のシーンのものなので、どのシーンのものが使われているかも出しておく。
	//======================================================================================
	void SceneSettingsPanel::DrawAmbientTab(EditorContext& a_editContext, Engine::Scene::BaseScene* a_pScene)
	{
		const Engine::Scene::BaseScene* _pSource = a_editContext.pServices->pSceneManager->RefAmbientSourceScene();
		if (_pSource != a_pScene)
		{
			if (_pSource)
			{
				Engine::EditorField::Value("In Use", "%s", GetSceneName(a_editContext, _pSource).c_str());
				Engine::EditorField::Tooltip("このシーンは環境設定を切っているので、下のシーンのものが使われています");
			}
			else
			{
				Engine::EditorField::WarningText("環境設定を使うシーンがありません(環境光・フォグ・空・平行光なし)");
			}
			Engine::EditorField::Line();
		}

		a_pScene->RefAmbient().DrawEdit(*a_editContext.pServices);
	}

	//======================================================================================
	// 先読みの設定
	//
	// 計測は開くときに始まるので、フラグを立てただけでは今開いているシーンは計測されない。
	// 計測中かどうかは SceneManager が知っているので、そちらに聞いて出す。
	//======================================================================================
	void SceneSettingsPanel::DrawPreloadTab(EditorContext& a_editContext, Engine::Scene::BaseScene* a_pScene)
	{
		const bool _isRecording = (a_editContext.pServices->pSceneManager->GetPreLoadRecordingScene() == a_pScene);
		a_pScene->RefConfig().DrawEdit(*a_editContext.pServices, _isRecording);
	}
}
