#pragma once

#include "Editor/EditorCommon.h"

namespace Editor::Build
{
	struct BuildContext;
	class BuildPlan;

	//============================================================================
	// "ソース" フォルダ : 受け取った側がビルドできる一式
	//
	// <名前>/ソース/   Source/・ソリューションとプロジェクト・.editorconfig・シェーダーのソース
	// <名前>/Library/ ライブラリ(vcxproj は ..\Library を見る)
	//
	// 書き出す予定を BuildPlan に積むだけ。実際に写すのは BuildPlan::Execute
	//============================================================================
	class SourceDirectoryWrite
	{
	public:

		// ソースフォルダ(とライブラリ)の書き出しを積む
		bool Build(const BuildContext& a_context, BuildPlan& a_plan);

	private:

		// Source 以下と、直下に置くプロジェクトのファイル
		bool CopySource(const BuildContext& a_context, BuildPlan& a_plan);

		// ライブラリ
		bool CopyLibrary(const BuildContext& a_context, BuildPlan& a_plan);

		// シェーダーのソース : vcxproj が FxCompile で持っているので Asset/Shader/ に .hlsl / .hlsli を置く
		bool CopyShader(const BuildContext& a_context, BuildPlan& a_plan);
	};
}
