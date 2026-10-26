#pragma once

#include "../IPanel.h"
#include "Editor/EditorCommon.h"

namespace Editor
{
	class AssetDataBasePanel : public IPanel
	{
	public:
		AssetDataBasePanel();
		~AssetDataBasePanel() override = default;

		const char* GetName() const override { return "Assets"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Bottom; }
		void OnDrawImGui(EditorContext& a_editContext) override;
	private:

		void CreateAssetButton(EditorContext& a_editContext);

		void AssetDataBaseExplorer(EditorContext& a_editContext);

		float m_windowWidth = 1920;
		float m_windowHeight = 1080;

		// 文字列キャッシュ
		char m_nameCach[256] = "";
		char m_pathCach[256] = "";

		// "アセット名" -> "生成処理" の辞書
		// 同名チェック(アセットデータベース)や、作った中身の保存(リソースマネージャー)で使うので、
		// サービス一式を呼ぶ側から渡す
		using AssetCreateFunc = std::function<void(const ECS::EngineServices&, const std::string&, const std::string&)>;
		std::unordered_map<std::string, AssetCreateFunc> m_assetCreateFuncs;
	};
}