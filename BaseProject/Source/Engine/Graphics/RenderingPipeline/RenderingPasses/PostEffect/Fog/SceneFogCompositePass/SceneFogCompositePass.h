#pragma once
#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

namespace Engine::Graphics::Pipeline
{
	//======================================================================================
	// SceneFogCompositePass
	//
	// メインカラーへ、SceneVolumetricFogPass が書いたフォグを重ねる。
	//   出力 = lerp(メインカラー, フォグの色, フォグの濃さ * intensity)
	//
	// intensity と有効/無効はシーン(Engine::Scene::SceneAmbient)の持ち物で、
	// パスは SceneView から受け取って送るだけ。
	//
	// トーンマップ前(HDR)に置くこと。
	// フォグ側が縮小解像度のときは、シーンの深度(SceneDepth。任意)を見ながら引き伸ばす。
	// 物体の縁で、奥の地面のフォグがにじまないようにするため。
	// 深度を繋がないときは、ただのバイリニアで引く
	//======================================================================================
	class SceneFogCompositePass final : public Pass
	{
	public:
		~SceneFogCompositePass() override = default;

		void SetupSlots() override;

		void Compile(const PassContext& a_context) override;
		void Update(const PassContext& a_context) override;


		void Archive(Engine::Persistence::Archive& a_arch) override;

	private:

		// ルートパラメータの番号 : シェーダー(SceneFogCompositeCS)の並びと合わせる
		static constexpr int ROOT_COMPOSITE_CB = 0;
		static constexpr int ROOT_INPUT_SRV = 1;
		static constexpr int ROOT_OUTPUT_UAV = 2;
		static constexpr int ROOT_CAMERA_CB = 3;
	};
}
