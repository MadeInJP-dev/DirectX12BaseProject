#include "SpeedLineSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Camera/TPSCameraStateComponent.h"
#include "Application/Components/Camera/SpeedLineComponent.h"
#include "Application/Components/Effect/EffectOverrideComponent.h"

namespace App::System
{
	//==========================================================================================
	// SpeedLineSystem
	//
	// 自機の速さに応じて、カメラのまわりを流れる風の筋(スピードライン)の量を動かす。
	//
	// 筋はカメラに付けたエフェクトが出していて、ここはその個体パラメータ
	// (EffectOverrideComponent::params[0])へ 0..1 を書くだけ。
	// どれだけ出すかはエフェクトアセットの結び付け(ParamBindings)が決めるので、
	// 筋の見た目を直すのにコードを触らなくてよい。
	//
	//   t = speedThreshold から 1 の区間を 0..1 へ引き伸ばしたもの
	//
	// 速さは TPSSystem が既になました currentSpeed01 を使う(ラジアルブラーと同じ)。
	// あちらで滑らかになっているので、ここではなまさない。
	// TPSCameraStateComponent を読むので、それを書く TPSSystem より後ろに回る。
	//==========================================================================================
	void SpeedLineSystem::Init(App::ECS::APPWorld& a_world)
	{
		// 自分のチャンクの値だけを書くので、ワーカーで回す
		a_world.ActiveJobTask<const Component::TPSCameraStateComponent, Component::SpeedLineComponent, Component::EffectOverrideComponent>(
			Engine::ECS::ESystemType::Camera,
			"SpeedLineSystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& /*a_ctx*/,
				Component::ActiveTag* /*a_tags*/,
				const Component::TPSCameraStateComponent* a_tpsStatArray,
				Component::SpeedLineComponent* a_speedLineArray,
				Component::EffectOverrideComponent* a_overrideArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::SpeedLineComponent& _speedLine = a_speedLineArray[_i];

					// しきい値が 1 以上(=どの速さでも出さない)でも 0 除算しないよう、分母を先に見る
					const float _threshold = std::clamp(_speedLine.speedThreshold, 0.0f, 1.0f);
					const float _range = 1.0f - _threshold;

					_speedLine.currentAmount = (_range > 1e-4f)
						? std::clamp((a_tpsStatArray[_i].currentSpeed01 - _threshold) / _range, 0.0f, 1.0f)
						: 0.0f;

					a_overrideArray[_i].params[0] = _speedLine.currentAmount;
				}
			}
		);
	}
}
