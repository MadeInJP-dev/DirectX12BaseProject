#include "WindowConfig.h"

#include "NativeWindow.h"
#include "Engine/MainEngine.h"
#include "Engine/ECS/System/SystemContext.h"	// EngineServices
#include "Engine/EditorField/EditorField.h"

void Engine::Window::WindowConfig::DrawEdit(const ECS::EngineServices& a_services)
{
	// 書き換えた値はその場でウィンドウへ送る
	auto* _pWindow = a_services.pMainEngine ? a_services.pMainEngine->RefNativeWindow() : nullptr;

	// 大きさ
	Engine::EditorField::Header("Size");
	Engine::EditorField::Field("Width", windowWidth, 1, 0, 3840);
	Engine::EditorField::Field("Height", windowHeight, 1, 0, 2160);
	Engine::EditorField::HelpText("描画解像度も兼ねる(次の起動から)");

	// タイトル
	Engine::EditorField::Header("Title");
	if (Engine::EditorField::Field("Title", windowTitle))
	{
		if (_pWindow) _pWindow->ChangeTitle(windowTitle);
	}
	if (Engine::EditorField::Field("IsTitleFPS", isTitleFPS))
	{
		// 切ったときに FPS の表示を消すため
		if (_pWindow) _pWindow->ChangeTitle(windowTitle);
	}

	// モード
	Engine::EditorField::Header("Mode");
	if (Engine::EditorField::Field("WindowMode", windowMode))
	{
		if (_pWindow) _pWindow->ChangeWindowMode(windowMode);
	}
}

void Engine::Window::WindowConfig::Archive(Persistence::Archive& a_archive)
{
	a_archive.Field("windowTitle", windowTitle);
	a_archive.Field("isTitleFPS", isTitleFPS);

	// 大きさ
	a_archive.Field("windowWidth", windowWidth);
	a_archive.Field("windowHeight", windowHeight);

	// モード
	UINT _winMode = static_cast<UINT>(windowMode);
	a_archive.Field("windowMode", _winMode);
	windowMode = static_cast<EWindowMode>(_winMode);
}
