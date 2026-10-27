#pragma once

#include "Editor/EditorCommon.h"
#include "../IProfilerView.h"

namespace Editor
{
	/// <summary>
	/// ENGINE_PROFILE_SCOPE で計ったスコープごとの時間を表示する(ProfilerPanel の CPU Timers 表示)
	///
	/// 平均・最小最大の組み立てと並べ替えは Profiler が済ませているので、ここは表に並べるだけ
	/// </summary>
	class ScopeTimerProfilerView : public IProfilerView
	{
	public:

		const char* GetName() const override { return "CPU Timers"; }
		const char* GetDescription() const override { return "ENGINE_PROFILE_SCOPE で計ったスコープごとの時間(平均・最小・最大・回数)"; }
		void Draw(EditorContext& a_editContext) override;
	};
}
