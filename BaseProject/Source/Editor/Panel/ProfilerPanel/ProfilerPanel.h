#pragma once

#include "../IPanel.h"
#include "Editor/EditorCommon.h"
#include "ProfilerView/IProfilerView.h"

namespace Editor
{
	//==========================================================================================
	// エンジンの計測結果を表示するパネル
	//
	// 見た目はコンフィグパネルと同じ。上の段に FPS とフレーム時間、
	// 左で表示(ProfilerViewBase の派生)を選び、右にその中身を出す。
	//
	// このパネルが持つのは「どの表示があって、どれを出すか」だけ。
	// 何を計って何を並べるかは各表示のクラス(ProfilerView/ の下)が持つ。
	//   Overview   : FPS・フレーム時間・RAM・VRAM
	//   CPU Timers : ENGINE_PROFILE_SCOPE のスコープごとの時間(Profiler の結果)
	//   ECS        : 今のシーンのワールドの中身(ECSWorldProfiler の結果)
	//   Thread     : メイン・ワーカースレッドの稼働時間(ThreadProfiler の結果)
	//   Graphics   : メガバッファ・ディスクリプタヒープなどの使われ方(GraphicsProfiler の結果)
	//   Particles  : GPUパーティクルの席とプール
	//==========================================================================================
	class ProfilerPanel : public IPanel
	{
	public:

		ProfilerPanel();
		~ProfilerPanel() override = default;

		const char* GetName() const override { return "Profiler"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Bottom; }
		void OnDrawImGui(EditorContext& a_editContext) override;

	private:

		// 表示を足す(足した順に左の一覧へ並ぶ)
		template<typename T>
		void RegisterView() { m_upViewVec.push_back(std::make_unique<T>()); }

		// 上の段 : どの表示を見ていても分かってほしい FPS とフレーム時間
		void DrawToolbar();

		// 左 : 表示の一覧
		void DrawViewList();

		// 右 : 選んだ表示の見出しと中身
		void DrawSelectedView(EditorContext& a_editContext);

	private:

		// 表示(登録したあとは増減しない)
		std::vector<std::unique_ptr<IProfilerView>> m_upViewVec = {};

		// 選んでいる表示(m_upViewVec の添え字)
		size_t m_selectedIndex = 0;
	};
}
