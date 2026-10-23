#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/AnimatorAsset/AnimatorAsset.h"

namespace Editor::Build
{
	namespace
	{
		class AnimatorConverter final : public ResourceSaveConverter<Engine::Resource::AnimatorAsset>
		{
		public:
			AnimatorConverter() : ResourceSaveConverter("AnimatorAsset", ".obstet") {}

		protected:
			bool Save(Engine::Resource::AnimatorAsset& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_outError;
				a_resource.Save(a_context.filePath, a_context.resourceManager);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateAnimatorConverter()
	{
		return std::make_unique<AnimatorConverter>();
	}
}
