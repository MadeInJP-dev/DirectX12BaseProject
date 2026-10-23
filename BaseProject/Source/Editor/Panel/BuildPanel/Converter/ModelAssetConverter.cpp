#include "AssetConverterRegistry.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Data/Model/IO/ModelConverter/ModelConverter.h"

namespace Editor::Build
{
	namespace
	{
		// gltf などを .obmdl にし、配下のメッシュ・マテリアル・アニメーションも .ob* で書き出す
		class ModelAssetConverter final : public IAssetConverter
		{
		public:
			const char* GetType() const override { return "Model"; }
			std::string GetOutputExtension() const override { return ".obmdl"; }

			bool Convert(AssetConvertContext& a_context, std::string& a_outError) const override
			{
				if (!Engine::Resource::Converter::ModelConverter::ConvertModelDataToBinary(
					a_context.resourceManager, a_context.prop.guid))
				{
					a_outError = "モデルのコンバートに失敗しました";
					return false;
				}
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateModelConverter()
	{
		return std::make_unique<ModelAssetConverter>();
	}
}
