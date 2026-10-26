#include "MainMenuBar.h"

#include "../IPanel.h"
#include "Engine/MainEngine.h"

namespace Editor
{
	void MainMenuBar::Init(ECS::EngineServices* a_pServices)
	{
		m_sceneFileMenu.Init(a_pServices);
	}

	bool MainMenuBar::Draw(std::vector<std::unique_ptr<IPanel>>& a_panelVec)
	{
		// 保存先の取り直しと Ctrl+S はメニューを開いていなくても回す
		m_sceneFileMenu.Update();

		if (!ImGui::BeginMenuBar()) return false;

		if (ImGui::BeginMenu("File"))
		{
			m_sceneFileMenu.DrawMenuItems();
			ImGui::EndMenu();
		}

		bool _isResetLayout = false;
		if (ImGui::BeginMenu("Window"))
		{
			_isResetLayout = DrawWindowMenu(a_panelVec);
			ImGui::EndMenu();
		}

		DrawDebugPlayButton();

		//------------------------------------------------------------------
		// 右端 : いま開いているシーン
		//
		// 保存(Ctrl+S)の行き先なので、どこにいても見えるところに出しておく
		//------------------------------------------------------------------
		const std::string _sceneName = m_sceneFileMenu.GetCurrentSceneName();
		const std::string _sceneLabel = "Scene : " + (_sceneName.empty() ? std::string("(unsaved)") : _sceneName);
		const float _labelWidth = ImGui::CalcTextSize(_sceneLabel.c_str()).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		const float _rightX = ImGui::GetWindowContentRegionMax().x - _labelWidth;
		if (_rightX > ImGui::GetCursorPosX())
		{
			ImGui::SetCursorPosX(_rightX);
			Engine::EditorField::HelpText("%s", _sceneLabel.c_str());
		}

		ImGui::EndMenuBar();
		return _isResetLayout;
	}

	void MainMenuBar::DrawPopups()
	{
		m_sceneFileMenu.DrawPopups();
	}

	//======================================================================================
	// Window メニュー
	//
	// 既定の置き場所ごとに区切って並べる(画面の左から右、上から下の順)
	//======================================================================================
	bool MainMenuBar::DrawWindowMenu(std::vector<std::unique_ptr<IPanel>>& a_panelVec)
	{
		constexpr EPanelDock DOCK_ORDER[] =
		{
			EPanelDock::Left, EPanelDock::Center, EPanelDock::Right, EPanelDock::RightBottom, EPanelDock::Bottom,
		};

		bool _isFirstGroup = true;
		for (const EPanelDock _dock : DOCK_ORDER)
		{
			bool _isGroupStarted = false;
			for (auto& _upPanel : a_panelVec)
			{
				if (_upPanel->GetDefaultDock() != _dock) continue;

				if (!_isGroupStarted && !_isFirstGroup) ImGui::Separator();
				_isGroupStarted = true;
				_isFirstGroup = false;

				ImGui::MenuItem(_upPanel->GetName(), nullptr, &_upPanel->RefIsOpen());
			}
		}

		ImGui::Separator();

		const bool _isReset = ImGui::MenuItem("Reset Layout");
		Engine::EditorField::Tooltip("全部のパネルを開き、初めの配置へ戻す");
		return _isReset;
	}

	//======================================================================================
	// デバッグプレイの入り切り
	//
	// エディターを出したままゲームを動かすモード。
	// インスペクターで値の動きを見ながら操作を確かめるためのもので、
	// 映るのはゲームのカメラ、入力もゲームへ渡る。
	//
	// 入っている間はマウスがゲーム側の持ち物になる(カーソルは画面中央へ
	// 固定され、エディターはマウスを受け取らない)ので、
	// 抜けるのは Ctrl+P。ボタンの文字にも出しておく。
	//
	// 色は付けない。このエディターでは色付きのボタンは
	// 「押すと増える(緑)/減る(赤)」を表す決まりなので、
	// 表示の切り替えに使うと意味が食い違う
	//======================================================================================
	void MainMenuBar::DrawDebugPlayButton()
	{
		auto& _engine = MainEngine::Instance();
		const bool _isDebugPlay = (_engine.GetMode() == EAppMode::DebugPlay);

		if (Engine::EditorField::Button(_isDebugPlay
			? "Debug Play : ON   (Ctrl+P to Editor)"
			: "Debug Play : OFF"))
		{
			_engine.ChangeMode(_isDebugPlay ? EAppMode::Editor : EAppMode::DebugPlay);
		}

		if (!_isDebugPlay)
		{
			Engine::EditorField::Tooltip(
				"エディターを出したままシーンビューで遊ぶ\n"
				"入るとマウスはゲーム側へ渡るので、抜けるときは Ctrl+P");
		}
	}
}
