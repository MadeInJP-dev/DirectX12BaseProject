#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/Particles/ParticlesAsset.h"

namespace Editor::Build
{
	namespace
	{
		class ParticlesConverter final : public ResourceSaveConverter<Engine::Resource::ParticlesAsset>
		{
		public:
			ParticlesConverter() : ResourceSaveConverter("ParticlesAsset", ".obptic") {}

		protected:
			bool Save(Engine::Resource::ParticlesAsset& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_outError;
				a_resource.Save(a_context.filePath);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateParticlesConverter()
	{
		return std::make_unique<ParticlesConverter>();
	}
}
