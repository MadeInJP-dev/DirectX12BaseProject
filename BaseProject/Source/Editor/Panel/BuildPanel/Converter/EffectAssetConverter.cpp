#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"

namespace Editor::Build
{
	namespace
	{
		class EffectAssetConverter final : public ResourceSaveConverter<Engine::Resource::EffectAsset>
		{
		public:
			EffectAssetConverter() : ResourceSaveConverter("EffectAsset", ".obeffect") {}

		protected:
			bool Save(Engine::Resource::EffectAsset& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_outError;
				a_resource.Save(a_context.filePath);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateEffectAssetConverter()
	{
		return std::make_unique<EffectAssetConverter>();
	}
}
