#pragma once

namespace App::Component
{
	//==========================================================================================
	// AimPointComponent
	//
	// 狙われる側に付ける「ここを狙ってほしい」点。
	// 原点が足元にあるモデル(プレイヤーの機体など)で、追ってくるミサイルが足元へ刺さらず
	// 胴体の中心あたりへ向かうようにするためのもの。
	//
	// ・localPos は持ち主のローカル空間の点。ワールド行列を掛けて使うので、
	//   持ち主の拡大・回転にそのまま付いていく(プレイヤーは scale 3 なので、ワールドでは 3 倍の高さ)。
	// ・読むのは誘導(HomingSystem)と、撃つ瞬間の向きを決める MissileFireSystem / BossMissileSalvoSystem。
	//   持っていない相手は今までどおり原点を狙う(GetAimPointPos)。
	// ・撃つ側の targetOffsetY(MissileLockComponent)はこの点からさらに上へずらす量になる。
	//==========================================================================================
	struct AimPointComponent
	{
		Math::Vector3 localPos = { 0.0f, 0.0f, 0.0f };	// 狙ってほしい点(持ち主のローカル空間)
	};

	//==========================================================================================
	// 相手の狙う位置(ワールド)。AimPointComponent が無ければ原点。
	//
	// a_worldMat は相手の WorldMatrixComponent の値(呼ぶ側が引いてあるものを渡す)
	//==========================================================================================
	inline Math::Vector3 GetAimPointPos(Engine::ECS::World& a_world, const Engine::ECS::Entity& a_entity, const Math::Matrix& a_worldMat)
	{
		if (a_world.HasComponent<AimPointComponent>(a_entity))
		{
			if (const AimPointComponent* _pAimPoint = a_world.RefData<AimPointComponent>(a_entity))
			{
				return Math::Vector3::Transform(_pAimPoint->localPos, a_worldMat);
			}
		}
		return a_worldMat.Translation();
	}
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::AimPointComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::AimPointComponent& _comp = Engine::EditorField::RefValue<App::Component::AimPointComponent>(a_pData);
		a_ar.Field("localPos", _comp.localPos);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::AimPointComponent& _comp = Engine::EditorField::RefValue<App::Component::AimPointComponent>(a_context.pData);
		Engine::EditorField::Field("LocalPos", _comp.localPos, 0.01f);
		Engine::EditorField::Tooltip("狙ってほしい点(持ち主のローカル空間)。ワールド行列を掛けて使うので拡大も効く");
	}
};
