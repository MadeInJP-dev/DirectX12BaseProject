#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Data/Shader/Shader.h"

namespace Editor::Build
{
	namespace
	{
		class ShaderConverter final : public ResourceSaveConverter<Engine::Resource::Shader>
		{
		public:
			ShaderConverter() : ResourceSaveConverter("Shader", ".cso") {}

		protected:
			bool Save(Engine::Resource::Shader& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				// 読み込んだ時点で .cso のキャッシュが書かれる。ここでは書き出されたかだけを見る
				(void)a_resource;
				std::error_code _ec = {};
				if (!std::filesystem::exists(a_context.prop.filePath + ".cso", _ec))
				{
					a_outError = ".cso が書き出されませんでした";
					return false;
				}
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateShaderConverter()
	{
		return std::make_unique<ShaderConverter>();
	}
}
