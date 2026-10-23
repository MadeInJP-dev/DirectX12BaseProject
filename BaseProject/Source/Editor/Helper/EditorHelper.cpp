#include "EditorHelper.h"

#include "Engine/Graphics/D3D12/DescriptorHeapManager/DescriptorHeapManager.h"
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"

#include <ShObjIdl.h>

namespace Editor
{
	//======================================================================================
	// 一覧を絞り込むための検索欄
	//======================================================================================
	const std::string& EditorHelper::DrawSearchBox(const char* a_lable, const char* a_hint, bool a_isAutoFocus)
	{
		// 入力は呼び出し位置(ImGuiのID)ごとに覚える。
		// 呼ぶ側に文字列を持たせずに済ませるためで、
		// アセット選択のように同じ関数を何十箇所からも呼ぶ欄でも入力が混ざらない
		// (IDは窓とIDスタックを含むので、ラベルが同じでも別の欄なら別キーになる)。
		static std::unordered_map<ImGuiID, std::string> s_searchMap;

		std::string& _search = s_searchMap[ImGui::GetID(a_lable)];

		// 開いた直後はそのまま打ち始められるようにする。
		// 前回の入力が残っていると「何も出ない」ように見えるのでここで消す。
		// 出しっぱなしのパネルでは邪魔なので、呼ぶ側が切れるようにしてある。
		if (a_isAutoFocus && ImGui::IsWindowAppearing())
		{
			_search.clear();
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint(a_lable, a_hint, &_search);

		return _search;
	}

	//======================================================================================
	// 検索文字列に引っかかるか
	//======================================================================================
	bool EditorHelper::IsMatchSearch(const std::string& a_search, const std::string& a_text)
	{
		// 未入力なら絞り込まない
		if (a_search.empty()) return true;

		// 大文字小文字を区別しない部分一致。
		// 小文字化したコピーを作らずに済ませたいので std::search で比較器を差し替える
		auto _isSameChar = [](char a_lhs, char a_rhs)
		{
			return
				std::tolower(static_cast<unsigned char>(a_lhs)) ==
				std::tolower(static_cast<unsigned char>(a_rhs));
		};

		return std::search(
			a_text.begin(), a_text.end(),
			a_search.begin(), a_search.end(),
			_isSameChar) != a_text.end();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE EditorHelper::GetImGuiTexHandle(const Handle<Graphics::D3D12::ImGuiSRV>& a_imguiSRVHandle)
	{
		auto* _pGE = MainEngine::Instance().RefGraphicsEngine();
		if (!_pGE) return {};

		auto* _pHeapManager = _pGE->RefDescriptorHeapManager();
		if (!_pHeapManager) return {};

		return _pHeapManager->GetImGuiSRVGPUHandle(a_imguiSRVHandle);
	}

	//======================================================================================
	// SRVを画像としてImGui上に描画させる
	//======================================================================================
	ImVec2 EditorHelper::DrawSRVView(
		D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle,
		float a_width, float a_height,
		float /*a_minSize*/, float /*a_maxSize*/
	)
	{
		ImTextureID _imTex = static_cast<ImTextureID>(a_gpuHandle.ptr);

		// 横幅だけを取得（縦の残り領域は無視する）
		float drawWidth = ImGui::GetContentRegionAvail().x;

		// アスペクト比を計算
		float aspect = a_width / a_height;

		// 横幅に合わせて高さを逆算する
		float drawHeight = drawWidth / aspect;

		ImGui::Text("Size : %f,%f", drawWidth, drawHeight);

		// 計算したサイズで描画
		ImGui::Image(_imTex, ImVec2(drawWidth, drawHeight));

		// 実際に描画したサイズを返す
		return ImVec2(drawWidth, drawHeight);
	}

	//======================================================================================
	// ノードのタイトルバー表示
	//======================================================================================
	void EditorHelper::DrawNodeTitleBar(const std::string& a_name)
	{
		ImNodes::BeginNodeTitleBar();
		ImGui::Text("%s", a_name.c_str());
		ImNodes::EndNodeTitleBar();
	}

	//--------------------------------------------------------------------------------------
	// エクスプローラーを開いてフォルダ名を取得
	//--------------------------------------------------------------------------------------
	bool EditorHelper::GetDirectoryNameOpenExplorer(std::string& a_dirName)
	{
		a_dirName = "";
		IFileDialog* _pDialog = nullptr;

		// ダイアログインスタンス作成
		HRESULT _hr = CoCreateInstance(
			CLSID_FileOpenDialog,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&_pDialog)
		);
		if (FAILED(_hr))
		{
			ENGINE_LOG("ダイアログインスタンス作成失敗");
			return false;
		}

		// フォルダ選択モードにする
		DWORD _options = 0;
		_pDialog->GetOptions(&_options);

		_pDialog->SetOptions(
			_options |
			FOS_PICKFOLDERS |
			FOS_FORCEFILESYSTEM |
			FOS_PATHMUSTEXIST
		);

		// ダイアログを表示
		_hr = _pDialog->Show(nullptr);

		if (FAILED(_hr))
		{
			ENGINE_LOG("ダイアログを開くのに失敗");
			_pDialog->Release();
			return false;
		}

		// 選択したフォルダのアイテムを取得
		IShellItem* _pItem = nullptr;
		_hr = _pDialog->GetResult(&_pItem);

		if (SUCCEEDED(_hr))
		{
			// アイテムからパスを取得
			PWSTR _path = nullptr;
			_hr = _pItem->GetDisplayName(
				SIGDN_FILESYSPATH,
				&_path
			);

			if (SUCCEEDED(_hr))
			{
				// Unicodeパスを文字列に変換
				a_dirName = std::filesystem::path(_path).string();

				// 解放
				CoTaskMemFree(_path);
			}

			_pItem->Release();
		}
		_pDialog->Release();

		return SUCCEEDED(_hr);
	}
}
