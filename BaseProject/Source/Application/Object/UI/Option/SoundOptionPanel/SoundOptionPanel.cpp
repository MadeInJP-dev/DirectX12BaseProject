#include "SoundOptionPanel.h"

#include "Engine/GameObject/BaseObject/BaseObject.h"	// ObjectContext
#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/EditorField/EditorField.h"

#include "Application/InstanceResource/UserDataResource.h"

namespace App::Object
{
	namespace
	{
		// 種類ごとの名前 : 保存のキーの頭とインスペクターの見出し(EVolumeKind の並び)
		constexpr const char* VOLUME_KIND_NAME[] =
		{
			"MasterVolume",
			"BgmVolume",
			"SeVolume",
			"UiVolume",
		};
	}

	uint32_t& SoundOptionPanel::RefVolume(Game::UserData::SoundSettings& a_settings, EVolumeKind a_kind)
	{
		switch (a_kind)
		{
		case EVolumeKind::Bgm: return a_settings.bgmVol;
		case EVolumeKind::Se:  return a_settings.seVol;
		case EVolumeKind::Ui:  return a_settings.uiVol;

		case EVolumeKind::Master:
		default:               return a_settings.mainVol;
		}
	}

	void SoundOptionPanel::OnOpen(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		m_settings = _pUserDataResource->pUserData->GetSoundSettings();
	}

	//======================================================================================
	// 並べる
	//======================================================================================
	void SoundOptionPanel::LayoutItems(Engine::GameObject::ObjectContext& a_context)
	{
		PlaceHeaderRow(a_context, m_header);

		for (NumericMutate& _numeric : m_volumeNumericArray)
		{
			PlaceNumeric(a_context, _numeric);
			AdvanceRow();
		}
	}

	//======================================================================================
	// 操作
	//======================================================================================
	void SoundOptionPanel::UpdateItems(Engine::GameObject::ObjectContext& a_context)
	{
		for (size_t _i = 0; _i < m_volumeNumericArray.size(); ++_i)
		{
			const EVolumeKind _kind = static_cast<EVolumeKind>(_i);

			// 0〜100 のまま動かし、整数に丸めて書く(ゲージにもそのまま出る)。
			// 掴んで動かしている間は毎フレーム値が来るので、整数が変わったときだけ鳴らす側へ送る
			const uint32_t _current = RefVolume(m_settings, _kind);
			float _volume = static_cast<float>(_current);
			if (!m_volumeNumericArray[_i].Update(a_context, _volume,
				0.0f, static_cast<float>(Game::UserData::SOUND_VOLUME_MAX), static_cast<float>(VOLUME_STEP))) continue;

			const int _rounded = static_cast<int>(std::lround(_volume));
			if (_rounded != static_cast<int>(_current)) ApplyVolume(a_context, _kind, _rounded);
		}
	}

	void SoundOptionPanel::ApplyVolume(Engine::GameObject::ObjectContext& a_context, EVolumeKind a_kind, int a_volume)
	{
		const int _max = static_cast<int>(Game::UserData::SOUND_VOLUME_MAX);
		RefVolume(m_settings, a_kind) = static_cast<uint32_t>(std::clamp(a_volume, 0, _max));

		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		Game::UserData& _userData = *_pUserDataResource->pUserData;
		_userData.RefDoundSettings() = m_settings;

		// その場で音を変える。鳴っている音(後ろのゲームの BGM も)へ送り直される
		if (a_context.pServices && a_context.pServices->pAudioManager)
		{
			_userData.ApplySoundSettings(*a_context.pServices->pAudioManager);
		}
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void SoundOptionPanel::ArchiveItems(Engine::Persistence::Archive& a_ar)
	{
		static_assert(std::size(VOLUME_KIND_NAME) == VOLUME_KIND_COUNT, "VOLUME_KIND_NAME を EVolumeKind と揃えること");

		ArchiveItemUI(a_ar, "Header", m_header);

		for (size_t _i = 0; _i < m_volumeNumericArray.size(); ++_i)
		{
			m_volumeNumericArray[_i].Archive(a_ar, VOLUME_KIND_NAME[_i]);
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void SoundOptionPanel::DrawItemsInspector(Engine::GameObject::ObjectContext& a_context)
	{
		DrawItemUIInspector(a_context, "Header", m_header);
		Engine::EditorField::Tooltip("見出し(文字だけの UI)。未設定なら行を取らない");

		for (size_t _i = 0; _i < m_volumeNumericArray.size(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(_i));
			m_volumeNumericArray[_i].DrawInspector(a_context, VOLUME_KIND_NAME[_i]);

			Engine::EditorField::Value(VOLUME_KIND_NAME[_i], "%u", RefVolume(m_settings, static_cast<EVolumeKind>(_i)));
		}
	}
}
