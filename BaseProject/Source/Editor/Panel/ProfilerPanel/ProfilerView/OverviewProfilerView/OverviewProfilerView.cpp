#include "OverviewProfilerView.h"

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Device/GraphicsDevice/GraphicsDevice.h"
#include "Engine/Window/NativeWindow.h"

namespace Editor
{
	namespace
	{
		constexpr double MB = 1024.0 * 1024.0;
	}

	void OverviewProfilerView::Draw(EditorContext& /*a_editContext*/)
	{
		ImGui::SeparatorText("Frame");
		DrawFrame();

		ImGui::SeparatorText("Memory");
		DrawMemory();
		DrawVideoMemory();
	}

	//======================================================================================
	// FPS & デルタタイム
	//======================================================================================
	void OverviewProfilerView::DrawFrame()
	{
		auto& _engine = MainEngine::Instance();
		const float _dt = _engine.GetDeltaTime();

		Engine::EditorField::Value("FPS", "%d", _engine.GetFPS());
		Engine::EditorField::Value("Frame Time", "%.2f ms", _dt * 1000.0f);
	}

	//======================================================================================
	// メモリ使用量(RAM)
	//======================================================================================
	void OverviewProfilerView::DrawMemory()
	{
		auto* _pWindow = MainEngine::Instance().RefNativeWindow();
		if (!_pWindow) return;

		Engine::EditorField::Value("RAM Usage", "%.2f MB", _pWindow->GetMemoryUsage() / MB);
	}

	//======================================================================================
	// VRAM 使用量
	//======================================================================================
	void OverviewProfilerView::DrawVideoMemory()
	{
		auto* _pGE = MainEngine::Instance().RefGraphicsEngine();
		auto* _pDevice = _pGE ? _pGE->RefRenderDevice()->RefGraphicsDevice() : nullptr;
		auto* _pAdapter = _pDevice ? _pDevice->RefAdapter() : nullptr;
		if (!_pAdapter) return;

		// 使用量を引けるのは IDXGIAdapter3 から
		ComPtr<IDXGIAdapter3> _adapter3;
		if (FAILED(_pAdapter->QueryInterface(IID_PPV_ARGS(&_adapter3)))) return;

		// ローカルビデオメモリ(GPU に載っているもの)の使用状況
		DXGI_QUERY_VIDEO_MEMORY_INFO _videoMemInfo = {};
		if (FAILED(_adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &_videoMemInfo))) return;

		// Budget は OS がこのアプリに割り当てている量
		Engine::EditorField::Value("VRAM Usage", "%.2f / %.2f MB",
			static_cast<double>(_videoMemInfo.CurrentUsage) / MB,
			static_cast<double>(_videoMemInfo.Budget) / MB);
		Engine::EditorField::Tooltip("用途ごとの内訳は Graphics の Video Memory にある");
	}
}
