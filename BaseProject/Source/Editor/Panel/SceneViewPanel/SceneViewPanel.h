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
namespace Core::Math
{
	struct Ray;
}


namespace Editor
{
	class SceneViewPanel : public IPanel
	{
	public:
		~SceneViewPanel() override = default;

		const char* GetName() const override { return "Scene"; }
		EPanelDock GetDefaultDock() const override { return EPanelDock::Center; }
		void OnDrawImGui(EditorContext& a_editContext) override;
		ImGuiWindowFlags GetFlags() const override { return ImGuiWindowFlags_MenuBar; }
	private:

		// シーンビュー上でのエンティティ選択
		void SelectEntityForMouse(EditorContext& a_editContext, Engine::ECS::World* a_pWorld, const ImVec2& a_pos, const ImVec2& a_rect);

		// レイと描画メッシュのAABBでエンティティをピッキングする(物理空間は使わない)。
		// 当たり判定を持たないエンティティも選択できるよう、描画メッシュから直接判定する。
		Engine::ECS::Entity PickEntityByRay(Engine::ECS::World* a_pWorld, const Math::Ray& a_ray);

		// ギズモ
		// ギズモ本体はプライマリ選択に出し、動かした分は選択中の全エンティティへ適用する。
		void GuizmoDraw(const ImVec2& a_pos, const ImVec2& a_rect, EditorContext& a_editContext, Engine::ECS::World* a_pWorld);

		// ワールド空間の移動量をエンティティへ加算する。
		// 親を持つ場合は LocalTransform が親基準なので、移動量も親空間へ変換してから足す。
		void TranslateEntity(Engine::ECS::World* a_pWorld, const ECS::Entity& a_entity, const Math::Vector3& a_worldDelta);

		// 祖先に選択中のエンティティがいるか。
		// 親と子を同時に選んでいると、親の移動が子へ伝播したうえに子自身も動いて二重に移動してしまうので、
		// その判定に使う。
		bool IsAncestorSelected(Engine::ECS::World* a_pWorld, const EditorContext& a_editContext, const ECS::Entity& a_entity);

		// 選択エンティティの親のワールド行列を取得する。
		// 親がいない/親のワールドがまだ無い場合は単位行列を入れて false を返す。
		bool TryGetParentWorldMatrix(Engine::ECS::World* a_pWorld, const ECS::Entity& a_entity, Math::Matrix& a_outParentMat);

		// ギズモに載せられない情報(各種オフセット位置・パーティクルの発生方向など)を
		// シーンビュー画像の上へ直接描くHUD。
		void DrawEntityHUD(const ImVec2& a_pos, const ImVec2& a_rect, const ECS::Entity& a_entity, Engine::ECS::World* a_pWorld);

		// ECS外オブジェクト用のギズモ(BaseObject::DrawGizmoへ委譲)
		void GameObjectGizmoDraw(const ImVec2& a_pos, const ImVec2& a_rect, EditorContext& a_editContext);

		// メニューバー(カメラ)。シーンの開く・保存は MainMenuBar の File にある
		void DrawMenuBar(EditorContext& a_editContext);

		// エンティティコピー : Ctrl+C / Ctrl+V のショートカット処理
		void CopyEntities(EditorContext& a_editContext, Engine::ECS::World* a_pWorld);

		// 選択中のエンティティ(と子孫)をコピーバッファへ控える
		void CopyToBuffer(EditorContext& a_editContext, Engine::ECS::World* a_pWorld);

		// コピーバッファの内容から新しいエンティティを生成する
		void PasteFromBuffer(Engine::ECS::World* a_pWorld);

		// コピー対象を集める : 選択中のエンティティとその子孫すべて(重複なし)
		std::vector<ECS::Entity> CollectCopyTargets(Engine::ECS::World* a_pWorld, const EditorContext& a_editContext);

	private:

		// コピーしたエンティティ1体分のスナップショット。
		// コピー時点のバイト列をそのまま持つので、コピー元が消えても貼り付けられる。
		struct EntityCopyData
		{
			ECS::Signature sig = {};
			std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>> dataMap = {};

			// コピー元のGUID。貼り付け時に親子関係を貼り直すために使う
			Core::GUID srcGUID = {};
		};

	private:

		// コピー用 : Ctrl+C で詰めて、Ctrl+V のたびに中身から生成する(何度でも貼り付け可)
		std::vector<EntityCopyData> m_copyBufferVec = {};
	};
}