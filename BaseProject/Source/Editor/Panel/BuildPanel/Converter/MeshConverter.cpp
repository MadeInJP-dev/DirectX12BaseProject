#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/Mesh/Mesh.h"

namespace Editor::Build
{
	namespace
	{
		class MeshConverter final : public ResourceSaveConverter<Engine::Resource::Mesh>
		{
		public:
			MeshConverter() : ResourceSaveConverter("Mesh", ".obmesh") {}

		protected:
			bool Save(Engine::Resource::Mesh& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_outError;
				a_resource.Save(a_context.fileDir, a_context.fileName);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateMeshConverter()
	{
		return std::make_unique<MeshConverter>();
	}
}
