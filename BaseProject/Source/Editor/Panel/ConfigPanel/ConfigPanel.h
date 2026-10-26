#pragma once
#include "../IPanel.h"
#include "Editor/EditorCommon.h"
#include "Engine/Config/ConfigRegistry.h"

namespace Editor
{
	//==========================================================================================
	// エンジン設定(EngineData.ojoptn)のパネル
	//
	// 値は各システムが持っていて、ConfigRegistry に登録されたものを分類ごとに並べるだけ。
	// 左で分類を選び、右にその分類の設定を出す。上の段で保存・元に戻す・絞り込みを行う
	//==========================================================================================
	class ConfigPanel : public IPanel
	{
	public:
		~ConfigPanel() override = default;

		const char* GetName() const override { return "Engine Config"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::RightBottom; }
		void OnDrawImGui(EditorContext& a_editContext) override;

	private:

		// 保存・元に戻す・未保存の印・絞り込み
		void DrawToolbar(Engine::Config::ConfigRegistry& a_registry);

		// 左 : 分類の一覧
		void DrawCategoryList(const Engine::Config::ConfigRegistry& a_registry);

		// 右 : 選んだ分類の設定
		void DrawSections(const Engine::Config::ConfigRegistry& a_registry, const Engine::ECS::EngineServices& a_services);

		// 今の分類と絞り込みに合うか
		bool IsShown(const Engine::Config::ConfigSection& a_section) const;

	private:

		// 選んでいる分類(無しは全部)
		std::optional<Engine::Config::EConfigCategory> m_opCategory = std::nullopt;

		// 名前・持ち主の絞り込み(大文字小文字は区別しない)
		std::string m_filter = "";
	};
}
