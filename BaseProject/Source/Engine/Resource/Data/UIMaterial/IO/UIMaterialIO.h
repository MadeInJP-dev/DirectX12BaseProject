#pragma once

namespace Engine::Resource
{
	class AssetDatabase;
	class ResourceManager;

	class UIMaterialIO
	{
	public:

		/// <summary>
		/// ファイルパスからの読み込み
		/// </summary>
		/// <param name="a_path">ファイルパス</param>
		/// <param name="a_resourceManager">ノイズのテクスチャを引き当てる先</param>
		/// <returns>実体を返す</returns>
		static UIMaterial LoadFromFile(const std::string& a_path, ResourceManager& a_resourceManager);

		/// <summary>
		/// 作成 : 既定の設定でファイルを書き出す(メタファイルは AssetDatabase の監視が作る)
		/// </summary>
		/// <param name="a_path">Asset/UIMaterial/ 以下のディレクトリ名</param>
		/// <param name="a_name">ファイルとマテリアルの名前</param>
		static void Create(AssetDatabase& a_assetDB, const std::string& a_path, const std::string& a_name);
	};
}
