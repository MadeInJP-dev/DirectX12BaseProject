#include "HitDamageEffectSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/MainEngine.h"

#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Combat/HitDamageEffectComponent.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/SceneView/DamageEffectData.h"

namespace App::System
{
	//==========================================================================================
	// HitDamageEffectSystem
	//
	// プレイヤーの被弾で、画面と HUD を乱す効果(DamageEffectPass)を出す。
	//
	// ・被弾は HealthComponent の体力が前のフレームより減ったことで見る。
	//   ヒット(HitEventResource)を数えると、爆風や体当たりのように積み方の違う
	//   ダメージを全部拾う必要があるが、体力の差ならどこから来ても同じに扱える。
	//   (デバッグの無敵中は体力が減らないので出ない)
	// ・立ち上がりの強さは減った量で決める(最大体力 × fullDamageRatio で最大)。
	//   効いている途中にもう一度食らったら、強い方から数え直す。
	// ・強さは duration 秒で 0 へ落とす。(1 - t)^fadePower で、出だしは強く・尾は短く。
	//
	// 2つのタスクに分ける。
	//   PostUpdate : 体力を減らす HealthSystem の後で、強さを動かす
	//   PreDraw    : SceneView へ送る(カメラの画面効果と同じ帯。EndFrame で消える)
	//==========================================================================================
	void HitDamageEffectSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<Component::HitDamageEffectComponent, const Component::HealthComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"HitDamageEffectSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				Component::HitDamageEffectComponent* a_effectArray,
				const Component::HealthComponent* a_healthArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::HitDamageEffectComponent& _effect = a_effectArray[_i];
					const Component::HealthComponent& _health = a_healthArray[_i];

					//--------------------------------------------------------------
					// 被弾を見る : 体力が前のフレームより減っていたら立ち上げる
					//--------------------------------------------------------------
					float _hitIntensity = 0.0f;
					if (_effect.prevHealth >= 0.0f && _health.currentHealth < _effect.prevHealth)
					{
						const float _damage = _effect.prevHealth - _health.currentHealth;
						const float _full = (std::max)(_health.maxHealth * _effect.fullDamageRatio, 1e-4f);
						_hitIntensity = std::clamp(_damage / _full, _effect.minIntensity, 1.0f);
					}
					_effect.prevHealth = _health.currentHealth;

					if (_effect.isTestRequested)
					{
						_hitIntensity = 1.0f;
						_effect.isTestRequested = false;
					}

					// 今の強さより強ければ数え直す(弱い当たりで強い効果を打ち消さない)
					if (_hitIntensity > 0.0f && _hitIntensity >= _effect.currentIntensity)
					{
						_effect.peakIntensity = _hitIntensity;
						_effect.timer = 0.0f;
					}

					//--------------------------------------------------------------
					// 消していく
					//--------------------------------------------------------------
					if (_effect.peakIntensity <= 0.0f)
					{
						_effect.currentIntensity = 0.0f;
						continue;
					}

					_effect.timer += a_ctx.dt;
					const float _duration = (std::max)(_effect.duration, 1e-3f);
					const float _t = std::clamp(_effect.timer / _duration, 0.0f, 1.0f);
					_effect.currentIntensity = _effect.peakIntensity * std::pow(1.0f - _t, (std::max)(_effect.fadePower, 0.01f));

					if (_t >= 1.0f)
					{
						_effect.peakIntensity = 0.0f;
						_effect.currentIntensity = 0.0f;
					}
				}
			}
		)
		// 順序 : 体力を減らした後に差を見る
		.After({ "HealthSystem" });

		a_world.ActiveTask<const Component::HitDamageEffectComponent>(
			Engine::ECS::ESystemType::PreDraw,
			"HitDamageEffectSubmitSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				const Component::HitDamageEffectComponent* a_effectArray
			)
			{
				auto* _pGE = a_ctx.pServices->pMainEngine->RefGraphicsEngine();
				if (!_pGE) return;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::HitDamageEffectComponent& _effect = a_effectArray[_i];
					if (_effect.currentIntensity <= 0.0f) continue;

					Engine::Graphics::DamageEffectData _data = {};
					_data.intensity = _effect.currentIntensity;
					_data.screenNoise = _effect.screenNoise;
					_data.screenGlitch = _effect.screenGlitch;
					_data.screenRGBShift = _effect.screenRGBShift;
					_data.screenDesaturate = _effect.screenDesaturate;
					_data.vignette = _effect.vignette;
					_data.vignetteColor = _effect.vignetteColor;
					_data.hudNoise = _effect.hudNoise;
					_data.hudGlitch = _effect.hudGlitch;
					_data.hudRGBShift = _effect.hudRGBShift;
					_data.hudFlicker = _effect.hudFlicker;
					_data.glitchBands = _effect.glitchBands;
					_data.glitchRate = _effect.glitchRate;
					_data.glitchSpeed = _effect.glitchSpeed;

					// 複数あれば SceneView が強い方を残す
					_pGE->RefSceneView()->SetDamageEffectData(_data);
				}
			}
		);
	}
}
