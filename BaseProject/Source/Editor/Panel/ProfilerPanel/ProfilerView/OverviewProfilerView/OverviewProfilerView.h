#pragma once

#include "Editor/EditorCommon.h"
#include "../IProfilerView.h"

namespace Editor
{
	/// <summary>
	/// アプリ全体の統計を表示する(ProfilerPanel の Overview 表示)
	///
	/// FPS・フレーム時間と、RAM・VRAM の使用量。
	/// 描画まわりの細かい内訳は Graphics、関数ごとの時間は CPU Timers にある
	/// </summary>
	class OverviewProfilerView : public IProfilerView
	{
	public:

		const char* GetName() const override { return "Overview"; }
		const char* GetDescription() const override { return "FPS・フレーム時間と、RAM・VRAM の使用量"; }
		void Draw(EditorContext& a_editContext) override;

	private:

		// FPS & デルタタイム
		void DrawFrame();

		// メモリ使用量(RAM)
		void DrawMemory();

		// VRAM 使用量(OS が割り当てた予算に対して)
		void DrawVideoMemory();
	};
}
