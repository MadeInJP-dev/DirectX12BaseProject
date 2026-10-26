#include "SourceDirectoryWrite.h"

#include "../../BuildContext.h"
#include "../../BuildSettings.h"
#include "../BuildPlan/BuildPlan.h"

namespace Editor::Build
{
	bool SourceDirectoryWrite::Build(const BuildContext& a_context, BuildPlan& a_plan)
	{
		bool _isSuccess = true;

		if (!CopySource(a_context, a_plan))
		{
			a_plan.AddWarning("ソースディレクトリが見つかりません : " + Core::File::ToUTF8(a_context.srcSourceDirectory));
			_isSuccess = false;
		}

		if (!CopyShader(a_context, a_plan))
		{
			a_plan.AddWarning("シェーダーのディレクトリが見つかりません : " + Core::File::ToUTF8(a_context.srcShaderDirectory));
			_isSuccess = false;
		}

		if (a_context.pSettings->isWriteLibrary && !CopyLibrary(a_context, a_plan))
		{
			a_plan.AddWarning("ライブラリのディレクトリが見つかりません : " + Core::File::ToUTF8(a_context.srcLibraryDirectory));
			_isSuccess = false;
		}

		return _isSuccess;
	}

	bool SourceDirectoryWrite::CopySource(const BuildContext& a_context, BuildPlan& a_plan)
	{
		// フォルダ名はコピー元のまま(vcxproj が Source\〜 で持っている)
		const std::filesystem::path _dstSource = a_context.sourceDirectory / a_context.srcSourceDirectory.filename();
		if (!a_plan.AddCopyDirectory(a_context.srcSourceDirectory, _dstSource)) return false;

		// ソリューションとプロジェクト設定 : 作業ディレクトリ(プロジェクトのフォルダ)の直下にあるもの
		for (const auto& _file : a_context.pSettings->projectFileVec)
		{
			const std::filesystem::path _path = Core::File::FromUTF8(_file);
			if (!a_plan.AddCopyFile(_path, a_context.sourceDirectory / _path.filename()))
			{
				a_plan.AddWarning("プロジェクトのファイルが見つかりません : " + _file);
			}
		}
		return true;
	}

	bool SourceDirectoryWrite::CopyLibrary(const BuildContext& a_context, BuildPlan& a_plan)
	{
		return a_plan.AddCopyDirectory(
			a_context.srcLibraryDirectory,
			a_context.libraryDirectory,
			{},
			a_context.pSettings->libraryExcludeDirectoryVec);
	}

	bool SourceDirectoryWrite::CopyShader(const BuildContext& a_context, BuildPlan& a_plan)
	{
		// 置き場はコピー元と同じ相対位置(Asset/Shader/〜)
		return a_plan.AddCopyDirectory(
			a_context.srcShaderDirectory,
			a_context.sourceDirectory / a_context.srcShaderDirectory.relative_path(),
			a_context.pSettings->shaderSourceExtensionVec);
	}
}
