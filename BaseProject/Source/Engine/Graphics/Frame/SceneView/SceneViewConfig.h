#pragma once

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Engine::Graphics
{
	//==========================================================================================
	// 視点まわりの描画設定(エンジン設定の "SceneViewConfig")。持ち主は SceneView
	//==========================================================================================
	struct SceneViewConfig
	{
		// TAA用のカメラジッター(サブピクセル揺らし)を掛けるか。
		// 切るとジッターが止まり、TAAはブレンドのみ(空間的なAA効果は無くなる)になる。デバッグ用
		bool useJitter = true;

		// 設定画面・保存(ConfigRegistry から呼ばれる)
		void DrawEdit(const ECS::EngineServices& a_services);
		void Archive(Persistence::Archive& a_archive);
	};
}
