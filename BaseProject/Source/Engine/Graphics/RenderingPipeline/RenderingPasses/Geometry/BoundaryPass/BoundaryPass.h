#pragma once

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	//======================================================================================
	// BoundaryPass
	//
	// 行ける範囲の境界面(DrawSubmitter::SubmitBoundary で積んだ面)を板ポリで描く。
	// 面ごとに定数バッファを詰めて1枚ずつ描く(面は多くても立方体の6枚ほど)。
	//
	// 半透明なので深度は読むだけで書かず、手前のものには隠される。
	// 内側からも外側からも見えるよう裏面も描く
	//======================================================================================
	class BoundaryPass : public Pass
	{
	public:
		~BoundaryPass() override = default;

		void SetupSlots() override;
		void OnLinksResolved() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;
	};
}
