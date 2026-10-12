#pragma once

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	//======================================================================================
	// UIPass
	//
	// UIを画面へ重ねる。深度を持たないので、積んだ順がそのまま前後になる。
	// 並べ替えは GraphicsEngine 側(レイヤー順)で済んでいる
	//
	// 色のほかに、ポストプロセスを掛けるUIのマスクも出す(UIData::postEffectFlags)。
	// 今は ScanLineMask だけ。ScanLinePass の Mask へ繋ぐと、印の付いたUIにだけ掛かる
	//======================================================================================
	class UIPass : public Pass
	{
	public:
		~UIPass() override = default;

		void SetupSlots() override;
		void OnLinksResolved() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;
	};
}
