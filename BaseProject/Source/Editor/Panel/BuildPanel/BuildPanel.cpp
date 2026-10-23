#include "BuildPanel.h"

#include "../../Helper/EditorHelper.h"
#include "Converter/AssetConverterRegistry.h"

#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/Scene/SceneManager/SceneManager.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

namespace Editor
{
	BuildPanel::BuildPanel()
	{}
	void BuildPanel::OnDrawImGui(EditorContext& a_editContext)
	{
		// ビルド書き出しディレクトリ選択
		SelectBuildDirectory();

		ImGui::Separator();

		// 使用アセットのコンバート
		UseAssetConvert(a_editContext);
	}
	void BuildPanel::SelectBuildDirectory()
	{
		ImGui::Text(
			"%s",
			m_buildDirectory.empty()
			? "Not selected"
			: m_buildDirectory.c_str()
		);

		ImGui::SameLine();

		if (ImGui::Button("Select Build Directory"))
		{
			EditorHelper::GetDirectoryNameOpenExplorer(m_buildDirectory);
		}
	}

	//======================================================================================
	// 1件コンバートする
	//--------------------------------------------------------------------------------------
	// アセット種別ごとの処理は Converter/ 以下。ここは引いて呼ぶだけ
	//======================================================================================
	bool BuildPanel::ConvertAsset(EditorContext& a_editContext, const Core::GUID& a_guid)
	{
		auto* _pResourceManager = a_editContext.pServices->pResourceManager;
		const auto* _pProp = _pResourceManager->GetAssetDatabase().GetAssetProperty(a_guid);
		if (!_pProp) return false;

		const auto* _pConverter = Build::AssetConverterRegistry::Find(_pProp->type);
		if (!_pConverter)
		{
			ENGINE_WARNING("[Build] この種別のコンバーターがありません : %s (%s)",
				_pProp->type.c_str(), _pProp->fileName.c_str());
			return false;
		}

		const std::string _filePath = _pResourceManager->GetAssetDatabase().GetFilePathFromGUID(a_guid);

		Build::AssetConvertContext _context =
		{
			*_pResourceManager,
			*_pProp,
			_filePath,
			Core::File::GetDirFromPath(_filePath),
			Core::File::GetFileNameWithoutExtension(_filePath),
			a_editContext.pServices->pSceneManager ? a_editContext.pServices->pSceneManager->RefWorld() : nullptr
		};

		std::string _error = {};
		if (!_pConverter->Convert(_context, _error))
		{
			ENGINE_WARNING("[Build] コンバート失敗 : %s (%s) %s",
				_pProp->type.c_str(), _filePath.c_str(), _error.c_str());
			return false;
		}

		ENGINE_LOG("[Build] コンバート完了 : %s (%s)", _pProp->type.c_str(), _filePath.c_str());
		return true;
	}

	void BuildPanel::UseAssetConvert(EditorContext& a_editContext)
	{
		if (!a_editContext.pServices) return;
		auto* _pSceneManager = a_editContext.pServices->pSceneManager;
		if (!_pSceneManager) return;
		auto _guids = _pSceneManager->GetUsedAssetGUIDs();

		auto* _pResourceManager = a_editContext.pServices->pResourceManager;
		if (!_pResourceManager) return;
		const auto& _assetDataBase = _pResourceManager->GetAssetDatabase();

		ImGui::Text("Used Assets : %zu", _guids.size());

		// 一括ですべてをコンバートするボタン : 変換済みと変換の要らないものは飛ばす
		if (ImGui::Button("All Convert"))
		{
			size_t _successCount = 0;
			size_t _failCount = 0;
			for (const auto& _guid : _guids)
			{
				const auto* _pProp = _assetDataBase.GetAssetProperty(_guid);
				if (!_pProp) continue;

				const auto* _pConverter = Build::AssetConverterRegistry::Find(_pProp->type);
				if (!_pConverter || _pConverter->IsConverted(*_pProp)) continue;

				if (ConvertAsset(a_editContext, _guid)) ++_successCount;
				else ++_failCount;
			}
			ENGINE_LOG("[Build] 一括コンバート : 成功 %zu / 失敗 %zu", _successCount, _failCount);
		}

		ImGui::Separator();

		// 個別でコンバートする : マーク・ボタン・名前を列でそろえる
		if (!ImGui::BeginTable("UsedAssets", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY))
		{
			return;
		}
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.0f);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		for (size_t _i = 0; _i < _guids.size(); ++_i)
		{
			const auto& _guid = _guids[_i];
			ImGui::PushID(static_cast<int>(_i));

			ImGui::TableNextRow();

			const auto* _pProp = _assetDataBase.GetAssetProperty(_guid);
			if (!_pProp)
			{
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("アセットプロパティが見つかりません");
				ImGui::PopID();
				continue;
			}

			// 種別のコンバーター : 無い種別は変換できない
			const auto* _pConverter = Build::AssetConverterRegistry::Find(_pProp->type);
			const bool _isConverted = _pConverter && _pConverter->IsConverted(*_pProp);

			const auto& _filePath = _assetDataBase.GetFilePathFromGUID(_guid);

			// 1列目 : コンバート済みかどうか(緑：変換済み / 赤：未変換)
			ImGui::TableSetColumnIndex(0);
			{
				const ImVec2 _cursorPos = ImGui::GetCursorScreenPos();
				const float _radius = 5.0f;
				ImGui::GetWindowDrawList()->AddCircleFilled(
					ImVec2(_cursorPos.x + _radius + 2.0f, _cursorPos.y + ImGui::GetFrameHeight() * 0.5f),
					_radius,
					_isConverted
					? IM_COL32(50, 220, 100, 255)
					: IM_COL32(240, 70, 70, 255)
				);
				ImGui::Dummy(ImVec2(_radius * 2.0f + 4.0f, ImGui::GetFrameHeight()));
			}

			// 2列目 : コンバートボタン
			ImGui::TableSetColumnIndex(1);
			ImGui::BeginDisabled(_pConverter == nullptr);
			if (ImGui::Button("Convert"))
			{
				ConvertAsset(a_editContext, _guid);
			}
			ImGui::EndDisabled();

			// 3列目 : 種別と名前
			ImGui::TableSetColumnIndex(2);
			ImGui::AlignTextToFramePadding();
			ImGui::Text("[%s] %s", _pProp->type.c_str(), _filePath.c_str());

			ImGui::PopID();
		}

		ImGui::EndTable();
	}
}
