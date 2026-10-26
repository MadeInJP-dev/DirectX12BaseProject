#pragma once

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Engine::Graphics
{
	//==========================================================================================
	// デバッグ描画の設定(エンジン設定の "DebugDrawConfig")。持ち主は DebugDraw
	//
	// コライダーやレイなどのワイヤー表示のオンオフ。判定は DebugDraw の入口で行うので、
	// 呼び出し側(システムなど)は今まで通り無条件に Draw～ を呼んでよい
	//==========================================================================================
	struct DebugDrawConfig
	{
		bool drawWire = true;	// false ならデバッグワイヤーを一切描かない

		// 設定画面・保存(ConfigRegistry から呼ばれる)
		void DrawEdit(const ECS::EngineServices& a_services);
		void Archive(Persistence::Archive& a_archive);
	};
}
