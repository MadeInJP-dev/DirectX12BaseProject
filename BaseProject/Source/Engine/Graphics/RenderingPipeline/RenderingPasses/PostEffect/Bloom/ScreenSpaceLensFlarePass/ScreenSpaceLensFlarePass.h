#pragma once

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	//======================================================================================
	// ScreenSpaceLensFlarePass
	//
	// まとめ終わったブルームを画面の中心で折り返して、メインカラーへ加算する。
	// 明るいところの反対側にゴーストが映る、簡易的なレンズフレア。
	//
	// ブルームから作るので、プレイヤーがブルームを切っていれば一緒に掛けない
	//======================================================================================
	class ScreenSpaceLensFlarePass final : public Pass
	{
	public:
		~ScreenSpaceLensFlarePass() override = default;

		void SetupSlots() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;

		// ※ HLSL の ScreenSpaceLensFlareData と並びを合わせること
		struct ScreenSpaceLensFlareCB
		{
			float intensity;	// 強度
			int enable;		// 0 なら素通し
			float pad[2];
		};

		// 編集対象の値 : エディターはここだけを触る。
		// 実体は下の m_cb で、シェーダーへはそのまま送られる
		using Params = ScreenSpaceLensFlareCB;
		Params& RefParams() { return m_cb; }

	private:

		ScreenSpaceLensFlareCB m_cb = { 0.1f, 1, { 0.0f, 0.0f } };
	};
}
