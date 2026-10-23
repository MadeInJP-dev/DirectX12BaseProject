#pragma once

#include "Editor/EditorCommon.h"

//------------------------------------------------------------------------------------------
// このヘッダーはAnimatorAsset -> StateGraphEditor 経由で
// ResourceManagerより先に読まれるため、重いヘッダーは持ち込まない。
//------------------------------------------------------------------------------------------

namespace Editor
{
	//==========================================================================================
	//
	// エディターの中だけで使い回す描画ヘルパー
	// 「検索欄」「SRVの表示」「ノードエディタ部品」を1か所にまとめている
	//
	// エディターの外からも使う編集UI(値の欄・アセット選択・ボタンなど)は
	// EditorField.h の自由関数にある。外側はそちらだけを使うこと
	//
	//==========================================================================================
	class EditorHelper
	{
	public:
		//--------------------------------------------------------------------------------------
		// 検索
		//--------------------------------------------------------------------------------------

		/// <summary>
		/// 一覧を絞り込むための検索欄
		/// 入力は呼び出し位置(ImGuiのID)ごとに覚えるので、呼ぶ側は文字列を持たなくてよい。
		/// コンボの中で使う場合は BeginCombo の直後に呼ぶこと。
		/// </summary>
		/// <param name="a_lable">検索欄のラベル(ImGuiのID兼用。同じ窓に複数置くなら変える)</param>
		/// <param name="a_hint">未入力時に薄く出す文字</param>
		/// <param name="a_isAutoFocus">
		/// 開いた瞬間に入力を消してフォーカスを入れるか。
		/// 開くたびに打ち直すコンボ・ポップアップでは true、
		/// 出しっぱなしのパネル(入力を保ちたい / 勝手にフォーカスを奪われたくない)では false。
		/// </param>
		/// <returns>入力中の検索文字列(空なら絞り込みなし)</returns>
		static const std::string& DrawSearchBox(
			const char* a_lable = "##Search",
			const char* a_hint = "Search...",
			bool a_isAutoFocus = true
		);

		/// <summary>
		/// 検索文字列に引っかかるか(大文字小文字を区別しない部分一致)
		/// </summary>
		/// <param name="a_search">DrawSearchBox が返した検索文字列</param>
		/// <param name="a_text">候補の表示名</param>
		/// <returns>表示してよければ true(検索文字列が空なら常に true)</returns>
		static bool IsMatchSearch(const std::string& a_search, const std::string& a_text);

		//--------------------------------------------------------------------------------------
		// テクスチャ
		//--------------------------------------------------------------------------------------

		/// <summary>
		/// SRVを画像としてImGui上に描画させる
		/// </summary>
		/// <param name="a_gpuHandle">GPUハンドル</param>
		/// <param name="a_width">横幅</param>
		/// <param name="a_height">縦</param>
		/// <returns>実際に描画した範囲</returns>
		static ImVec2 DrawSRVView(
			D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle,
			float a_width, float a_height,
			float a_minSize = 100, float a_maxSize = 500
		);

		/// <summary>
		/// ImGuiへ渡すテクスチャハンドル(ImGui用SRVのGPUハンドル)を引く
		/// </summary>
		/// <remarks>
		/// ディスクリプタヒープの実体は GraphicsEngine が持っているので、
		/// エディターは MainEngine -> GraphicsEngine を辿って借りる。
		/// パネルごとにこの経路を書くと同じ辿り方が散るため、ここへ寄せてある。
		/// まだ描画周りが出来ていない・ハンドルが無効なときは ptr==0 が返る
		/// </remarks>
		static D3D12_GPU_DESCRIPTOR_HANDLE GetImGuiTexHandle(const Handle<Graphics::D3D12::ImGuiSRV>& a_imguiSRVHandle);

		//--------------------------------------------------------------------------------------
		// ノードエディタ部品
		//--------------------------------------------------------------------------------------

		// ノードのタイトルバー表示
		static void DrawNodeTitleBar(const std::string& a_name);

		//--------------------------------------------------------------------------------------
		// エクスプローラーを開いてフォルダ名を取得
		//--------------------------------------------------------------------------------------
		static bool GetDirectoryNameOpenExplorer(std::string& a_dirName);
	};
}
