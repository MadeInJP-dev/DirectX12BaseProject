#pragma once

#include "../IOption.h"

#include "Engine/EditorField/EditorField.h"

namespace Engine::Option::GraphicsOptions
{
	// GIのスペースデノイズの設定
	struct WindowOption : IOption
	{
		// ウィンドウタイトル
		std::string windowTitle = "Window";			// 名前
		bool isTitleFPS = false;					// FPSをタイトルに表示するか

		// ウィンドウサイズ
		int windowWidth = 0;
		int windowHeight = 0;

		// ウィンドウモード
		EWindowMode windowMode = EWindowMode::Windowed;
		
		// FPSレート(制限なし / 垂直同期 / 上限FPS)
		EFPSRate fpsRate = EFPSRate::FPS_60;

		const std::string& GetName() const override
		{
			static const std::string NAME = "WindowOption";
			return NAME;
		}


		// カテゴリー
		EOptionCategory GetCategory() override
		{
			return EOptionCategory::Graphics;
		}

		// エディター
		void DrawEdit(const ECS::EngineServices& a_services) override;

		// アーカイブ
		void Archive(Persistence::Archive& a_archive) override;
	};
}