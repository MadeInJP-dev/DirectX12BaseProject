#pragma once

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Engine::Time
{
	//==========================================================================================
	// フレームの刻みの設定(エンジン設定の "TimeConfig")。持ち主は TimeManager
	//==========================================================================================
	struct TimeConfig
	{
		// FPSの上限(制限なし / 垂直同期 / 上限FPS)。プレイヤーが選んだものがあればそちら(MainEngine::GetFPSRate)
		EFPSRate fpsRate = EFPSRate::FPS_60;

		// 設定画面・保存(ConfigRegistry から呼ばれる)
		void DrawEdit(const ECS::EngineServices& a_services);
		void Archive(Persistence::Archive& a_archive);
	};
}
