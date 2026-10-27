#pragma once

#include "Editor/EditorCommon.h"
#include "../IProfilerView.h"

namespace Engine::Graphics
{
	struct GraphicsSnapshot;
}

namespace Editor
{
	/// <summary>
	/// 描画まわりの使われ方を表示する(ProfilerPanel の Graphics 表示)
	///
	/// メガバッファ・ディスクリプタヒープ・定数バッファ・毎フレームの構造体バッファなど、
	/// 容量が決まっていて溢れると描画が壊れるものが、どこまで埋まっているかを見るためのもの。
	/// 計測は GraphicsProfiler の担当なので、ここは取り直しを頼んで結果を並べるだけ
	/// </summary>
	class GraphicsProfilerView : public IProfilerView
	{
	public:

		const char* GetName() const override { return "Graphics"; }
		const char* GetDescription() const override { return "メガバッファ・ディスクリプタヒープ・定数バッファなど、容量の決まったものの埋まり具合"; }
		void Draw(EditorContext& a_editContext) override;

	private:

		// 各タブ
		void DrawOverview(const Graphics::GraphicsSnapshot& a_snapshot);
		void DrawVideoMemory(const Graphics::GraphicsSnapshot& a_snapshot);
		void DrawDescriptorHeap(const Graphics::GraphicsSnapshot& a_snapshot);
		void DrawMegaBuffers(const Graphics::GraphicsSnapshot& a_snapshot);
		void DrawFrameBuffers(const Graphics::GraphicsSnapshot& a_snapshot);
		void DrawDrawCounts(const Graphics::GraphicsSnapshot& a_snapshot);

		// 埋まり具合が閾値を超えているものを並べる
		void DrawWarnings(const Graphics::GraphicsSnapshot& a_snapshot);

		// 新しい結果が来ていたら履歴へ積む
		void PushHistory(const Graphics::GraphicsSnapshot& a_snapshot);

	private:

		// 履歴の長さ(取り直した回数)
		static constexpr size_t HISTORY_LENGTH = 240;

		// 履歴 1本ぶん : 古い順に並ぶリング
		struct History
		{
			std::vector<float> values = {};
			size_t head = 0;		// 次に書く位置
			size_t count = 0;		// 積んだ数(最大 HISTORY_LENGTH)

			void Push(float a_value);

			// 古い順に並べ直した配列
			std::vector<float> ToOrdered() const;
		};

		// 取り直しを止めて、今の結果を眺める
		bool m_isPaused = false;

		// 履歴へ最後に積んだ結果の番号
		uint64_t m_lastCaptureCount = 0;

		// 履歴
		History m_vramHistory = {};			// VRAM の使用量(MB)
		History m_srvHistory = {};			// SRV の席の数
		History m_cbHistory = {};			// 定数バッファ(グラフィック用)の使用量(KB)
		History m_drawItemHistory = {};		// 描画アイテムの数
	};
}
