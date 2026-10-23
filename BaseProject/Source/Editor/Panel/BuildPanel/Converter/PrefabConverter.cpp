#include "AssetConverterRegistry.h"

#include "Engine/ECS/World/World.h"
#include "Engine/Resource/Data/Prefab/Prefab.h"

namespace Editor::Build
{
	namespace
	{
		// プレハブはコンポーネント名をワールドのメタ情報から引くので、今のシーンのワールドを借りて書き直す
		class PrefabAssetConverter final : public IAssetConverter
		{
		public:
			const char* GetType() const override { return "Prefab"; }
			std::string GetOutputExtension() const override { return ".obprfb"; }

			bool Convert(AssetConvertContext& a_context, std::string& a_outError) const override
			{
				if (!a_context.pWorld)
				{
					a_outError = "ワールドがありません(シーンを開いてから変換してください)";
					return false;
				}

				Engine::Resource::Prefab _prefab;
				_prefab.Load(a_context.pWorld, a_context.filePath);
				_prefab.Save(a_context.pWorld, a_context.filePath);
				return true;
			}
		};
	}

	std::unique_ptr<IAssetConverter> CreatePrefabConverter()
	{
		return std::make_unique<PrefabAssetConverter>();
	}
}
