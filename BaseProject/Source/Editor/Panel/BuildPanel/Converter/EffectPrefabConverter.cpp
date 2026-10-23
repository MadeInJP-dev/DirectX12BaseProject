#include "AssetConverterRegistry.h"

#include "Engine/ECS/World/World.h"
#include "Engine/Resource/Data/EffectPrefab/EffectPrefab.h"

namespace Editor::Build
{
	namespace
	{
		// プレハブと同じく、今のシーンのワールドを借りて書き直す
		class EffectPrefabAssetConverter final : public IAssetConverter
		{
		public:
			const char* GetType() const override { return "EffectPrefab"; }
			std::string GetOutputExtension() const override { return ".obefprfb"; }

			bool Convert(AssetConvertContext& a_context, std::string& a_outError) const override
			{
				if (!a_context.pWorld)
				{
					a_outError = "ワールドがありません(シーンを開いてから変換してください)";
					return false;
				}

				Engine::Resource::EffectPrefab _prefab;
				_prefab.Load(a_context.pWorld, a_context.filePath);
				_prefab.Save(a_context.pWorld, a_context.filePath);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreateEffectPrefabConverter()
	{
		return std::make_unique<EffectPrefabAssetConverter>();
	}
}
