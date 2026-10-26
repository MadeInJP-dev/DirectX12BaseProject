#pragma once

#include "../IPanel.h"
#include "Editor/EditorCommon.h"

namespace Editor
{
	/// <summary>
	/// エンジンのログを表示するパネル
	///
	/// ログ本文は ImGuiTextBuffer へ追記していき、行頭のオフセットだけを別に持つ。
	/// 表示は ImGuiListClipper で見えている行だけを描くので、行数が増えても描画コストは変わらない。
	///
	/// ---- スレッド安全性 ----
	/// ログは**どのスレッドからでも来る**。リソースの非同期ロード(ResourceManager::RequestLoad)は
	/// ジョブとしてワーカースレッドで走り、その中の ENGINE_LOG / ENGINE_ERRLOG が
	/// そのままこのパネルへ流れ込むため。
	///
	/// そこで追加(AddLogRow)は**文字列をキューへ積むだけ**にして、
	/// ImGuiTextBuffer と行オフセットは描画スレッドが FlushPending() で触る。
	/// この2つを複数スレッドから触ると、
	///   ・ImVector の再確保と、他スレッドのインデックス参照がかち合って解放済みメモリを読む
	///   ・行オフセットが本文の長さと食い違い、描画時に範囲外を読む
	/// という形で落ちる(2026-08-17 に修正した起動時のクラッシュ)。
	/// </summary>
	class LogPanel : public IPanel
	{
	public:

		LogPanel();
		~LogPanel() override = default;

		const char* GetName() const override { return "Log"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Bottom; }
		void OnDrawImGui(EditorContext& a_editContext) override;

		// ログのクリア
		void Clear();

		// ログを1行積む。
		// 書式は解釈しないので、'%' を含む文字列(パスや割合)でも落ちない。
		// 積む側はログ用マクロ(ENGINE_LOG など)で、ここへは
		// MainEditor が登録したコールバック越しに届く
		void AddLogRow(const char* a_text);

	private:

		// 溜まっているログを本文へ流し込む(描画スレッドから呼ぶこと)
		void FlushPending();

		// 追記された分から行頭オフセットを拾い、必要ならスクロールを予約する
		void UpdateOffsetsAndScroll(int a_oldSize);

	private:

		// ---- 描画スレッドだけが触る ----
		ImGuiTextBuffer m_textBuffer;
		ImGuiTextFilter m_textFilter;

		ImVector<int> m_lineOffsets;

		bool m_isAutoScroll = true;
		bool m_isScrollToBottom = false;

		// ---- どのスレッドからでも触る(必ず m_pendingMutex の下で) ----
		std::mutex m_pendingMutex;
		std::vector<std::string> m_pendingVec;

		// パネルが描かれない間に溜まり続けないための上限。
		// 超えたら古いものから捨てる(直近のログのほうが原因調査に役立つため)
		static constexpr size_t PENDING_MAX = 4096;
	};
}
