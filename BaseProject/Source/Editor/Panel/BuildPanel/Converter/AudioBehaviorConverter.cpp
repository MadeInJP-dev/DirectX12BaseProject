#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/AudioBehavior/AudioBehavior.h"

namespace Editor::Build
{
	namespace
	{
		class AudioBehaviorConverter final : public ResourceSaveConverter<Engine::Resource::AudioBehavior>
		{
		public:
			AudioBehaviorConverter() : ResourceSaveConverter("AudioBehavior", ".obaudbhv") {}

		protected:
			bool Save(Engine::Resource::AudioBehavior& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_outError;
				a_resource.Save(a_context.filePath);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateAudioBehaviorConverter()
	{
		return std::make_unique<AudioBehaviorConverter>();
	}
}
