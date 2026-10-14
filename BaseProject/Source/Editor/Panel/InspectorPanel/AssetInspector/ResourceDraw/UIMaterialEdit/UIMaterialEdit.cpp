#include "UIMaterialEdit.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

namespace Editor::Inspector
{
	//-----------------------------------------------------------------------------------------
	// UIマテリアルの編集・詳細表示
	//
	// 進み具合(ディゾルブがどれだけ進んでいるか)はマテリアルには無い。
	// 確かめるときは、マテリアルを付けた UI のインスペクターで Transition に Dissolve を入れて流す
	//-----------------------------------------------------------------------------------------
	void UIMaterialEdit(EditorContext& a_editContext, Resource::UIMaterial* a_pMaterial)
	{
		if (!a_pMaterial) { return; }

		const auto _guid = a_editContext.pAssetProp->guid;
		auto& _services = *a_editContext.pServices;

		Engine::EditorField::Value("UI Material", "%s", a_pMaterial->name.c_str());
		Engine::EditorField::Line();

		// 保存ボタン
		if (Engine::EditorField::Button("Save Asset"))
		{
			auto _filePath = _services.pAssetDatabase->GetFilePathFromGUID(_guid);
			a_pMaterial->Save(_filePath);
			ENGINE_LOG("Save UIMaterial : %s", _filePath.c_str());
		}

		//------------------------------------------------------------------
		// 効果
		//------------------------------------------------------------------
		Engine::EditorField::Header("Effect");
		Engine::EditorField::Field("Effect", a_pMaterial->effect);
		Engine::EditorField::Tooltip("UIPS に組み込みの効果から選ぶ");

		if (a_pMaterial->effect == Graphics::EUIEffect::Dissolve)
		{
			//--------------------------------------------------------------
			// ノイズ : 消え方の模様
			//--------------------------------------------------------------
			Engine::EditorField::Header("Noise");
			if (Engine::EditorField::AssetField(_services, "NoiseTexture", "Texture", a_pMaterial->noiseTexGUID))
			{
				a_pMaterial->ResolveReferences(*_services.pResourceManager);
			}
			Engine::EditorField::Tooltip("赤(r)の値が小さいところから消える。空ならシェーダー内のノイズを使う");

			Engine::EditorField::Field("NoisePixel", a_pMaterial->noisePixel, 1.0f, 1.0f, 4096.0f);
			Engine::EditorField::Tooltip("ノイズ1枚ぶんの大きさ(px)。小さいほど細かく崩れる");

			//--------------------------------------------------------------
			// 縁 : 消えかけのところに乗せる色
			//--------------------------------------------------------------
			Engine::EditorField::Header("Edge");
			Engine::EditorField::Field("EdgeWidth", a_pMaterial->edgeWidth, 0.005f, 0.0f, 1.0f);
			Engine::EditorField::Tooltip("縁の幅(ノイズの値の幅)。0 で縁なし");
			Engine::EditorField::ColorField("EdgeColor", a_pMaterial->edgeColor);
			Engine::EditorField::Tooltip("a は乗せる強さ");
		}

		//------------------------------------------------------------------
		// ポストプロセス
		//------------------------------------------------------------------
		Engine::EditorField::Header("PostEffect");
		Engine::EditorField::Field("ScanLine", a_pMaterial->isScanLine);
		Engine::EditorField::Tooltip("このマテリアルを付けた UI にスキャンライン(ScanLinePass)を掛ける");

		Engine::EditorField::Line();
		Engine::EditorField::HelpText("進み具合は UI の Transition(Show / Hide / Pulse)の Dissolve で動かします");
	}
}
