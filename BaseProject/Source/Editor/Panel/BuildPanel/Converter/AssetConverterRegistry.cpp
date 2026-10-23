#include "AssetConverterRegistry.h"

#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

namespace Editor::Build
{
	//======================================================================================
	// コンバーター共通
	//======================================================================================
	bool IAssetConverter::IsConverted(const Engine::Resource::AssetProperty& a_prop) const
	{
		const std::string _ext = GetOutputExtension();
		if (_ext.empty()) return true;

		std::error_code _ec = {};
		return std::filesystem::exists(a_prop.filePath + _ext, _ec);
	}

	namespace
	{
		// そのまま読む形式の種別 : 変換は要らない
		class NoConvertConverter final : public IAssetConverter
		{
		public:
			explicit NoConvertConverter(const char* a_type) : m_type(a_type) {}

			const char* GetType() const override { return m_type; }
			std::string GetOutputExtension() const override { return ""; }
			bool Convert(AssetConvertContext& a_context, std::string& a_outError) const override
			{
				(void)a_context;
				(void)a_outError;
				return true;
			}

		private:
			const char* m_type;
		};
	}

	std::unique_ptr<IAssetConverter> CreateNoConvertConverter(const char* a_type)
	{
		return std::make_unique<NoConvertConverter>(a_type);
	}

	//======================================================================================
	// 登録
	//======================================================================================
	const std::vector<std::unique_ptr<IAssetConverter>>& AssetConverterRegistry::GetAll()
	{
		static const std::vector<std::unique_ptr<IAssetConverter>> s_converterVec = []()
			{
				std::vector<std::unique_ptr<IAssetConverter>> _vec;

				// 変換して .ob* / .dds を書くもの
				_vec.push_back(CreateModelConverter());
				_vec.push_back(CreateTextureConverter());
				_vec.push_back(CreateShaderConverter());
				_vec.push_back(CreateMeshConverter());
				_vec.push_back(CreateMaterialConverter());
				_vec.push_back(CreateAnimationConverter());
				_vec.push_back(CreateAnimatorConverter());
				_vec.push_back(CreateParticlesConverter());
				_vec.push_back(CreateEffectAssetConverter());
				_vec.push_back(CreateAudioBehaviorConverter());
				_vec.push_back(CreateUIMaterialConverter());
				_vec.push_back(CreateUIPresetConverter());
				_vec.push_back(CreatePrefabConverter());
				_vec.push_back(CreateEffectPrefabConverter());
				_vec.push_back(CreateSceneConverter());

				// そのまま読むもの
				_vec.push_back(CreateNoConvertConverter("Sound"));
				_vec.push_back(CreateNoConvertConverter("Font"));
				_vec.push_back(CreateNoConvertConverter("ObjectPrefab"));
				_vec.push_back(CreateNoConvertConverter("RenderingPipelineAsset"));	// JSON 固定で読む

				return _vec;
			}();

		return s_converterVec;
	}

	const IAssetConverter* AssetConverterRegistry::Find(const std::string& a_type)
	{
		for (const auto& _upConverter : GetAll())
		{
			if (a_type == _upConverter->GetType()) return _upConverter.get();
		}
		return nullptr;
	}
}
