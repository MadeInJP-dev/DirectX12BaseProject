#pragma once

#include "../Internal/EditorContext.h"
#include "Editor/EditorCommon.h"

namespace Editor
{
	/// <summary>
	/// パネルの既定の置き場所(初めて開いたときと Window > Reset Layout で使う)
	/// </summary>
	/// <remarks>
	///  Left  |     Center      | Right
	///        |                 |-------------
	///        |-----------------| RightBottom
	///        |     Bottom      |
	/// </remarks>
	enum class EPanelDock
	{
		Left,			// 一覧(エンティティ・オブジェクト)
		Center,			// シーンビュー
		Right,			// インスペクター
		RightBottom,	// 設定
		Bottom,			// アセット・ログ・計測など
	};

	/// <summary>
	/// エディター上でパネルとして表示するのならこのクラスを継承させる
	/// </summary>
	class IPanel
	{
	public:
		virtual ~IPanel() = default;
		virtual const char* GetName() const = 0;						// パネル名(見出しと Window メニューに出る)
		virtual void OnDrawImGui(EditorContext& a_editContext) = 0;		// 実際のパネルUI描画
		virtual ImGuiWindowFlags GetFlags() const { return 0; }			// ウィンドウフラグ
		virtual EPanelDock GetDefaultDock() const { return EPanelDock::Bottom; }	// 既定の置き場所

		// 開いているか(ImGui::Begin の閉じるボタンが書き換えるので参照で渡す)
		bool& RefIsOpen() { return m_isOpen; }

	private:

		bool m_isOpen = true;											// メニューバーから開閉するためのフラグ
	};
}
