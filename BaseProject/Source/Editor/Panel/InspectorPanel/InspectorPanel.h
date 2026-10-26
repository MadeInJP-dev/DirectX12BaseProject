#pragma once

#include "../IPanel.h"
#include "Editor/EditorCommon.h"

namespace Editor::Inspector
{
	class AssetInspector;
}

namespace Editor
{
	class InspectorPanel : public IPanel
	{
	public:
		InspectorPanel();
		~InspectorPanel() override;

		const char* GetName() const override { return "Inspector"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Right; }
		void OnDrawImGui(EditorContext& a_editContext) override;

	private:

		// アセット表示はエディターの実体(ノードエディターなど)を抱えるのでクラスで持つ。
		// 前方宣言で持つので、生成と破棄は完全型が見える .cpp 側に置く
		std::unique_ptr<Inspector::AssetInspector> m_upAssetInspector = nullptr;
	};
}