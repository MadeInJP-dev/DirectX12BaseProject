#include "SceneAmbientPanel.h"

#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

//==========================================================================================
// SceneAmbientPanel
//
// 触るのは一番上のシーンの環境設定と音量と物理の設定(重力)。
// 重ねたシーン(ポーズ画面など)が環境設定を切っていると、実際に絵へ出ているのは
// その下のシーンのものなので、どのシーンのものが使われているかも出しておく。
//==========================================================================================
void Editor::SceneAmbientPanel::OnDrawImGui(EditorContext& a_editContext)
{
	auto* _pSceneManager = a_editContext.pServices ? a_editContext.pServices->pSceneManager : nullptr;

	Engine::Scene::BaseScene* _pScene = _pSceneManager ? _pSceneManager->RefCurrentTopScene() : nullptr;
	if (!_pScene || !a_editContext.pServices)
	{
		Engine::EditorField::HelpText("No scene");
		return;
	}
	auto& _sceneManager = *_pSceneManager;

	// シーンの名前 : GUID からファイルを引いて出す
	auto _getSceneName = [&a_editContext](const Engine::Scene::BaseScene* a_pScene) -> std::string
		{
			if (!a_editContext.pServices->pAssetDatabase) return a_pScene->GetGUID().String();

			const std::string _path = a_editContext.pServices->pAssetDatabase->GetFilePathFromGUID(a_pScene->GetGUID());
			return _path.empty() ? a_pScene->GetGUID().String() : Core::File::GetFileNameWithoutExtension(_path);
		};

	Engine::EditorField::Value("Scene", "%s", _getSceneName(_pScene).c_str());

	// 実際に使われているシーン
	const Engine::Scene::BaseScene* _pSource = _sceneManager.RefAmbientSourceScene();
	if (_pSource != _pScene)
	{
		if (_pSource)
		{
			Engine::EditorField::Value("In Use", "%s", _getSceneName(_pSource).c_str());
			Engine::EditorField::Tooltip("このシーンは環境設定を切っているので、下のシーンのものが使われています");
		}
		else
		{
			Engine::EditorField::WarningText("環境設定を使うシーンがありません(環境光・フォグ・空・平行光なし)");
		}
	}

	Engine::EditorField::HelpText("シーンと一緒に保存されます");
	Engine::EditorField::Line();

	_pScene->RefAmbient().DrawEdit(*a_editContext.pServices);

	//------------------------------------------------------------------
	// シーンの音量
	//
	// 環境設定と違い、重ねたシーンでも下のものは使われない。
	// 音はそれぞれ鳴らしたシーンの音量で鳴る
	//------------------------------------------------------------------
	Engine::EditorField::Line();
	if (Engine::EditorField::TreeScope _tree{ "Sound", true, true })
	{
		_pScene->RefAudio().DrawEdit();
	}

	//------------------------------------------------------------------
	// シーンの物理の設定(重力)
	//
	// 物理空間はシーンのワールドごとにあるので、重ねたシーンでも下のものは使われない
	//------------------------------------------------------------------
	Engine::EditorField::Line();
	if (Engine::EditorField::TreeScope _tree{ "Physics", true, true })
	{
		_pScene->RefPhysics().DrawEdit();
	}
}
