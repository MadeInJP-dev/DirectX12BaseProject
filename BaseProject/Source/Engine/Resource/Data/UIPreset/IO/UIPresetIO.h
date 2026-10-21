#pragma once

namespace Engine::Resource
{
	class AssetDatabase;

	class UIPresetIO
	{
	public:

		// 置き場(Asset からの相対ではなく、作業ディレクトリからのパス)
		static constexpr const char* ASSET_DIR = "Asset/UIPreset/";

		/// <summary>
		/// ファイルパスからの読み込み
		/// </summary>
		static UIPreset LoadFromFile(const std::string& a_path);

		/// <summary>
		/// 作成 : 中身を入れてファイルを書き出し、GUID を返す
		/// </summary>
		/// <param name="a_path">Asset/UIPreset/ 以下のディレクトリ名(空なら直下)</param>
		/// <param name="a_name">ファイルとプリセットの名前</param>
		/// <param name="a_decorationJson">飾り1つぶんの JSON(空なら既定の飾り)</param>
		/// <returns>作ったプリセットの GUID(同じ名前が既にあれば、そちらの GUID を返して書き換えない)</returns>
		/// <remarks>
		/// 書き出す前にメタファイルを作って GUID を決める。
		/// 作った飾りがすぐに指せるよう、AssetDatabase の監視を待たない
		/// </remarks>
		static Core::GUID Create(
			AssetDatabase& a_assetDB,
			const std::string& a_path,
			const std::string& a_name,
			const std::string& a_decorationJson = "");
	};
}
