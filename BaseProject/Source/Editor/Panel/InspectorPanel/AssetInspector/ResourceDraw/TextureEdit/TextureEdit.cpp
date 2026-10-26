#include "TextureEdit.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/MainEngine.h"
#include "../../../../../../Editor/Helper/EditorHelper.h"

namespace Editor::Inspector
{
	namespace
	{
		//-----------------------------------------------------------------------------------------
		// 使用フラグを文字列化
		//-----------------------------------------------------------------------------------------
		std::string MakeUsageString(Resource::ETextureUsage a_usage)
		{
			if (a_usage == Resource::ETextureUsage::None) { return "None"; }

			std::string _usageStr;
			for (auto _flag : magic_enum::enum_values<Resource::ETextureUsage>())
			{
				if (_flag == Resource::ETextureUsage::None) { continue; }

				// 立っているフラグのみ連結
				bool _hasFlag = (static_cast<uint32_t>(a_usage) & static_cast<uint32_t>(_flag)) != 0;
				if (!_hasFlag) { continue; }

				if (!_usageStr.empty()) { _usageStr += " | "; }
				_usageStr += magic_enum::enum_name(_flag);
			}
			return _usageStr;
		}
	}

	//-----------------------------------------------------------------------------------------
	// テクスチャの詳細表示
	//-----------------------------------------------------------------------------------------
	void TextureEdit(EditorContext& a_editContext, Resource::Texture* a_pTexture)
	{
		if (!a_pTexture) { return; }

		auto _guid = a_editContext.pAssetProp->guid;

		// 画像の保存
		if (ImGui::Button("Save to DDS"))
		{
			auto _filePath = a_editContext.pServices->pAssetDatabase->GetFilePathFromGUID(_guid);
			a_pTexture->Save(_filePath);
			ENGINE_LOG("テクスチャの保存が完了 : %s", _filePath.c_str());
		}

		Engine::EditorField::Line();

		// ---- リソース情報 ----
		const auto& _desc = a_pTexture->GetDesc();
		Engine::EditorField::Value("Name", "%s", a_pTexture->GetName().c_str());
		Engine::EditorField::Value("Size", "%llu x %u", _desc.Width, _desc.Height);
		Engine::EditorField::Value("MipLevels", "%u", static_cast<UINT>(_desc.MipLevels));
		Engine::EditorField::Value("ArraySize", "%u", static_cast<UINT>(_desc.DepthOrArraySize));
		Engine::EditorField::Value("Format", "%s", std::string(magic_enum::enum_name(_desc.Format)).c_str());
		Engine::EditorField::Value("SampleCount", "%u", _desc.SampleDesc.Count);
		Engine::EditorField::Value("Usage", "%s", MakeUsageString(a_pTexture->GetUsage()).c_str());

		const auto& _clearColor = a_pTexture->GetClearColor();
		Engine::EditorField::Value("ClearColor", "%.3f, %.3f, %.3f, %.3f", _clearColor.r, _clearColor.g, _clearColor.b, _clearColor.a);

		Engine::EditorField::Line();

		// ---- 画像の描画 ----
		const auto* _pGraphicsEngine = a_editContext.pServices->pMainEngine->RefGraphicsEngine();
		auto _gpuHandle = EditorHelper::GetImGuiTexHandle(a_pTexture->GetImGuiSRV());
		EditorHelper::DrawSRVView(_gpuHandle, static_cast<float>(_pGraphicsEngine->GetRenderWidth()), static_cast<float>(_pGraphicsEngine->GetRenderHeight()));
	}
}
