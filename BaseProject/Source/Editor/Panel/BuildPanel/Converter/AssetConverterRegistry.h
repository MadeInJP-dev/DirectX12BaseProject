#pragma once

#include "IAssetConverter.h"

namespace Editor::Build
{
	//==========================================================================================
	// アセット種別からコンバーターを引く
	//==========================================================================================
	class AssetConverterRegistry
	{
	public:

		// 種別のコンバーターを返す。無ければ nullptr
		static const IAssetConverter* Find(const std::string& a_type);

		// 登録されているコンバーター全部
		static const std::vector<std::unique_ptr<IAssetConverter>>& GetAll();
	};

	//------------------------------------------------------------------------------------------
	// 種別ごとのコンバーターの作り手(実装は種別ごとの .cpp)
	//------------------------------------------------------------------------------------------
	std::unique_ptr<IAssetConverter> CreateModelConverter();
	std::unique_ptr<IAssetConverter> CreateTextureConverter();
	std::unique_ptr<IAssetConverter> CreateShaderConverter();
	std::unique_ptr<IAssetConverter> CreateMeshConverter();
	std::unique_ptr<IAssetConverter> CreateMaterialConverter();
	std::unique_ptr<IAssetConverter> CreateAnimationConverter();
	std::unique_ptr<IAssetConverter> CreateAnimatorConverter();
	std::unique_ptr<IAssetConverter> CreateParticlesConverter();
	std::unique_ptr<IAssetConverter> CreateEffectAssetConverter();
	std::unique_ptr<IAssetConverter> CreateAudioBehaviorConverter();
	std::unique_ptr<IAssetConverter> CreateUIMaterialConverter();
	std::unique_ptr<IAssetConverter> CreateUIPresetConverter();
	std::unique_ptr<IAssetConverter> CreatePrefabConverter();
	std::unique_ptr<IAssetConverter> CreateEffectPrefabConverter();
	std::unique_ptr<IAssetConverter> CreateSceneConverter();

	// 変換の要らない種別(そのまま読む形式) : 種別名を渡す
	std::unique_ptr<IAssetConverter> CreateNoConvertConverter(const char* a_type);
}
