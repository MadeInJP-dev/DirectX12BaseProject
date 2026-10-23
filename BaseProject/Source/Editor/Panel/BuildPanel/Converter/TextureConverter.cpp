#include "AssetConverterRegistry.h"
#include "ResourceSaveConverter.h"

#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Data/Texture/Texture.h"

namespace Editor::Build
{
	namespace
	{
		// png などの元画像を、ミップマップ付きのブロック圧縮 DDS にする
		class TextureConverter final : public ResourceSaveConverter<Engine::Resource::Texture>
		{
		public:
			TextureConverter() : ResourceSaveConverter("Texture", ".dds") {}

		protected:
			bool Save(Engine::Resource::Texture& a_resource, const AssetConvertContext& a_context, std::string& a_outError) const override
			{
				const auto& _extVec = a_context.prop.extensionsVec;

				// 元画像を探す。.dds しか無いものは、すでに製品用の形
				static constexpr const char* SOURCE_EXT[] = { ".png", ".jpg", ".tga" };
				for (const char* _ext : SOURCE_EXT)
				{
					if (std::find(_extVec.begin(), _extVec.end(), _ext) == _extVec.end()) continue;

					a_resource.Save(a_context.prop.filePath + _ext);
					return true;
				}

				if (std::find(_extVec.begin(), _extVec.end(), ".dds") != _extVec.end()) return true;

				a_outError = "元画像(.png / .jpg / .tga)がありません";
				return false;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateTextureConverter()
	{
		return std::make_unique<TextureConverter>();
	}
}
