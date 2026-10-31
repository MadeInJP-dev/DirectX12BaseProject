#include "EffectInstance.h"

#include "Engine/Audio/AudioManager.h"
#include "Engine/Graphics/LightManager/LightManager.h"
#include "Engine/Effect/Trail/TrailNodePool.h"

namespace Engine::Effect
{
	//======================================================================================
	// EffectInstance : 借りている声への操作
	//
	// どれもアセットを見ないので、アセットが読めていなくても呼べる。
	// 解放の経路(エンティティが消える)はアセットの生存と無関係に走るため
	//======================================================================================
	void EffectInstance::SetSoundPos(Engine::Audio::AudioManager& a_audioManager, const Math::Vector3& a_pos)
	{
		soundPos = a_pos;

		for (size_t _i = 0; _i < Resource::EFFECT_SOUND_MAX; ++_i)
		{
			auto* _pInstance = a_audioManager.RefInstance(soundHandles[_i]);
			if (!_pInstance) continue;

			// 2Dで発行されたインスタンスに位置を渡すと DirectXTK が例外を投げる
			if (!_pInstance->Is3D()) continue;

			_pInstance->SetPos(soundPos);
		}
	}

	void EffectInstance::ReleaseSounds(Engine::Audio::AudioManager& a_audioManager)
	{
		for (size_t _i = 0; _i < Resource::EFFECT_SOUND_MAX; ++_i)
		{
			// 鳴っていても止めて返却される
			a_audioManager.ReleaseSoundInstance(soundHandles[_i]);
			soundHandles[_i] = {};
			soundTriggered[_i] = false;

			// 何から発行したかも空にする。
			// 残しておくと、次に作り直すときに「合っている」と判断されて声が湧かない
			soundSourceGUID[_i] = Core::DEFAULT_GUID;
			soundSource3D[_i] = false;
		}
	}

	void EffectInstance::ReleaseLights(Engine::Graphics::LightManager& a_lightManager)
	{
		for (auto& _handle : lightHandles)
		{
			if (!_handle.IsValid()) continue;
			a_lightManager.RemoveLight(_handle);
			_handle = {};
		}
	}

	void EffectInstance::ReleaseTrails(TrailNodePool& a_trailNodePool)
	{
		for (auto& _handle : trailHandles)
		{
			if (!_handle.IsValid()) continue;
			a_trailNodePool.Detach(_handle);
		}
	}
}
