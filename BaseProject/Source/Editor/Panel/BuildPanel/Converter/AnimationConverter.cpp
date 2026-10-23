#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/Animation/Animation.h"

namespace Editor::Build
{
	namespace
	{
		class AnimationConverter final : public ResourceSaveConverter<Engine::Resource::AnimationData>
		{
		public:
			AnimationConverter() : ResourceSaveConverter("Animation", ".obanim") {}

		protected:
			bool Save(Engine::Resource::AnimationData& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_outError;
				a_resource.Save(a_context.fileDir, a_context.fileName);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateAnimationConverter()
	{
		return std::make_unique<AnimationConverter>();
	}
}
