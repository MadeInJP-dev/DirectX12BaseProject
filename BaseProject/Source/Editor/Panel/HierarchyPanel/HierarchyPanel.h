#pragma once
#include "../IPanel.h"
#include "Editor/EditorCommon.h"

namespace Engine
{
	namespace ECS
	{
		class World;
	}
}

namespace Editor
{
	class HierarchyPanel : public IPanel
	{
	public:
		~HierarchyPanel() override = default;

		const char* GetName() const override { return "Entities"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Left; }
		void OnDrawImGui(EditorContext& a_editContext) override;
	private:

		// 空のエンティティの追加
		void AddEntity(EditorContext& a_editContext,ECS::World* a_pWorld);

		// プレハブから実体を生成して追加
		void InstantiatePrefab(EditorContext& a_editContext,ECS::World* a_pWorld, const Core::GUID& a_guid);

		// エンティティノードの描画
		// a_isDrawChildren = false のときは子をたどらない(検索中の平坦表示用)
		void DrawEntityNode(EditorContext& a_editContext,Engine::ECS::World* a_pWorld, const Engine::ECS::Entity& a_entity, bool a_isDrawChildren = true);

		// 一覧に出す名前(NameComponent が無ければエンティティID)
		std::string GetEntityLabel(Engine::ECS::World* a_pWorld, const Engine::ECS::Entity& a_entity) const;

		// ドラッグアンドドロップの制御
		void HandleDragAndDrop(ECS::World* a_pWorld, const ECS::Entity& a_entity, const std::string& a_label);

		// 親子関係の再構築処理
		void AttachChild(ECS::World* a_pWorld, const ECS::Entity& a_parent, const ECS::Entity& a_child);

		// 子供の取得
		std::vector<ECS::Entity> GetChildEntities(Engine::ECS::World* a_pWorld, ECS::Entity a_parent) const;

		// 親の状態
		enum class EParentState
		{
			None,		// 親を持たない(ルート)
			HasParent,	// 親が居る(その子として出る)
			Missing,	// 親を指しているのに見つからない(ルートに印付きで出す)
		};
		EParentState GetParentState(Engine::ECS::World* a_pWorld, const ECS::Entity& a_entity) const;

	private:

		// エンティティ生成時のオフセット
		float m_distance = 10.0f;				// カメラからの距離
	};
}