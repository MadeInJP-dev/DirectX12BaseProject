#include "SceneAudio.h"

#include "Engine/Audio/AudioManager.h"
#include "Engine/EditorField/EditorField.h"

namespace
{
	// グループの並びに合わせた見出し
	constexpr const char* GROUP_LABEL[Engine::Audio::SOUND_GROUP_COUNT] =
	{
		"SE",
		"BGM",
		"UI",
		"Voice",
	};
}

namespace Engine::Scene
{
	//======================================================================================
	// 席の出し入れ
	//======================================================================================
	void SceneAudio::Register(Audio::AudioManager* a_pAudioManager)
	{
		Unregister();

		m_pAudioManager = a_pAudioManager;
		if (!m_pAudioManager) return;

		m_volumeScopeID = m_pAudioManager->AddVolumeScope(m_volume);
	}

	void SceneAudio::Unregister()
	{
		if (m_pAudioManager && m_volumeScopeID != Audio::INVALID_VOLUME_SCOPE_ID)
		{
			m_pAudioManager->RemoveVolumeScope(m_volumeScopeID);
		}

		m_pAudioManager = nullptr;
		m_volumeScopeID = Audio::INVALID_VOLUME_SCOPE_ID;
	}

	void SceneAudio::Apply()
	{
		if (!m_pAudioManager) return;

		m_pAudioManager->SetVolumeScope(m_volumeScopeID, m_volume);
	}

	//======================================================================================
	// 保存・読み込み
	//======================================================================================
	void SceneAudio::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("MasterVolume", m_volume.master);

		// グループは並び順で保存する。
		// ※ ESoundGroup へ足すときは Count の手前へ(既存の値がずれる)
		for (size_t _i = 0; _i < m_volume.groupArray.size(); ++_i)
		{
			a_ar.Field("GroupVolume" + std::to_string(_i), m_volume.groupArray[_i]);
		}

		if (a_ar.IsLoading()) Apply();
	}

	//======================================================================================
	// 編集UI
	//======================================================================================
	void SceneAudio::DrawEdit()
	{
		bool _isChanged = false;

		Engine::EditorField::HelpText("このシーンで鳴らした音に掛かる。プレイヤーの音量(設定画面)はさらにこれへ掛かる");

		if (Engine::EditorField::Slider("Master", m_volume.master, 0.0f, 1.0f)) _isChanged = true;
		Engine::EditorField::Tooltip("このシーンの全部の音へ掛かる");

		for (size_t _i = 0; _i < m_volume.groupArray.size(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(_i));

			if (Engine::EditorField::Slider(GROUP_LABEL[_i], m_volume.groupArray[_i], 0.0f, 1.0f)) _isChanged = true;
		}

		// 動かした瞬間に効かせる。鳴っている音にもその場で送り直される
		if (_isChanged) Apply();
	}
}
