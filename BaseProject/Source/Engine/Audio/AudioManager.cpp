#include "AudioManager.h"

#include "../Resource/Manager/ResourceManager/ResourceManager.h"

namespace Engine::Audio
{
	//======================================================================================
	// 音量
	//======================================================================================
	VolumeScopeID AudioManager::AddVolumeScope(const SoundVolume& a_volume)
	{
		const VolumeScopeID _id = m_nextVolumeScopeID++;
		m_volumeScopeMap[_id] = a_volume;
		return _id;
	}

	void AudioManager::SetVolumeScope(VolumeScopeID a_id, const SoundVolume& a_volume)
	{
		auto _it = m_volumeScopeMap.find(a_id);
		if (_it == m_volumeScopeMap.end()) return;
		if (_it->second == a_volume) return;

		_it->second = a_volume;
		RefreshAllVolume();
	}

	void AudioManager::RemoveVolumeScope(VolumeScopeID a_id)
	{
		m_volumeScopeMap.erase(a_id);

		// 返した席を張ったままにしない(以降の音は素通しで発行される)
		if (m_currentVolumeScopeID == a_id) m_currentVolumeScopeID = INVALID_VOLUME_SCOPE_ID;
	}

	void AudioManager::SetUserMasterVolume(float a_volume)
	{
		const float _volume = std::clamp(a_volume, 0.0f, 1.0f);
		if (m_userMasterVolume == _volume) return;

		m_userMasterVolume = _volume;
		RefreshAllVolume();
	}

	void AudioManager::SetUserGroupVolume(ESoundGroup a_group, float a_volume)
	{
		const size_t _index = static_cast<size_t>(a_group);
		if (_index >= m_userGroupVolumeArray.size()) return;

		const float _volume = std::clamp(a_volume, 0.0f, 1.0f);
		if (m_userGroupVolumeArray[_index] == _volume) return;

		m_userGroupVolumeArray[_index] = _volume;
		RefreshAllVolume();
	}

	float AudioManager::GetUserGroupVolume(ESoundGroup a_group) const
	{
		const size_t _index = static_cast<size_t>(a_group);
		if (_index >= m_userGroupVolumeArray.size()) return 1.0f;

		return m_userGroupVolumeArray[_index];
	}

	float AudioManager::CalcVolumeScale(ESoundGroup a_group, VolumeScopeID a_scopeID) const
	{
		// シーンの音量(作り手のつり合い)。席の外の音は素通し
		float _sceneScale = 1.0f;
		if (auto _it = m_volumeScopeMap.find(a_scopeID); _it != m_volumeScopeMap.end())
		{
			_sceneScale = _it->second.CalcScale(a_group);
		}

		// × プレイヤーの音量
		return _sceneScale * m_userMasterVolume * GetUserGroupVolume(a_group);
	}

	//======================================================================================
	// 鳴っているものすべてへ音量を送り直す
	//--------------------------------------------------------------------------------------
	// 鳴らしている側は設定が変わったことに気付けない
	// (重ねたシーンのように更新が止まっているものもある)ので、ここから送り込む
	//======================================================================================
	void AudioManager::RefreshAllVolume()
	{
		for (auto& _instance : m_soundInstancePool.RefAll())
		{
			if (!_instance.has_value()) continue;
			_instance->RefreshVolume();
		}
	}

	bool AudioManager::Init(Resource::ResourceManager* a_pResourceManager)
	{
		m_pResourceManager = a_pResourceManager;

		
		Release();

		// エンジンフラグ定義
		DirectX::AUDIO_ENGINE_FLAGS _flgs = DirectX::AudioEngine_EnvironmentalReverb | DirectX::AudioEngine_ReverbUseFilters;
#ifdef _DEBUG
		_flgs |= DirectX::AudioEngine_Debug;
#endif
		// オーディオエンジン作成
		m_upAudioEngine = std::make_unique<DirectX::AudioEngine>(_flgs);
		m_upAudioEngine->SetReverb(DirectX::Reverb_Default);

		m_listener.OrientFront = { 0,0,1 };

		ENGINE_LOG("[Init] AudioManager が初期化されました");
		return true;
	}
	void AudioManager::ReleaseInstances()
	{
		// 鳴っているものを止めてから破棄する
		for (auto& _instance : m_soundInstancePool.RefAll())
		{
			if (!_instance.has_value()) continue;
			_instance->Stop();
		}
		m_soundInstancePool.Release();

		// 関所が覚えている声も、もう無いので忘れる
		m_playGates.clear();
	}
	void AudioManager::Release()
	{
		// SoundEffectInstance は AudioEngine を参照しているため、
		// エンジンより先にインスタンスを破棄しきる
		ReleaseInstances();

		m_upAudioEngine = nullptr;
	}
	bool AudioManager::CanPlaySound(const Core::GUID& a_guid, float a_minInterval, uint32_t a_maxConcurrent)
	{
		if (a_minInterval <= 0.0f && a_maxConcurrent == 0) return true;

		auto _it = m_playGates.find(a_guid);
		if (_it == m_playGates.end()) return true;

		SoundPlayGate& _gate = _it->second;

		// 前回から間が空いていない
		if (a_minInterval > 0.0f && _gate.hasPlayed)
		{
			const float _elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - _gate.lastPlayTime).count();
			if (_elapsed < a_minInterval) return false;
		}

		// 同時に鳴っている数。鳴り終わった・返された声はここで外す
		if (a_maxConcurrent > 0)
		{
			std::erase_if(_gate.playingHandles, [this](const Handle<Resource::SoundInstance>& a_handle)
				{
					auto* _pInstance = RefInstance(a_handle);
					return !_pInstance || !_pInstance->IsPlay();
				});
			if (_gate.playingHandles.size() >= a_maxConcurrent) return false;
		}

		return true;
	}

	void AudioManager::NotifySoundPlayed(const Core::GUID& a_guid, const Handle<Resource::SoundInstance>& a_handle)
	{
		SoundPlayGate& _gate = m_playGates[a_guid];
		_gate.lastPlayTime = std::chrono::steady_clock::now();
		_gate.hasPlayed = true;

		// 鳴り終わった・返された声を外してから足す(数えるときだけ外すと、上限を使わない音では溜まり続ける)
		std::erase_if(_gate.playingHandles, [this](const Handle<Resource::SoundInstance>& a_h)
			{
				auto* _pInstance = RefInstance(a_h);
				return !_pInstance || !_pInstance->IsPlay();
			});
		_gate.playingHandles.push_back(a_handle);
	}

	void AudioManager::Update()
	{
		if (!m_upAudioEngine) return;

		// Update() が false を返すのは「無音」か「デバイスロスト」のどちらか
		if (m_upAudioEngine->Update()) return;

		if (!m_upAudioEngine->IsCriticalError()) return;

		// オーディオデバイスが失われたので作り直す。
		// 発行済みの SoundEffectInstance は AudioEngine 側で新デバイスへ移行される
		ENGINE_WARNING("オーディオデバイスをロストしました。再初期化します");
		if (!m_upAudioEngine->Reset())
		{
			ENGINE_WARNING("オーディオデバイスの再初期化に失敗しました");
		}
	}
	void AudioManager::SubmitListener(const ListenerData& a_data)
	{
		m_listener.SetPosition(a_data.pos);
		m_listener.SetVelocity(a_data.velocity);

		// X3DAudio は前方・上方向が「正規化されていて直交している」ことを前提にしている。
		// 送られてきた行列にスケールや微妙な歪みが乗っていても落ちないよう、ここで整える。
		Math::Vector3 _front = a_data.front;
		if (_front.LengthSquared() > 1e-8f) _front.Normalize();
		else                                _front = Math::Vector3::Forward();

		// 上方向から前方成分を抜いて直交化する
		Math::Vector3 _up = a_data.up - _front * a_data.up.Dot(_front);
		if (_up.LengthSquared() <= 1e-8f)
		{
			// 前方と上方向が平行だった場合の逃げ道。前方と重ならない軸から作り直す
			Math::Vector3 _ref = (std::fabs(_front.y) > 0.99f)
				? Math::Vector3::Forward()
				: Math::Vector3::Up();
			_up = _ref - _front * _ref.Dot(_front);
		}
		_up.Normalize();

		m_listener.SetOrientation(_front, _up);
	}
	const Resource::SoundInstance* AudioManager::GetInstance(const Handle<Resource::SoundInstance>& a_handle) const
	{
		return m_soundInstancePool.Get(a_handle);
	}
	Resource::SoundInstance* AudioManager::RefInstance(const Handle<Resource::SoundInstance>& a_handle)
	{
		return m_soundInstancePool.Ref(a_handle);
	}
	Handle<Resource::SoundInstance> AudioManager::CreateSoundInstance(
		const Core::GUID& a_guid, bool a_is3D, ESoundGroup a_group)
	{
		// サウンドエンジンがなければ発行しない
		if (!m_upAudioEngine)
		{
			ENGINE_WARNING("サウンドエンジンがない状態でサウンドのリクエストが来ました");
			return Handle<Resource::SoundInstance>();
		}

		if(a_guid == Core::DEFAULT_GUID) return Handle<Resource::SoundInstance>();

		// インスタンスを作成
		Resource::SoundInstance _instance = {};

		// ロード済みのサウンドかチェック
		if (m_pResourceManager->Has<Resource::Sound>(a_guid))
		{
			// サウンドの取得
			auto _soundRef = m_pResourceManager->GetCache<Resource::Sound>(a_guid);
			if (!_soundRef.IsValid()) return Handle<Resource::SoundInstance>();
			_instance.Init(*this, *m_pResourceManager, _soundRef, a_is3D);// インスタンスの初期化

		}
		else
		{
			// サウンドのロード
			auto _soundRef = m_pResourceManager->LoadImmediate<Resource::Sound>(a_guid);
			if (!_soundRef.IsValid()) return Handle<Resource::SoundInstance>();
			_instance.Init(*this, *m_pResourceManager, _soundRef, a_is3D);// インスタンスの初期化
		}

		// グループと席(今のシーン)の札を付けてから預ける。
		// 以降この音は、そのシーンの音量とプレイヤーの音量が掛かった状態で鳴る
		_instance.SetVolumeTag(a_group, m_currentVolumeScopeID);

		return m_soundInstancePool.Add(std::move(_instance));
	}
	void AudioManager::ReleaseSoundInstance(const Handle<Resource::SoundInstance>& a_handle)
	{
		// 存在しないハンドルは Remove 側で弾かれるので、ここでは停止だけ気にする
		if (auto* _pInstance = m_soundInstancePool.Ref(a_handle))
		{
			_pInstance->Stop();
		}
		m_soundInstancePool.Remove(a_handle);
	}
	AudioManager::AudioManager()
	{
		// プレイヤーの音量は既定で素通し。実際の値は起動時にアプリのユーザーデータから流し込まれる
		m_userGroupVolumeArray.fill(1.0f);
	}
	AudioManager::~AudioManager()
	{
		// 本来は MainEngine::Release() から明示的に解放されている想定。
		// 取りこぼしがあった場合の保険として正しい順序で解放しておく
		Release();
	}
}
