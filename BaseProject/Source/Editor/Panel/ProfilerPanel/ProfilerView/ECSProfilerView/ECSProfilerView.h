#pragma once

#include "Editor/EditorCommon.h"
#include "../IProfilerView.h"

namespace Engine::Scene
{
	class SceneManager;
}

namespace Engine::ECS
{
	class ECSWorldProfiler;
	struct ECSWorldSnapshot;
}

namespace Editor
{
	/// <summary>
	/// ECSワールドのプロファイラの結果を表示する(ProfilerPanel の ECS 表示)
	///
	/// 計測は ECSWorldProfiler の担当なので、ここは取り直しを頼んで結果を並べるだけ。
	/// ワールドには触らない(プロファイラを付けるのだけはここで行う)
	/// </summary>
	class ECSProfilerView : public IProfilerView
	{
	public:

		const char* GetName() const override { return "ECS"; }
		const char* GetDescription() const override { return "今のシーンのワールドの中身(アーキタイプ・コンポーネント・システム・リソース)"; }
		void Draw(EditorContext& a_editContext) override;

	private:

		// 各タブ
		void DrawOverview(const ECS::ECSWorldSnapshot& a_snapshot);
		void DrawArchetypes(const ECS::ECSWorldSnapshot& a_snapshot);
		void DrawComponents(const ECS::ECSWorldSnapshot& a_snapshot);
		void DrawSystems(const ECS::ECSWorldSnapshot& a_snapshot);
		void DrawResources(const ECS::ECSWorldSnapshot& a_snapshot);

	private:

		// 取り直しを止めて、今の結果を眺める
		bool m_isPaused = false;

		// アーキタイプの絞り込み
		ImGuiTextFilter m_archetypeFilter = {};
		bool m_isHideEmptyArchetype = false;	// エンティティが居ないものを隠す
		bool m_isSortArchetypeByEntity = false;	// エンティティ数の多い順

		// コンポーネントの絞り込み
		bool m_isHideUnusedComponent = false;	// どのアーキタイプも持っていないものを隠す
	};
}
