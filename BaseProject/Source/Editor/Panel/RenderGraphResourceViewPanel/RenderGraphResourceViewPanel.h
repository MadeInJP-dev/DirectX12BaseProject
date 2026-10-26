#pragma once

#include "../IPanel.h"

#include "Engine/Graphics/RenderingPipeline/RenderGraph/Diagnostics/AliasingReport.h"
#include "Editor/EditorCommon.h"

namespace Engine::Graphics::Pipeline
{
	class RenderGraph;
	class VirtualResource;
}

namespace Editor
{
	//======================================================================================
	// レンダリングパイプラインが確保しているリソースの一覧
	//
	// 実行インスタンスはカメラごとにあるので、まずカメラを選んでから中身を見る
	//======================================================================================
	class RenderGraphResourceViewPanel : public IPanel
	{
	public:
		~RenderGraphResourceViewPanel() override = default;

		const char* GetName() const override { return "Render Graph"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Bottom; }
		void OnDrawImGui(EditorContext& a_editContext) override;

	private:

		//----------------------------------------------------------------------------------
		// リソース一覧
		//----------------------------------------------------------------------------------
		void DrawResourceList(const Graphics::Pipeline::RenderGraph& a_graph);

		// 生存区間(このリソースに触る最初と最後のパス)を出す
		void DrawLifetime(
			const Graphics::Pipeline::RenderGraph& a_graph, const Graphics::Pipeline::VirtualResource& a_resource);

		// リソース1本ぶんの絵を出す。履歴つきなら2枚並べる
		void DrawResourceImage(
			const Graphics::Pipeline::RenderGraph& a_graph, const Graphics::Pipeline::VirtualResource& a_resource);

		//----------------------------------------------------------------------------------
		// リソースの使い回し(エイリアシング)
		//
		// 横がパスの実行順、縦が席。同じ席を順番に使い回す様子をそのまま並べる
		//----------------------------------------------------------------------------------
		void DrawAliasing(const Graphics::Pipeline::RenderGraph& a_graph);

		// 上の要約(席の数・ヒープの総量・使い回しでどれだけ減ったか)
		void DrawAliasingSummary(const Graphics::Pipeline::AliasingReport& a_report);

		// 本体の格子
		void DrawAliasingTimeline(const Graphics::Pipeline::AliasingReport& a_report);

		// 席に着けなかったリソース
		void DrawAliasingUnassigned(const Graphics::Pipeline::AliasingReport& a_report);

	private:

		float m_windowWidth = 1920;
		float m_windowHeight = 1080;

		// どのカメラのグラフを見ているか。
		// カメラが減ると添字がずれるので、描くたびに範囲へ収め直す
		int m_selectedGraph = 0;

		//----------------------------------------------------------------------------------
		// エイリアシング表示の状態
		//----------------------------------------------------------------------------------
		// 縦軸の取り方。
		//   false : 席ごとに等間隔。使い回しの構造を読むとき
		//   true  : バイト数どおりの高さ。どれだけ無駄が出ているかを読むとき
		bool m_isByteScale = false;

		float m_passWidth = 34.f;		// パス1つぶんの横幅
		float m_slotHeight = 26.f;		// 席1つぶんの高さ(等間隔のとき)

		// カーソルが乗っている席 : 同じ席のものをまとめて強調する。
		// 無効値なら何も乗っていない
		uint32_t m_hoveredSlot = static_cast<uint32_t>(-1);
	};
}
