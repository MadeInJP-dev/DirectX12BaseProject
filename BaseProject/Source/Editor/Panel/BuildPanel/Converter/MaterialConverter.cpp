#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/Material/Material.h"

namespace Editor::Build
{
	namespace
	{
		class MaterialConverter final : public ResourceSaveConverter<Engine::Resource::Material>
		{
		public:
			MaterialConverter() : ResourceSaveConverter("Material", ".obmtrl") {}

		protected:
			bool Save(Engine::Resource::Material& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_outError;
				Engine::Persistence::Archive _ar(Engine::Persistence::Archive::EMode::Save, a_context.fileDir, a_context.fileName, "mtrl");
				a_resource.Archive(_ar);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateMaterialConverter()
	{
		return std::make_unique<MaterialConverter>();
	}
}
