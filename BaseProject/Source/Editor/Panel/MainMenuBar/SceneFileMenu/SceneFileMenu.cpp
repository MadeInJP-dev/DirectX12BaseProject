#include "SceneFileMenu.h"

#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

namespace Editor
{
	namespace
	{
		// シーンの置き場(<名前>/<名前>.ojscene)
		constexpr const char* SCENE_DIR = "Asset/Scenes/";
	}

	SceneFileMenu::SceneFileMenu()
		: m_openPopup("Open Scene")
		, m_saveAsPopup("Save Scene As", "File Name##SaveSceneName", "Save")
		, m_createPopup("Create New Scene", "File Name##NewSceneName", "Create")
	{
		m_openPopup.SetContent([this]() { DrawOpenSceneContent(); });

		m_saveAsPopup.SetOnConfirm([this](const std::string& a_name) { return SaveSceneAs(a_name); });
		m_saveAsPopup.SetHint([](const std::string& a_name)
			{
				return std::string(SCENE_DIR) + (a_name.empty() ? "<名前>" : a_name) + "/ へ今の中身を保存します";
			});

		m_createPopup.SetOnConfirm([this](const std::string& a_name) { return CreateScene(a_name); });
		m_createPopup.SetHint([](const std::string& a_name)
			{
				return std::string(SCENE_DIR) + (a_name.empty() ? "<名前>" : a_name) + "/ へ空のシーンを作って開きます";
			});
	}

	void SceneFileMenu::Init(ECS::EngineServices* a_pServices)
	{
		m_pServices = a_pServices;
	}

	void SceneFileMenu::Update()
	{
		if (!m_pServices) return;

		//------------------------------------------------------------------
		// 開いているシーンを実物から取り直す
		//
		// シーンはエディターからだけでなくゲーム側からも切り替わる
		// (タイトル→ホーム、ホーム→出撃先など)。以前は一度覚えたきり
		// だったので、ゲームが切り替えた後もエディターは前のシーンを指したまま
		// になり、Ctrl+S で「別のシーンのファイル」へ上書きしてしまっていた。
		//
		// 実際に積まれているシーンが自分のGUIDを持っているので、毎フレーム
		// そこから取り直す。これで表示も保存先も必ず今のシーンを指す。
		//------------------------------------------------------------------
		auto* _pScene = m_pServices->pSceneManager->RefCurrentTopScene();
		if (!_pScene) return;

		if (!(m_currentSceneGUID == _pScene->GetGUID()))
		{
			m_currentSceneGUID = _pScene->GetGUID();

			// 保存先のファイルがあるシーンだけ上書きさせる
			m_canOverwrite = m_currentSceneGUID.IsValid();

			ENGINE_LOG("シーンのGUIDがセットされました : %s", m_currentSceneGUID.String().c_str());
		}

		// Ctrl + S : 保存先があれば上書き、なければ名前を付けて保存
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			if (m_canOverwrite) SaveScene(m_currentSceneGUID);
			else m_saveAsPopup.Open();
		}
	}

	void SceneFileMenu::DrawMenuItems()
	{
		if (ImGui::MenuItem("New Scene...")) m_createPopup.Open();
		if (ImGui::MenuItem("Open Scene...")) m_openPopup.Open();

		ImGui::Separator();

		if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, m_canOverwrite)) SaveScene(m_currentSceneGUID);
		if (ImGui::MenuItem("Save Scene As...")) m_saveAsPopup.Open(GetCurrentSceneName());
	}

	void SceneFileMenu::DrawPopups()
	{
		m_openPopup.Draw();
		m_saveAsPopup.Draw();
		m_createPopup.Draw();
	}

	std::string SceneFileMenu::GetCurrentSceneName() const
	{
		if (!m_pServices || !m_currentSceneGUID.IsValid()) return "";
		return m_pServices->pAssetDatabase->GetFileNameFromGUID(m_currentSceneGUID);
	}

	//======================================================================================
	// 一覧から開く
	//======================================================================================
	void SceneFileMenu::DrawOpenSceneContent()
	{
		const auto _sceneSpan = m_pServices->pAssetDatabase->GetTypeMetaVec("Scene");

		if (const auto* _pSceneProp = EditorHelper::DrawAssetSelectList(_sceneSpan, m_currentSceneGUID, "##OpenSceneSearch"))
		{
			m_pServices->pSceneManager->ReserveChangeScene(_pSceneProp->guid, Scene::ESceneChangeType::Replace);
			ENGINE_LOG("シーンを読み込みました : %s", _pSceneProp->fileName.c_str());

			m_currentSceneGUID = _pSceneProp->guid;
			m_canOverwrite = true;
			m_openPopup.Close();
		}

		Engine::EditorField::Line();
		if (Engine::EditorField::Button("Close", Math::Vector2(120.0f, 0.0f))) m_openPopup.Close();
	}

	//======================================================================================
	// 名前を付けて保存
	//======================================================================================
	bool SceneFileMenu::SaveSceneAs(const std::string& a_name)
	{
		const std::string _dirPath = SCENE_DIR + a_name;
		const std::string _filePath = _dirPath + "/" + a_name;

		std::filesystem::create_directories(_dirPath);

		const Core::GUID _guid = m_pServices->pAssetDatabase->AddMetaData(_filePath, "Scene");
		m_currentSceneGUID = _guid;
		m_canOverwrite = true;

		// 名前を付けて保存した先が、このシーンの新しい置き場所になる。
		// シーン本体にも覚えさせておかないと、Update の取り直しで保存前のGUIDへ戻ってしまう
		if (auto* _pCurrentScene = m_pServices->pSceneManager->RefCurrentTopScene())
		{
			_pCurrentScene->SetGUID(_guid);
		}

		SaveScene(m_currentSceneGUID);
		return true;
	}

	//======================================================================================
	// 新規シーンの作成
	//--------------------------------------------------------------------------------------
	// 空のシーンをアセットとして書き出してから、そのまま開く。
	//
	// 「名前を付けて保存」は今開いている中身を別名で書き出すものなので、
	// 空から始めたいときには使えない(前のシーンの中身が付いてくる)。
	//======================================================================================
	bool SceneFileMenu::CreateScene(const std::string& a_name)
	{
		const Core::GUID _guid = m_pServices->pSceneManager->CreateEmptyScene(*m_pServices->pAssetDatabase, "", a_name);

		// 失敗(同名がある等)のときは閉じない。理由はログへ出ている
		if (!_guid.IsValid()) return false;

		// 作った先をこのまま開く。中身は空なので、上書き保存の行き先もここになる
		m_pServices->pSceneManager->ReserveChangeScene(_guid, Scene::ESceneChangeType::Replace);

		m_currentSceneGUID = _guid;
		m_canOverwrite = true;
		return true;
	}

	void SceneFileMenu::SaveScene(const Core::GUID& a_guid)
	{
		// 現在のシーンを取得
		auto* _pScene = m_pServices->pSceneManager->RefCurrentTopScene();
		if (!_pScene)
		{
			ENGINE_LOG("シーンのセーブに失敗しました");
			return;
		}

		// 読み込み途中のシーンは中身が揃っていない : 保存すると空の中身でファイルを上書きしてしまう
		if (!_pScene->IsReady())
		{
			ENGINE_WARNING("[Scene] 読み込み中のシーンは保存できません。読み込みが済んでから保存してください");
			return;
		}

		// ファイルパスを取得
		const auto _path = m_pServices->pAssetDatabase->GetFilePathFromGUID(a_guid);
		if (_path.empty())
		{
			ENGINE_LOG("シーンのセーブに失敗しました");
			return;
		}

		const auto _fileDir = Core::File::GetDirFromPath(_path);
		const auto _fileName = Core::File::GetFileNameWithoutExtension(_path);

		// どのシーンを保存するかをログ出力する
		ENGINE_LOG("[Scene] セーブ : %s", _path.c_str());

		{
			Persistence::Archive _ar(Persistence::Archive::EMode::Save, _fileDir, _fileName, "scene");
			_pScene->Archive(_ar);
		}

		// シーンの設定(先読み一覧・計測フラグ)は別のファイル。一緒に書いておく
		_pScene->RefConfig().SaveFile(_fileDir, _fileName);
	}
}
