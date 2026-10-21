#pragma once

#include "../../../../../Internal/EditorContext.h"
#include "Editor/EditorCommon.h"

namespace Editor::Inspector
{
	/// <summary>
	/// UIプリセットの編集・詳細表示
	/// </summary>
	/// <remarks>
	/// 飾り1つぶんの見た目を、UI の飾りと同じ欄で直す。
	/// 直すとすぐに番号が進み、このプリセットを指している飾りが全部写し直す(保存は Save Asset)
	/// </remarks>
	/// <param name="a_editContext">エディターコンテキスト</param>
	/// <param name="a_pPreset">編集対象のUIプリセット</param>
	void UIPresetEdit(EditorContext& a_editContext, Resource::UIPreset* a_pPreset);
}
