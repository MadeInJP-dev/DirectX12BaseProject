#pragma once

#include "../IPanel.h"
#include "ECSProfilerView.h"
#include "ThreadProfilerView.h"
#include "GraphicsProfilerView.h"
#include "Editor/EditorCommon.h"

namespace Editor
{
	class Profiler;

	/// <summary>
	/// エンジンの計測結果を表示するパネル
	/// FPS/メモリなどの全体統計と、Profilerが積んだ関数ごとの計測結果を出す
	///
	/// 計測はProfilerの担当なので、ここは受け取った結果を並べるだけにする
	///
	/// メニューバーの View で表示を切り替える
	///   Engine : エンジン全体の統計とスコープごとの計測
	///   ECS    : 今のシーンのワールドの中身(ECSWorldProfiler の結果)
	///   Thread : メイン・ワーカースレッドの稼働時間(ThreadProfiler の結果)
	///   Graphics : メガバッファ・ディスクリプタヒープなどの使われ方(GraphicsProfiler の結果)
	/// </summary>
	class ProfilerPanel : public IPanel
	{
	public:
		~ProfilerPanel() override = default;

		const char* GetName() const override { return "Profiler"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Bottom; }
		void OnDrawImGui(EditorContext& a_editContext) override;
		ImGuiWindowFlags GetFlags() const override { return ImGuiWindowFlags_MenuBar; }

	private:

		// 表示の切り替え
		enum class EView
		{
			Engine,		// エンジン全体
			ECS,		// ECSワールド
			Thread,		// スレッドの稼働時間
			Graphics,	// 描画まわりの使われ方
		};

		// メニューバー
		void DrawMenuBar();

		// エンジン全体の表示
		void DrawEngineView(EditorContext& a_editContext);

		// メモリ使用率(Ram)
		void DrawMemoryUsage();

		// VRAM使用率
		void DrawVRAMUsage();

		// CPU時間 : GPU時間
		void DrawCoreTimings();

		// FPS & デルタタイム
		void DrawFPSAndDeltaTime();

		// DrawCall数 : 総プリミティブ数 : アイテム
		void DrawRenderStats();

		// ディスクリプタヒープ使用率
		void DrawDescriptorHeapUsage();

		// GPUパーティクル : 発生源の席の使用状況とプールの一覧
		void DrawParticleStats();

		// スコープごとの計測結果(ENGINE_PROFILE_SCOPE / Profilerが並べ替え済み)
		void DrawTimerTable(Profiler* a_pProfiler);

	private:

		EView			m_eView = EView::Engine;	// 今の表示
		ECSProfilerView	m_ecsView = {};				// ECSワールドの表示
		ThreadProfilerView m_threadView = {};		// スレッドの稼働時間の表示
		GraphicsProfilerView m_graphicsView = {};	// 描画まわりの使われ方の表示
	};
}
