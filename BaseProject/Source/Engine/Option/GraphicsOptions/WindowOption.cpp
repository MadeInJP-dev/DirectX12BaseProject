#include "WindowOption.h"

#include "../../MainEngine.h"
#include "../../Window/NativeWindow.h"

void Engine::Option::GraphicsOptions::WindowOption::DrawEdit(const ECS::EngineServices& a_services)
{
	// ウィンドウの持ち主はエンジン。書き換えた値はその場でウィンドウへ送る
	auto* _pWindow = a_services.pMainEngine ? a_services.pMainEngine->RefNativeWindow() : nullptr;

	// ウィンドウサイズ
	Engine::EditorField::Header("WindowSize");
	Engine::EditorField::Value("Width", "%f", windowWidth);
	Engine::EditorField::Value("Height", "%f", windowHeight);
	Engine::EditorField::Field("Width", windowWidth, 1, 0, 1980);
	Engine::EditorField::Field("Height", windowHeight, 1, 0, 1080);

	Engine::EditorField::Line();

	// ウィンドウタイトル
	if (Engine::EditorField::Field("Title", windowTitle))
	{
		// ウィンドウがない状況はあり得ないが一応
		if (_pWindow)
		{
			_pWindow->ChangeTitle(windowTitle);
		}
	}
	if (Engine::EditorField::Field("IsTitleFPS", isTitleFPS))
	{
		// FPS表示を消すため
		if (_pWindow)
		{
			_pWindow->ChangeTitle(windowTitle);
		}
	}

	Engine::EditorField::Line();

	// ウィンドウモード
	if (Engine::EditorField::Field("WindowMode", windowMode))
	{
		// ウィンドウがない状況はあり得ないが一応
		if (_pWindow)
		{
			_pWindow->ChangeWindowMode(windowMode);
		}
	}
	Engine::EditorField::Field("FPSRate", fpsRate);

	Engine::EditorField::Line();
}

void Engine::Option::GraphicsOptions::WindowOption::Archive(Persistence::Archive& a_archive)
{
	a_archive.Field("WindowTitle", windowTitle);
	a_archive.Field("isTitleFPS",isTitleFPS);

	// ウィンドウサイズ
	a_archive.Field("windowWidth", windowWidth);
	a_archive.Field("windowHeight", windowHeight);

	// モード
	UINT _winMode = static_cast<UINT>(windowMode);
	a_archive.Field("windowMode", _winMode);
	windowMode = static_cast<EWindowMode>(_winMode);

	a_archive.Field("fpsRate", fpsRate);
}
