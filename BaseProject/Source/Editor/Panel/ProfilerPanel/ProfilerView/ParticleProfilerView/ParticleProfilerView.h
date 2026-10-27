#pragma once

#include "Editor/EditorCommon.h"
#include "../IProfilerView.h"

namespace Editor
{
	/// <summary>
	/// GPUパーティクルの使われ方を表示する(ProfilerPanel の Particles 表示)
	///
	/// 発生源の席が返し忘れで増え続けていないか、プールごとの容量・伸び方・眠っているかを見る。
	/// 値は ParticleBufferManager が持っているので、ここは引いて並べるだけ
	/// </summary>
	class ParticleProfilerView : public IProfilerView
	{
	public:

		const char* GetName() const override { return "Particles"; }
		const char* GetDescription() const override { return "GPUパーティクルの発生源の席とプールごとの容量・状態"; }
		void Draw(EditorContext& a_editContext) override;
	};
}
