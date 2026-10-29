#pragma once

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Engine::Window
{
	//==========================================================================================
	// ウィンドウの設定(エンジン設定の "WindowConfig")。持ち主は NativeWindow
	//
	// 大きさは描画解像度も兼ねる(バックバッファとレンダーグラフはこの大きさで作られ、
	// クライアント領域へ伸ばして映す)。どちらも作るときにしか読まないので、次の起動から効く。
	// 今の描画解像度を知りたい側は GraphicsEngine::GetRenderWidth / GetRenderHeight を見る
	//==========================================================================================
	struct WindowConfig
	{
		// タイトル
		std::string windowTitle = "Window";
		bool isTitleFPS = false;					// FPSをタイトルに表示するか

		// 大きさ(= 描画解像度)
		int windowWidth = 0;
		int windowHeight = 0;

		// 起動したときのウィンドウモード(プレイヤーが選んだものがあればそちら)
		EWindowMode windowMode = EWindowMode::Windowed;

		// アイコンパス
		std::string iconPath = "";

		// 設定画面・保存(ConfigRegistry から呼ばれる)
		void DrawEdit(const ECS::EngineServices& a_services);
		void Archive(Persistence::Archive& a_archive);
	};
}
