#pragma once

#include "../../../../../Internal/EditorContext.h"
#include "Editor/EditorCommon.h"

namespace Editor::Inspector
{
	/// <summary>
	/// UIマテリアルの編集・詳細表示
	/// 効果の種類・ノイズ・縁の色などを編集する(読み込み済みの実体を直接書き換えるので、置いてあるUIへすぐ効く)
	/// </summary>
	/// <param name="a_editContext">エディターコンテキスト</param>
	/// <param name="a_pMaterial">編集対象のUIマテリアル</param>
	void UIMaterialEdit(EditorContext& a_editContext, Resource::UIMaterial* a_pMaterial);
}
