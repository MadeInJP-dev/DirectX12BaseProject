#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/UIMaterial/UIMaterial.h"

namespace Editor::Build
{
	namespace
	{
		class UIMaterialConverter final : public ResourceSaveConverter<Engine::Resource::UIMaterial>
		{
		public:
			UIMaterialConverter() : ResourceSaveConverter("UIMaterial", ".obuimat") {}

		protected:
			bool Save(Engine::Resource::UIMaterial& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_outError;
				a_resource.Save(a_context.filePath);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateUIMaterialConverter()
	{
		return std::make_unique<UIMaterialConverter>();
	}
}
