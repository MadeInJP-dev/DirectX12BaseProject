#include "AssetConverterRegistry.h"

namespace Editor::Build
{
	namespace
	{
		// シーンは組み立てに読み込みの全工程が要るので、ここでは書き出さない。
		// .obscene はエディターのシーン保存(.ojscene と一緒に書かれる)で作る
		class SceneAssetConverter final : public IAssetConverter
		{
		public:
			const char* GetType() const override { return "Scene"; }
			std::string GetOutputExtension() const override { return ".obscene"; }

			bool Convert(AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_context;
				a_outError = "シーンは開いてエディターで保存すると .obscene が書かれます";
				return false;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateSceneConverter()
	{
		return std::make_unique<SceneAssetConverter>();
	}
}
