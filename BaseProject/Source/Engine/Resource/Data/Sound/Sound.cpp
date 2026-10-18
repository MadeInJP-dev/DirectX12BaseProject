#include "Sound.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "../../../Audio/AudioManager.h"

namespace Engine::Resource
{
	SoundInstance::~SoundInstance()
	{
		Stop();
	}
	bool SoundInstance::Init(Audio::AudioManager& a_audioManager, ResourceManager& a_resourceManager, const ResourceRef<Sound>& a_resourceRef,bool a_is3D)
	{
		m_pAudioManager = &a_audioManager;

		// 元データ取得
		auto* _pSound = a_resourceManager.Ref(a_resourceRef);
		if (!_pSound) return false;

		// エフェクト取得
		auto* _pEffect = _pSound->RefSoundEffect();
		if (!_pEffect) return false;

		// インスタンス作成
		DirectX::SOUND_EFFECT_INSTANCE_FLAGS _flags = DirectX::SoundEffectInstance_Default;
		if (a_is3D)
		{
			_flags |= DirectX::SoundEffectInstance_Use3D | DirectX::SoundEffectInstance_ReverbUseFilters;
		}
		m_upSoundInstance = _pEffect->CreateInstance(_flags);
		m_is3D = a_is3D;

		// 元データを参照として保持し、インスタンスが生きている間は
		// Sound(= DirectX::SoundEffect) が解放されないようにする
		m_soundRef = a_resourceRef;

		// 音量を一度も指定されないまま鳴らされても、
		// プレイヤーの音量が効いている状態にしておく(シーンの席は発行元が札を付けたときに効く)
		RefreshVolume();

		return true;
	}
	void SoundInstance::Play(bool a_isLoop)
	{
		if (!m_upSoundInstance) return;

		//停止
		m_upSoundInstance->Stop();

		// 再生
		m_upSoundInstance->Play(a_isLoop);
	}
	void SoundInstance::Play3D(const Math::Vector3 & a_pos, bool a_isLoop)
	{
		if (!m_upSoundInstance) return;

		if (!m_is3D)
		{
			// 3Dなしで作られたインスタンスに Apply3D を掛けると DirectXTK が例外を投げる。
			// 3Dで鳴らしたい場合は CreateSoundInstance(..., true) で発行すること
			ENGINE_WARNING("3D指定なしで作成されたサウンドインスタンスに Play3D が呼ばれました");
			Play(a_isLoop);
			return;
		}

		m_upSoundInstance->Stop();

		// 覚えている音量(× シーン × プレイヤーの音量)で鳴らし直す。
		// 以前はここで生の 1 を入れていたので、
		// 鳴らす前に指定した音量もグループ音量も捨てられていた
		RefreshVolume();

		m_upSoundInstance->Play(a_isLoop);

		// 座標設定
		m_emitter.SetPosition(a_pos);

		Apply3D();
	}
	void SoundInstance::Apply3D()
	{
		if (!m_upSoundInstance) return;

		// SoundEffectInstance_Use3D なしで作られたインスタンスに対して
		// Apply3D を呼ぶと DirectXTK が例外を投げるため、ここで弾く
		if (!m_is3D || !m_pAudioManager) return;

		m_upSoundInstance->Apply3D(m_pAudioManager->RefAudioListner(), m_emitter, false);
	}
	void SoundInstance::Stop()
	{
		if (!m_upSoundInstance) return;
		m_upSoundInstance->Stop();
	}
	void SoundInstance::Pause()
	{
		if (!m_upSoundInstance) return;
		m_upSoundInstance->Pause();
	}
	void SoundInstance::Resume()
	{
		if (!m_upSoundInstance) return;
		m_upSoundInstance->Resume();
	}
	//======================================================================================
	// 音量
	//--------------------------------------------------------------------------------------
	// 覚えるのは「呼び出し側が指定した音量」だけ。
	// 実際に送るときにシーンの音量とプレイヤーの音量を掛ける。
	//
	// こうしておくと、設定画面で BGM を絞ったときに
	// 鳴らしている側(進行役・コンポーネント)を1つも触らずに効かせられる。
	// 逆に呼び出し側が設定値を掛けてから渡す作りにすると、
	// 設定を変えるたびに全員へ知らせて回ることになる。
	//======================================================================================
	void SoundInstance::SetVolume(float a_vol)
	{
		m_volume = a_vol;
		RefreshVolume();
	}

	void SoundInstance::SetVolumeTag(Audio::ESoundGroup a_group, Audio::VolumeScopeID a_scopeID)
	{
		if (m_group == a_group && m_volumeScopeID == a_scopeID) return;

		m_group = a_group;
		m_volumeScopeID = a_scopeID;
		RefreshVolume();
	}

	void SoundInstance::RefreshVolume()
	{
		if (!m_upSoundInstance || !m_pAudioManager) return;

		const float _scale = m_pAudioManager->CalcVolumeScale(m_group, m_volumeScopeID);

		m_upSoundInstance->SetVolume(std::clamp(m_volume * _scale, 0.0f, 1.0f));
		Apply3D();
	}
	void SoundInstance::SetPos(const Math::Vector3 & a_pos)
	{
		if (!m_upSoundInstance) return;
		m_emitter.SetPosition(a_pos);
		Apply3D();
	}
	void SoundInstance::SetCurveDistanceScaler(float a_val)
	{
		if (!m_upSoundInstance) return;
		m_emitter.CurveDistanceScaler = a_val;
		Apply3D();
	}
	bool SoundInstance::IsPlay()
	{
		if (!m_upSoundInstance) return false;
		if (m_upSoundInstance->GetState() == DirectX::SoundState::PLAYING) return true;
		return false;
	}
	bool SoundInstance::IsPause()
	{
		if (!m_upSoundInstance) return false;
		if (m_upSoundInstance->GetState() == DirectX::SoundState::PAUSED) return true;
		return false;
	}
}
