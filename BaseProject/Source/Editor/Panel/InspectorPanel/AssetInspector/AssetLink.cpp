#include "AssetLink.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

namespace Editor::Inspector
{
	namespace
	{
		//-----------------------------------------------------------------------------------------
		// 未設定のGUIDか
		//
		// 空欄はエラーではなく「そのパーツは使わない」という状態なので、
		// missing とは分けて見せる
		//-----------------------------------------------------------------------------------------
		bool IsEmptyGUID(const Core::GUID& a_guid)
		{
			return (!a_guid.IsValid() || a_guid == Core::DEFAULT_GUID);
		}

		//-----------------------------------------------------------------------------------------
		// 項目名を出して、リンクを同じ行へ並べる準備をする
		//-----------------------------------------------------------------------------------------
		void DrawLabel(const char* a_label)
		{
			if (!a_label || a_label[0] == '\0') return;

			ImGui::TextUnformatted(a_label);
			Engine::EditorField::SameLine();
		}

		//-----------------------------------------------------------------------------------------
		// マウスを乗せたときの手掛かり
		//
		// 同名のアセットが別フォルダにあることがあるので、置き場所まで出す
		//-----------------------------------------------------------------------------------------
		void DrawLinkTooltip(const Resource::AssetProperty& a_prop, bool a_isJumpable)
		{
			if (!ImGui::IsItemHovered()) return;

			ImGui::BeginTooltip();
			Engine::EditorField::Value("Type", "%s", a_prop.type.c_str());
			Engine::EditorField::Value("Path", "%s", a_prop.filePath.c_str());
			ImGui::TextDisabled("%s", a_prop.guid.String().c_str());

			if (a_isJumpable)
			{
				ImGui::Separator();
				ImGui::TextDisabled("click to open");
			}

			ImGui::EndTooltip();
		}
	}

	//-----------------------------------------------------------------------------------------
	// アセットをインスペクターで開く
	//-----------------------------------------------------------------------------------------
	bool JumpToAsset(
		EditorContext& a_editContext,
		const Core::GUID& a_guid,
		bool a_isPushHistory)
	{
		if (IsEmptyGUID(a_guid)) return false;

		// 引けないGUIDへは飛ばない(参照が切れているアセットを開いても何も出せない)
		auto* _pAsset = a_editContext.pServices->pAssetDatabase->FindAssetProperty(a_guid);
		if (!_pAsset) return false;

		// 同じものを開き直したときは履歴だけが伸びるので積まない
		if (a_isPushHistory &&
			a_editContext.selectedAssetGUID.IsValid() &&
			a_editContext.selectedAssetGUID != a_guid)
		{
			a_editContext.assetHistoryVec.push_back(a_editContext.selectedAssetGUID);

			// 古いものから捨てる
			if (a_editContext.assetHistoryVec.size() > EditorContext::ASSET_HISTORY_MAX)
			{
				a_editContext.assetHistoryVec.erase(a_editContext.assetHistoryVec.begin());
			}
		}

		a_editContext.SelectAsset(_pAsset);

		return true;
	}

	//-----------------------------------------------------------------------------------------
	// 参照しているアセットを1件描く
	//-----------------------------------------------------------------------------------------
	bool DrawAssetLink(
		EditorContext* a_pEditContext,
		const char* a_label,
		const Core::GUID& a_guid,
		const char* a_pDisplayText)
	{
		// ---- 未設定 ----
		if (IsEmptyGUID(a_guid))
		{
			DrawLabel(a_label);
			Engine::EditorField::HelpText("(none)");
			return false;
		}

		// ---- データベースに無い ----
		// 飛び先を引くのにもエディターのコンテキストが要る。無ければ名前も出せない
		auto* _pAsset = (a_pEditContext && a_pEditContext->pServices)
			? a_pEditContext->pServices->pAssetDatabase->FindAssetProperty(a_guid)
			: nullptr;
		if (!_pAsset)
		{
			DrawLabel(a_label);
			Engine::EditorField::HelpText("(missing) %s", a_guid.String().c_str());
			return false;
		}

		DrawLabel(a_label);

		// 表示は呼ぶ側の指定が優先。無ければアセット名
		const std::string _text = (a_pDisplayText && a_pDisplayText[0] != '\0')
			? std::string(a_pDisplayText)
			: _pAsset->fileName;

		// ---- 飛び先を持たない呼び出し ----
		// リンクの見た目にすると押せそうに見えてしまうので、ただの文字で出す
		if (!a_pEditContext)
		{
			Engine::EditorField::HelpText("%s", _text.c_str());
			DrawLinkTooltip(*_pAsset, false);
			return false;
		}

		// ---- リンク ----
		// ImGuiのIDは表示文字から作られるので、同じ名前のアセットが並ぶと
		// ホバーの見た目を共有してしまう。GUIDを隠しIDとして足しておく
		const std::string _linkLabel = _text + "##AssetLink" + a_guid.String();

		const bool _isClicked = ImGui::TextLink(_linkLabel.c_str());

		DrawLinkTooltip(*_pAsset, true);

		if (!_isClicked) return false;

		JumpToAsset(*a_pEditContext, a_guid);
		return true;
	}

	//-----------------------------------------------------------------------------------------
	// 行き来バー
	//-----------------------------------------------------------------------------------------
	void DrawAssetNavBar(EditorContext& a_editContext)
	{
		auto& _historyVec = a_editContext.assetHistoryVec;

		// 戻り先が無くてもボタンは出す(押せないことが見えていたほうが分かりやすい)
		ImGui::BeginDisabled(_historyVec.empty());
		const bool _isBack = ImGui::ArrowButton("##AssetBack", ImGuiDir_Left);
		ImGui::EndDisabled();

		Engine::EditorField::SameLine();

		if (_historyVec.empty())
		{
			Engine::EditorField::HelpText("(no history)");
			return;
		}

		// 戻り先の名前を出しておく : 何段も辿ったときに現在地を見失わないため
		const auto* _pBackAsset =
			a_editContext.pServices->pAssetDatabase->FindAssetProperty(_historyVec.back());

		Engine::EditorField::HelpText("Back : %s", _pBackAsset ? _pBackAsset->fileName.c_str() : "(missing)");

		if (!_isBack) return;

		//------------------------------------------------------------------
		// 1つ戻る
		//
		// 戻り先のアセットが消えている(データベースから引けない)ことがあるので、
		// 開けるものが見つかるまで履歴を捨てながら遡る。
		// 戻る操作自体は履歴へ積まない(積むと同じ2件を行き来し続ける)。
		//------------------------------------------------------------------
		while (!_historyVec.empty())
		{
			const Core::GUID _guid = _historyVec.back();
			_historyVec.pop_back();

			if (JumpToAsset(a_editContext, _guid, false)) break;
		}
	}
}
