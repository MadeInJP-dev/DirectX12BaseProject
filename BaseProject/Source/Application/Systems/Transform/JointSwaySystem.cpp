#include "JointSwaySystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Transform/JointSwayComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"

namespace App::System
{
	//==========================================================================================
	// JointSwaySystem
	//
	//   角度 = amplitude * sin(2π * (time / period + phase))
	//   向き = 揺れ(軸まわりに角度) を当ててから restQuat
	//
	// 揺れは自分のローカルの軸で回したいので、中心の向きより先に当てる
	// (Quaternion の掛け算は「左を当ててから右」)。
	// 経過時間はエンティティごとに持つ。止まっていたシーンが動き出しても、
	// そこから続きを揺れるだけで飛ばない。
	//
	// 自分のチャンクの値だけを書くので、ワーカーで回す
	//==========================================================================================
	void JointSwaySystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveJobTask<Component::JointSwayComponent, Component::LocalTransformComponent>(
			Engine::ECS::ESystemType::Update,
			"JointSwaySystem",
			[](
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				Component::JointSwayComponent* a_swayArray,
				Component::LocalTransformComponent* a_trsArray
			)
			{
				constexpr float TWO_PI = DirectX::XM_2PI;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::JointSwayComponent& _sway = a_swayArray[_i];
					Component::LocalTransformComponent& _trs = a_trsArray[_i];

					// 周期が 0 以下なら止めて中心の向きに置く
					float _angleDeg = 0.0f;
					if (_sway.period > 0.0f)
					{
						// 周期ぶんで巻き戻し、長く動かしても値が大きくなりすぎないようにする
						_sway.time = std::fmod(_sway.time + a_ctx.dt, _sway.period);
						_angleDeg = _sway.amplitude * std::sin(TWO_PI * (_sway.time / _sway.period + _sway.phase));
					}

					Math::Vector3 _axis = _sway.axis;
					if (_axis.LengthSquared() <= 1e-8f) _axis = { 1.0f, 0.0f, 0.0f };
					_axis.Normalize();

					const Math::Quaternion _swing = Math::Quaternion::CreateFromAxisAngle(_axis, DirectX::XMConvertToRadians(_angleDeg));

					Math::Quaternion _quat = _swing * _sway.restQuat;
					_quat.Normalize();

					_trs.quat = _quat;
					_trs.isDirty = true;
				}
			}
		);
	}
}
