#include "UIPresetEdit.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

#include "Application/Object/UI/Decoration/Decoration.h"
#include "Application/Object/UI/Decoration/DecorationDraw.h"

namespace Editor::Inspector
{
	namespace
	{
		//-------------------------------------------------------------------------------------
		// 編集中の飾り
		//
		// プリセットの中身は JSON の文字列なので、開いている間だけ飾りへ戻して欄に出す。
		// 別のプリセットを開いたとき・番号が進んだとき(UI 側の EditPreset で書き戻されたなど)は読み直す
		//-------------------------------------------------------------------------------------
		struct EditingPreset
		{
			Core::GUID guid = {};
			uint32_t revision = 0;
			App::Object::Decoration::Decoration decoration = {};
		};

		EditingPreset s_editing = {};

		// テクスチャ・フォントの参照を外す。
		// 静的な寿命の飾りが握ったままだと、リソースマネージャーより後まで残ってしまう
		void ReleaseResourceRefs(App::Object::Decoration::Decoration& a_decoration)
		{
			if (auto* _pImage = a_decoration.RefImage()) _pImage->texRef = {};
			if (auto* _pText = a_decoration.RefText()) _pText->fontRef = {};
		}
	}

	void UIPresetEdit(EditorContext& a_editContext, Resource::UIPreset* a_pPreset)
	{
		if (!a_pPreset) { return; }

		const auto _guid = a_editContext.pAssetProp->guid;
		auto& _services = *a_editContext.pServices;

		if (!(s_editing.guid == _guid) || s_editing.revision != a_pPreset->revision)
		{
			App::Object::Decoration::DeserializePresetDecoration(a_pPreset->decorationJson, s_editing.decoration);
			s_editing.guid = _guid;
			s_editing.revision = a_pPreset->revision;
		}

		Engine::EditorField::Value("UI Preset", "%s", a_pPreset->name.c_str());

		if (a_pPreset->isDirty) Engine::EditorField::WarningText("保存されていません");

		// 保存ボタン
		if (Engine::EditorField::Button("Save Asset"))
		{
			auto _filePath = _services.pAssetDatabase->GetFilePathFromGUID(_guid);
			a_pPreset->Save(_filePath);
			ENGINE_LOG("Save UIPreset : %s", _filePath.c_str());
		}

		Engine::EditorField::Line();
		Engine::EditorField::HelpText("直すと、このプリセットを指している飾りの「付いていく部分」がすぐに変わります");
		Engine::EditorField::HelpText("位置・回転・Z順・名前・表示は、指している飾りの値のままです");

		// テクスチャの絵(プレビュー)は出さない : 参照を握り続けないため、描き終えたら外している
		if (App::Object::Decoration::DrawDecorationInspector(s_editing.decoration, _services, false))
		{
			a_pPreset->SetDecorationJson(App::Object::Decoration::SerializePresetDecoration(s_editing.decoration));
			s_editing.revision = a_pPreset->revision;
		}

		ReleaseResourceRefs(s_editing.decoration);
	}
}
