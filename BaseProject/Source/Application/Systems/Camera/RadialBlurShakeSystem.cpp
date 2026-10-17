#include "RadialBlurShakeSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Camera/RadialBlurComponent.h"
#include "Application/InstanceResource/RadialBlurShakeResource.h"

namespace App::System
{
	//==========================================================================================
	// RadialBlurShakeSystem
	//
	// 画面のラジアルブラーを震わせる。
	//
	// 頼むのは RadialBlurShakeResource(今は SwarmBossController が咆哮の間だけ書く)。
	// ここはカメラの RadialBlurComponent の実行中の値(shakeStrength / shakeCenterOffset)を書くだけで、
	// 速度ぶん(currentStrength)と足し合わせてシェーダーへ送るのは CamSetShaderSystem。
	//
	//   揺れ   = 周波数の違う3本の正弦波の和(-1〜1)。周期が揃わないので規則的に見えない
	//   引きずり量 = strength × (1 + strengthJitter × 揺れ) × weight
	//   中心のずれ = centerJitter × 揺れ(縦横で別の組) × weight
	//
	// 揺れの位相はカメラごとの時計(shakeTime)で進める。震えていない間は 0 に戻すので、
	// 毎回同じ揺れ方から始まる。
	//==========================================================================================
	namespace
	{
		//--------------------------------------------------------------------------------------
		// 周波数の違う3本の正弦波の和(-1〜1)。a_seed で組をずらす
		//--------------------------------------------------------------------------------------
		float ShakeNoise(float a_time, float a_frequency, float a_seed)
		{
			const float _w = DirectX::XM_2PI * a_frequency * a_time;
			return
				std::sin(_w + a_seed)                 * 0.5f +
				std::sin(_w * 1.73f + a_seed * 2.3f)  * 0.3f +
				std::sin(_w * 2.91f + a_seed * 4.1f)  * 0.2f;
		}
	}

	void RadialBlurShakeSystem::Init(App::ECS::APPWorld& a_world)
	{
		// 自分のチャンクの値だけを書く(リソースは読むだけ)ので、ワーカーで回す
		a_world.ActiveJobTask<Component::RadialBlurComponent>(
			Engine::ECS::ESystemType::Camera,
			"RadialBlurShakeSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				Component::RadialBlurComponent* a_radialArray
			)
			{
				if (!a_ctx.pWorld) return;

				const InstanceResource::RadialBlurShakeResource& _shake = a_ctx.pWorld->GetResource<InstanceResource::RadialBlurShakeResource>();
				const float _weight = std::clamp(_shake.weight, 0.0f, 1.0f);

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::RadialBlurComponent& _radial = a_radialArray[_i];

					// 震えていなければ足しているぶんを戻す
					if (_weight <= 0.0f || _shake.strength <= 0.0f)
					{
						_radial.shakeStrength     = 0.0f;
						_radial.shakeCenterOffset = { 0.0f, 0.0f };
						_radial.shakeTime         = 0.0f;
						continue;
					}

					_radial.shakeTime += a_ctx.dt;

					const float _jitter = std::clamp(_shake.strengthJitter, 0.0f, 1.0f);
					const float _noise  = ShakeNoise(_radial.shakeTime, _shake.frequency, 0.0f);

					_radial.shakeStrength = std::max(_shake.strength * (1.0f + _jitter * _noise) * _weight, 0.0f);

					_radial.shakeCenterOffset = Math::Vector2(
						ShakeNoise(_radial.shakeTime, _shake.frequency, 1.7f),
						ShakeNoise(_radial.shakeTime, _shake.frequency, 3.1f)) * (_shake.centerJitter * _weight);
				}
			}
		)
		.ReadsResource<InstanceResource::RadialBlurShakeResource>();
	}
}
