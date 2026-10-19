#pragma once

#include "IOption.h"

// プロジェクトオプション
#include "ProjectOptions/BuildConfig.h"
#include "ProjectOptions/InputOption.h"
#include "ProjectOptions/CursorOption.h"

// グラフィックスオプション
#include "GraphicsOptions/WindowOption.h"
#include "GraphicsOptions/RenderingOption.h"

// デバッグオプション
#include "DebugOptions/DebugDrawOption.h"

namespace Engine::Option
{
	// 実体は MainEngine が持ち、EngineServices::pOptionManager で配る。
	// 値を使う側(グラフィックス・入力・オーディオ)へは、MainEngine が起動時と毎フレームに流し込む
	class OptionManager
	{
	public:

		OptionManager();
		~OptionManager() = default;
		NON_COPYABLE_NON_MOVABLE(OptionManager);

		// 初期化
		void Init();

		// シリアライズ・デシリアライズ
		void Serialize();
		void Deserialize();

		// エディター描画
		void DrawEdit(const ECS::EngineServices& a_services);
		//-------------------------------------------------------------------------------------------------
		// アクセサ
		//-------------------------------------------------------------------------------------------------
		// ---- プロジェクト ----
		// 起動設定
		const ProjectOptions::BuildConfig& GetBuildConfig() const{ return m_buildConfig; }

		// 入力設定
		const ProjectOptions::InputOption& GetInputOption() const { return m_inputOption; }
		ProjectOptions::InputOption& RefInputOption() { return m_inputOption; }

		// 自前で描くマウスカーソルの設定
		const ProjectOptions::CursorOption& GetCursorOption() const { return m_cursorOption; }
		ProjectOptions::CursorOption& RefCursorOption() { return m_cursorOption; }

		// ※ 音量(マスター + グループごと)はシーンの持ち物なので、
		//    BaseScene の SceneAudio が持つ(旧 AudioOption)

		// ---- グラフィックス ----
		// ウィンドウ設定
		const GraphicsOptions::WindowOption& GetWindowOption() const { return m_windowOption; }
		GraphicsOptions::WindowOption& RefWindowOption() { return m_windowOption; }

		// レンダリング設定
		const GraphicsOptions::RenderingOption& GetRenderingOption() const { return m_renderingOption; }
		GraphicsOptions::RenderingOption& RefRenderingOption() { return m_renderingOption; }

		// ※ 被写界深度(DoF)はカメラの持ち物なので、
		//    アクティブカメラの FocusParamComponent が持つ(旧 DoFOption)
		// ※ GI デノイズ・ライティング・ブルーム・トーンマップの調整値は
		//    各パスの持ち物なので、レンダリングパイプラインのノードが持つ
		//    (旧 GIOption / LightingOption / BloomOption / ToneMapOption)

		// ---- デバッグ ----
		// デバッグ描画設定(ワイヤー表示のオンオフ)
		const DebugOptions::DebugDrawOption& GetDebugDrawOption() const { return m_debugDrawOption; }
		DebugOptions::DebugDrawOption& RefDebugDrawOption() { return m_debugDrawOption; }

	private:

		void Archive(Persistence::Archive& a_ar);

	private:

		// プロジェクトオプション
		ProjectOptions::BuildConfig m_buildConfig = {};
		ProjectOptions::InputOption m_inputOption = {};
		ProjectOptions::CursorOption m_cursorOption = {};

		// グラフィックスオプション
		GraphicsOptions::WindowOption m_windowOption = {};
		GraphicsOptions::RenderingOption m_renderingOption = {};

		// デバッグオプション
		DebugOptions::DebugDrawOption m_debugDrawOption = {};

		// ループ処理用
		std::vector<IOption*> m_pOptionList;
	};
}
