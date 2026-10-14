#include "UIMaterial.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

namespace Engine::Resource
{
	void UIMaterial::Archive(Persistence::Archive& a_ar)
	{
		a_ar.StringField("Name", name);
		a_ar.Field("Effect", effect);
		a_ar.GUIDField("NoiseTexGUID", noiseTexGUID);
		a_ar.Field("NoisePixel", noisePixel);
		a_ar.Field("EdgeWidth", edgeWidth);
		a_ar.Field("EdgeColor", edgeColor);
		a_ar.Field("IsScanLine", isScanLine);
	}

	void UIMaterial::Save(const std::string& a_baseFilePath)
	{
		auto _fileDir = Core::File::GetDirFromPath(a_baseFilePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_baseFilePath);

		Persistence::Archive _ar(Persistence::Archive::EMode::Save, _fileDir, _fileName, "uimat");
		Archive(_ar);
	}

	void UIMaterial::ResolveReferences(ResourceManager& a_resourceManager)
	{
		// 届くまでの間はシェーダー内のノイズで代わりにするので待たない
		noiseTexRef = noiseTexGUID.IsValid()
			? a_resourceManager.RequestLoad<Texture>(noiseTexGUID)
			: ResourceRef<Texture>();
	}
}
