#pragma once
namespace Engine::Resource
{
	class ModelIO
	{
	public:
		// ---- Input ----
		/// <summary>
		/// モデルの読み込み
		/// </summary>
		/// <param name="a_filePath">モデルファイルパス</param>
		/// <param name="a_pContext">
		/// ビルドコンテキスト。
		/// 省略した場合はモデル1体分のバッチをここで開く。
		/// </param>
		static Model Import(const std::string& a_filePath, const ResourceBuildContext* a_pContext = nullptr);

	private:
		// 独自形式の読み込み処理
		static Model Load(const ResourceBuildContext& a_ctx, const std::string& a_filePath);
		// GLTFのロード
		static Model GLTFLoad(const ResourceBuildContext& a_ctx, const std::string& a_filePath);



		// ランタイムデータの作成
		static void CreateDrawCmd(const ResourceBuildContext& a_ctx, const ModelAssetData& a_modelAssetData, ModelRuntimeData& a_runtimeData);

		// LOD モデルを読む(LOD として読まれているモデルは読まない)
		static void LoadLodModels(const ResourceBuildContext& a_ctx, const std::string& a_filePath, const ModelAssetData& a_modelAssetData, ModelRuntimeData& a_runtimeData);

		// 優先順位の高い拡張子を検索
		static std::string FinddExtension(const std::vector<std::string>& a_extVed);
	};
}
