#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/UIPreset/UIPreset.h"

namespace Editor::Build
{
	namespace
	{
		class UIPresetConverter final : public ResourceSaveConverter<Engine::Resource::UIPreset>
		{
		public:
			UIPresetConverter() : ResourceSaveConverter("UIPreset", ".obuipreset") {}

		protected:
			bool Save(Engine::Resource::UIPreset& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_outError;
				a_resource.Save(a_context.filePath);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateUIPresetConverter()
	{
		return std::make_unique<UIPresetConverter>();
	}
}
