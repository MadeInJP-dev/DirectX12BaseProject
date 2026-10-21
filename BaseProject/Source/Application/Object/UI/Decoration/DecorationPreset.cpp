#include "Application/Object/UI/Decoration/Decoration.h"
#include "Application/Object/UI/Decoration/DecorationDraw.h"

#include <nlohmannJSON/json.hpp>

#include "Engine/ECS/System/SystemContext.h"	// EngineServices
#include "Engine/EditorField/EditorField.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Data/UIPreset/IO/UIPresetIO.h"

namespace App::Object::Decoration
{
	namespace
	{
		// 中身(文字列・画像のテクスチャ)を a_src から a_dst へ写す。種類が違えば何もしない
		void CopyContent(DecorationBody& a_dst, const DecorationBody& a_src)
		{
			if (TextData* _pDst = std::get_if<TextData>(&a_dst))
			{
				if (const TextData* _pSrc = std::get_if<TextData>(&a_src)) _pDst->text = _pSrc->text;
				return;
			}
			if (ImageData* _pDst = std::get_if<ImageData>(&a_dst))
			{
				if (const ImageData* _pSrc = std::get_if<ImageData>(&a_src))
				{
					_pDst->texGUID = _pSrc->texGUID;
					_pDst->texRef = _pSrc->texRef;
				}
			}
		}
	}

	//======================================================================================
	// 写す
	//--------------------------------------------------------------------------------------
	// 見た目は種類ごと差し替える(枠と UV・フォントと組み方は種類の中身に入っているため)。
	// 中身に付いていかないときは、差し替える前の中身(文字列・テクスチャ)を戻す。
	// 種類が変わったときは戻す先が無いので、プリセットの中身のまま
	//======================================================================================
	void ApplyPresetParts(Decoration& a_dst, const Decoration& a_preset, EPresetPart a_parts)
	{
		if (Core::HasFlag(a_parts, EPresetPart::LOOK))
		{
			const DecorationBody _keep = a_dst.body;

			a_dst.color = a_preset.color;
			a_dst.body = a_preset.body;

			if (!Core::HasFlag(a_parts, EPresetPart::CONTENT)) CopyContent(a_dst.body, _keep);
		}
		else if (Core::HasFlag(a_parts, EPresetPart::CONTENT))
		{
			CopyContent(a_dst.body, a_preset.body);
		}

		if (Core::HasFlag(a_parts, EPresetPart::SIZE))
		{
			a_dst.pixelSize = a_preset.pixelSize;
			a_dst.scale = a_preset.scale;
			a_dst.pivot = a_preset.pivot;
		}

		if (Core::HasFlag(a_parts, EPresetPart::REACTION))
		{
			a_dst.opReaction = a_preset.opReaction;
		}

		if (Core::HasFlag(a_parts, EPresetPart::ANIMATION))
		{
			a_dst.opTweenAnim = a_preset.opTweenAnim;
			a_dst.opOscillationAnim = a_preset.opOscillationAnim;
		}
	}

	//======================================================================================
	// プリセットの中身の読み書き
	//--------------------------------------------------------------------------------------
	// 飾りの保存(ArchiveDecoration)をメモリ上の JSON へ通す。
	// プリセットの中の飾りは何も指さない(プリセットがプリセットを指すと、写す順が決まらない)
	//======================================================================================
	std::string SerializePresetDecoration(const Decoration& a_decoration)
	{
		Decoration _copy = a_decoration;
		_copy.preset = PresetLink{};

		nlohmann::json _json = {};
		{
			Engine::Persistence::Archive _ar(Engine::Persistence::Archive::EMode::Save, _json);
			ArchiveDecoration(_ar, _copy);
		}
		return _json.dump();
	}

	bool DeserializePresetDecoration(const std::string& a_json, Decoration& a_outDecoration)
	{
		a_outDecoration = Decoration{};
		if (a_json.empty()) return true;

		nlohmann::json _json = nlohmann::json::parse(a_json, nullptr, false);
		if (_json.is_discarded() || !_json.is_object()) return false;

		Engine::Persistence::Archive _ar(Engine::Persistence::Archive::EMode::Load, _json);
		ArchiveDecoration(_ar, a_outDecoration);
		a_outDecoration.preset = PresetLink{};
		return true;
	}

	//======================================================================================
	// つながり
	//======================================================================================
	void RequestPreset(Decoration& a_decoration, Engine::Resource::ResourceManager* a_pResourceManager)
	{
		PresetLink& _link = a_decoration.preset;
		_link.isApplied = false;
		_link.isRequested = true;

		if (!a_pResourceManager || !_link.IsLinked())
		{
			_link.presetRef = {};
			return;
		}
		_link.presetRef = a_pResourceManager->RequestLoad<Engine::Resource::UIPreset>(_link.presetGUID);
	}

	bool SyncPreset(Decoration& a_decoration, Engine::Resource::ResourceManager* a_pResourceManager)
	{
		PresetLink& _link = a_decoration.preset;
		if (!_link.IsLinked() || !a_pResourceManager) return false;

		// 読み込み直後はまだ頼んでいない : ここで頼む(見つからなかったものは頼み直さない)
		if (!_link.isRequested) RequestPreset(a_decoration, a_pResourceManager);
		if (!_link.presetRef.IsValid() || !a_pResourceManager->IsReady(_link.presetRef)) return false;

		const Engine::Resource::UIPreset* _pPreset = a_pResourceManager->Get(_link.presetRef);
		if (!_pPreset) return false;

		if (_link.isApplied && _link.appliedRevision == _pPreset->revision) return false;

		// 読めない中身でも、同じ番号のうちは何度も読み直さない
		_link.isApplied = true;
		_link.appliedRevision = _pPreset->revision;

		Decoration _preset = {};
		if (!DeserializePresetDecoration(_pPreset->decorationJson, _preset))
		{
			ENGINE_WARNING("[DecorationPreset] プリセットの中身が読めません : %s", _pPreset->name.c_str());
			return false;
		}

		ApplyPresetParts(a_decoration, _preset, _link.follow);
		return true;
	}

	void PushToPreset(const Decoration& a_decoration, Engine::Resource::ResourceManager* a_pResourceManager)
	{
		const PresetLink& _link = a_decoration.preset;
		if (!_link.IsLinked() || !a_pResourceManager) return;
		if (!a_pResourceManager->IsReady(_link.presetRef)) return;

		Engine::Resource::UIPreset* _pPreset = a_pResourceManager->Ref(_link.presetRef);
		if (!_pPreset) return;

		// 付いていく部分だけを上書きする(付いていかない部分はプリセットの値のまま)
		Decoration _preset = {};
		DeserializePresetDecoration(_pPreset->decorationJson, _preset);
		ApplyPresetParts(_preset, a_decoration, _link.follow);

		_pPreset->SetDecorationJson(SerializePresetDecoration(_preset));
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	bool DrawPresetInspector(Decoration& a_decoration, const Engine::ECS::EngineServices& a_services)
	{
		PresetLink& _link = a_decoration.preset;
		bool _isChanged = false;

		Engine::EditorField::Header("Preset");

		const bool _wasLinked = _link.IsLinked();
		if (Engine::EditorField::AssetField(a_services, "Preset", "UIPreset", _link.presetGUID))
		{
			if (!_wasLinked) _link.follow = DEFAULT_PRESET_FOLLOW;
			RequestPreset(a_decoration, a_services.pResourceManager);
			_isChanged = true;
		}
		Engine::EditorField::Tooltip("指したプリセットの見た目に付いていく。プリセットを直すと、指している飾りが全部変わる");

		if (_link.IsLinked())
		{
			if (Engine::EditorField::FlagsField("Follow", _link.follow))
			{
				_link.isApplied = false;
				_isChanged = true;
			}
			Engine::EditorField::Tooltip("付いていく部分。LOOK=種類・色・枠・フォント / SIZE=大きさ・倍率・ピボット / REACTION / ANIMATION / CONTENT=文字列・テクスチャ");

			Engine::EditorField::Field("EditPreset", _link.isEditPreset);
			Engine::EditorField::Tooltip("立てると、付いていく部分をここで直せる。直した値はプリセットへ書き戻され、同じプリセットを指す飾りが全部変わる");

			const Engine::Resource::UIPreset* _pPreset =
				(a_services.pResourceManager && a_services.pResourceManager->IsReady(_link.presetRef))
				? a_services.pResourceManager->Get(_link.presetRef)
				: nullptr;

			if (_pPreset && _pPreset->isDirty)
			{
				Engine::EditorField::WarningText("プリセットが保存されていません");
				Engine::EditorField::SameLine();
				if (Engine::EditorField::SmallButton("Save Preset"))
				{
					if (auto* _pMutable = a_services.pResourceManager->Ref(_link.presetRef))
					{
						_pMutable->Save(a_services.pAssetDatabase->GetFilePathFromGUID(_link.presetGUID));
					}
				}
			}

			if (Engine::EditorField::SmallButton("Unlink"))
			{
				// 今の値を残したまま切り離す
				_link = PresetLink{};
				_isChanged = true;
			}
			Engine::EditorField::Tooltip("今の見た目を残したまま、プリセットから切り離す");
		}
		else
		{
			// この飾りから新しいプリセットを作って、そのままつなぐ
			static std::string s_newPresetName = "NewPreset";
			Engine::EditorField::Field("NewPresetName", s_newPresetName);
			Engine::EditorField::SameLine();
			if (!s_newPresetName.empty() && a_services.pAssetDatabase && Engine::EditorField::CreateSmallButton("Save As Preset"))
			{
				const Core::GUID _guid = Engine::Resource::UIPresetIO::Create(
					*a_services.pAssetDatabase, "", s_newPresetName, SerializePresetDecoration(a_decoration));

				if (_guid.IsValid())
				{
					_link.presetGUID = _guid;
					_link.follow = DEFAULT_PRESET_FOLLOW;
					RequestPreset(a_decoration, a_services.pResourceManager);
					_isChanged = true;
				}
			}
			Engine::EditorField::Tooltip("この飾りの見た目で Asset/UIPreset/ にプリセットを作り、そのままつなぐ(同じ名前があればそちらへつなぐ)");
		}

		return _isChanged;
	}
}
