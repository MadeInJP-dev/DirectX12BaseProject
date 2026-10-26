#pragma once

#include "Editor/EditorCommon.h"

namespace Engine::Resource
{
	struct AssetProperty;
}

namespace Editor::Build
{
	struct BuildContext;
	class BuildPlan;
	class IAssetConverter;

	//============================================================================
	// "実行" フォルダ : exe を置けばそのまま遊べる一式
	//
	// <名前>/実行/  exe・dll・説明書など・Asset/
	//
	// Asset/ には製品が読む形(バイナリ)と .assetmeta だけを置く。
	// 開発用の JSON(.oj*)や元データ(.gltf / .png / .hlsl など)は置かない。
	// 例外は JSON 固定で読むもの(BuildSettings::jsonKeepExtensionVec)と、
	// 変換の要らない種別(.wav / .ttf など、そのまま読むもの)。
	//
	// 書き出す予定を BuildPlan に積むだけ。実際に写すのは BuildPlan::Execute
	//============================================================================
	class ExecuteDirectoryWrite
	{
	public:

		// 実行フォルダの書き出しを積む
		bool Build(const BuildContext& a_context, BuildPlan& a_plan);

	private:

		// 実行ファイルと dll、一緒に置くファイル
		bool CopyExe(const BuildContext& a_context, BuildPlan& a_plan);

		// アセットの書き出し : バイナリとメタデータのみ
		void CopyAsset(const BuildContext& a_context, BuildPlan& a_plan);

		// 起動時に読む設定(Asset/Data) : アセットデータベースの外なのでフォルダごと見る
		void CopyData(const BuildContext& a_context, BuildPlan& a_plan);

		// 実行フォルダへ置く拡張子を選ぶ。置けるものが無ければ空
		std::vector<std::string> SelectRuntimeExtensions(
			const BuildContext& a_context,
			const Engine::Resource::AssetProperty& a_prop,
			const IAssetConverter& a_converter
		) const;

		// .assetmeta を置く : 中身は元のまま、Files だけ置いたものに合わせる
		void WriteMetaFile(
			const BuildContext& a_context,
			const Engine::Resource::AssetProperty& a_prop,
			const std::vector<std::string>& a_extensionVec,
			BuildPlan& a_plan
		);

		// エンジン設定を置く : ビルドモードを書き換える
		void WriteEngineData(const BuildContext& a_context, const std::filesystem::path& a_src, BuildPlan& a_plan);
	};
}
