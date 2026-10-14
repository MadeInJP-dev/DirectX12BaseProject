#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::Component
{
	struct MuzzleComponent;
	struct ModelComponent;
}

namespace Engine::Resource
{
	class ResourceManager;
}

namespace App::System
{
	//==========================================================================================
	// WeaponMuzzleSystem
	//
	// このフレームに撃った武器(WeaponFireComponent::isFired)の、弾を出す位置と向きを
	// 銃口(MuzzleComponent)と狙点(AimResultComponent)から決めて WeaponFireComponent へ書く。
	//
	// ・撃つかどうかの判定(撃ち方のシステム)から切り離してあるのは、撃ち方が3つあっても
	//   「どこから・どっちへ」は同じだから。撃ち方を足しても、ここは触らずに済む。
	// ・狙点がある武器は銃口から狙点へ向けて撃つ。無い武器は銃口の向き(MuzzleComponent::dir)へ。
	//==========================================================================================
	class WeaponMuzzleSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};

	namespace WeaponMuzzle
	{
		/// <summary>
		/// 銃口の位置を武器ローカルで求める(銃口ノード + offset)
		/// </summary>
		/// <param name="a_pModel">武器のモデル。無ければ武器の原点を銃口とする</param>
		/// <remarks>
		/// ノードの行列はモデルルート基準なので、その平行移動はそのまま武器ローカルの位置になる。
		/// ミサイルの発射位置(MissileFireSystem)もここを基準に並べる
		/// </remarks>
		Math::Vector3 GetMuzzleLocalPos(
			const Engine::Resource::ResourceManager& a_resourceManager,
			const Component::MuzzleComponent& a_muzzle,
			const Component::ModelComponent* a_pModel);
	}
}
