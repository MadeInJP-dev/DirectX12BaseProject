#pragma once
#include "../IPanel.h"
#include "Editor/EditorCommon.h"

namespace Engine::GameObject
{
	class BaseObject;
	class GameObjectManager;
}

namespace Editor
{
	/// <summary>
	/// GameObjectManager に登録されている ECS外オブジェクトのヒエラルキーウィンドウ。
	/// 選択したオブジェクトはインスペクター(Gameモード)で編集される。
	/// </summary>
	/// <remarks>
	/// ドラッグ&ドロップで親子関係を組める(BaseObject の親子。子は親の行列に追従する)。
	/// 見出しはオブジェクトの名前で、名前が無ければクラス名を出す。
	/// </remarks>
	class GameObjectHierarchyPanel : public IPanel
	{
	public:
		~GameObjectHierarchyPanel() override = default;

		const char* GetName() const override { return "Objects"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Left; }
		void OnDrawImGui(EditorContext& a_editContext) override;

	private:

		// 1件ぶんのノードを出す(子があれば折りたたみにする)
		void DrawObjectNode(
			EditorContext& a_editContext,
			GameObject::GameObjectManager* a_pManager,
			GameObject::BaseObject* a_pObject,
			bool& a_inoutIsSelectedAlive);

		// 直前の項目がクリックされていたら選択する
		void HandleSelect(
			EditorContext& a_editContext,
			GameObject::BaseObject* a_pObject,
			bool& a_inoutIsSelectedAlive);

		/// <summary>
		/// 直前の項目の右クリックメニュー
		/// </summary>
		/// <remarks>
		/// ポップアップは「直前の項目」の状態を消費するので、
		/// 選択判定とドラッグ&ドロップより後に呼ぶこと
		/// </remarks>
		void HandleContextMenu(
			EditorContext& a_editContext,
			GameObject::GameObjectManager* a_pManager,
			GameObject::BaseObject* a_pObject);

		// ドラッグ&ドロップで親を付け替える
		void HandleDragAndDrop(
			GameObject::GameObjectManager* a_pManager,
			GameObject::BaseObject* a_pObject);

	private:

		// プレハブとして書き出すときの名前(右クリックで開いた相手ごとに表示名で入れ直す)
		std::string m_prefabName = {};
		const GameObject::BaseObject* m_prefabNameOwner = nullptr;
	};
}
