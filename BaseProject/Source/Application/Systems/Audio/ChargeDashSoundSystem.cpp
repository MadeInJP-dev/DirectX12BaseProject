#include "ChargeDashSoundSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Audio/AudioManager.h"

#include "Application/Components/Movement/ChargeDashComponent.h"
#include "Application/Components/Audio/ChargeDashSoundComponent.h"

namespace App::System
{
	//==========================================================================================
	// ChargeDashSoundSystem
	//
	// チャージダッシュの溜めの段階が変わった瞬間だけ、音を鳴らす / 止める。
	//
	//   段階  0 : 鳴らしていない(押していない・撃ち出した・クールタイム中)
	//         1 : 溜めている       … 溜めの音を頭から鳴らす
	//         2 : 溜まりきった     … 溜めの音を止め、溜まった音(単発)と待機の音(ループ)を鳴らす
	//   0 へ戻ったら溜めの音と待機の音を止める。溜まった音は鳴り終わるまで残す
	//   (撃ち出した瞬間に切れると、溜まった手応えまで消えてしまうため)。
	//
	// 段階は ChargeDashComponent の isCharging / isCharged / isDashing だけで決める。
	// 溜めを進めるのは ChargeDashSystem(Physics)なので、その後ろの PostUpdate で回す。
	// サウンドは借りっぱなしにして、鳴らすたびに頭から鳴らし直す(返すのはコンポーネントの Release)。
	//==========================================================================================
	namespace
	{
		using SoundHandle = Engine::Handle<Engine::Resource::SoundInstance>;

		// 頭から鳴らす。初めて鳴らすときに借りる。音が未設定なら何もしない
		void PlaySound(Engine::Audio::AudioManager& a_audio, SoundHandle& a_handle, const Core::GUID& a_guid, float a_volume, bool a_isLoop)
		{
			if (a_guid == Core::DEFAULT_GUID) return;

			if (!a_audio.RefInstance(a_handle))
			{
				a_handle = a_audio.CreateSoundInstance(a_guid, false);
			}
			if (auto* _pInstance = a_audio.RefInstance(a_handle))
			{
				_pInstance->Stop();
				_pInstance->SetVolume(a_volume);
				_pInstance->Play(a_isLoop);
			}
		}

		void StopSound(Engine::Audio::AudioManager& a_audio, const SoundHandle& a_handle)
		{
			if (auto* _pInstance = a_audio.RefInstance(a_handle)) _pInstance->Stop();
		}

		// 今あるべき段階
		uint32_t CalcStage(const Component::ChargeDashComponent& a_dash)
		{
			if (a_dash.isDashing || !a_dash.isCharging) return 0;
			return a_dash.isCharged ? 2u : 1u;
		}
	}

	void ChargeDashSoundSystem::Init(App::ECS::APPWorld& a_world)
	{
		// サウンドを鳴らすのでメインのタスクで回す(EffectUpdateSystem と同じ)
		a_world.ActiveTask<const Component::ChargeDashComponent, Component::ChargeDashSoundComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"ChargeDashSoundSystem",
			[]
			(
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				const Component::ChargeDashComponent* a_dashArray,
				Component::ChargeDashSoundComponent* a_soundArray
			)
			{
				if (!a_ctx.pServices || !a_ctx.pServices->pAudioManager) return;
				auto& _audio = *a_ctx.pServices->pAudioManager;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::ChargeDashSoundComponent& _sound = a_soundArray[_i];

					const uint32_t _stage = CalcStage(a_dashArray[_i]);
					if (_stage == _sound.stage) continue;

					switch (_stage)
					{
					case 1:
						// 溜め始め
						StopSound(_audio, _sound.readyHandle);
						PlaySound(_audio, _sound.chargingHandle, _sound.chargingSoundGUID, _sound.chargingVolume, false);
						break;

					case 2:
						// 溜まりきった : 溜めの音から、溜まった音 + 待機の音へ
						StopSound(_audio, _sound.chargingHandle);
						PlaySound(_audio, _sound.chargedHandle, _sound.chargedSoundGUID, _sound.chargedVolume, false);
						PlaySound(_audio, _sound.readyHandle, _sound.readySoundGUID, _sound.readyVolume, true);
						break;

					default:
						// 離した・撃ち出した。溜まった音は鳴り終わるまで残す
						StopSound(_audio, _sound.chargingHandle);
						StopSound(_audio, _sound.readyHandle);
						break;
					}

					_sound.stage = _stage;
				}
			}
		);
	}
}
