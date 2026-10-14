#pragma once

#include "Engine/ECS/World/World.h"

namespace Engine::Resource { class Prefab; }

//==========================================================================================
// ProjectileSpawn
//
// 「プレハブから弾を1発出す」処理をまとめたヘルパー。
// WeaponProjectileSpawnSystem(手持ちの武器)と MissileFireSystem(肩のミサイル)が共有する。
//
// プレハブの実体化・最低限のコンポーネント補完・発射元/誘導相手の埋め込みは
// どちらの武器でも同じなので、片方だけ直して食い違うことがないようにここへ寄せる。
// 「どこから・どっちへ・誰を狙って撃つか」は武器ごとに違うので呼び出し側の担当。
//==========================================================================================
namespace App::System::ProjectileSpawn
{
	/// <summary>
	/// 「発射元」として弾に持たせるエンティティを解決する
	/// </summary>
	/// <remarks>
	/// 銃は本体にぶら下がる子エンティティのことがあり、コライダーを持つのは本体側
	/// (銃自体はコリジョンワールドに登録されていない)。弾が当たらないようにしたいのは
	/// 本体なので、自分 → 親 と辿って最初に ColliderComponent を持つものを発射元とする。
	/// 銃が本体そのもの(敵など)なら自分がそのまま返る。
	/// 誰もコライダーを持たなければ、辿れた最上位を返す。
	/// </remarks>
	Engine::ECS::Entity ResolveShooterEntity(
		Engine::ECS::World& a_world,
		Engine::ECS::Entity a_gunEntity);

	/// <summary>
	/// プレハブから弾を1発生成する
	/// </summary>
	/// <param name="a_pos">発射位置(ワールド)</param>
	/// <param name="a_velocity">初速ベクトル(向き × 弾速)</param>
	/// <param name="a_shooter">発射元。弾が自分に当たらないようにするため</param>
	/// <param name="a_homingTarget">誘導弾なら追う相手。不要なら INVALID_ENTITY</param>
	/// <param name="a_damage">弾の威力。0 以下ならプレハブ(ProjectileComponent)の値のまま。
	/// 威力は武器の性能(WeaponStateComponent / MissileStateComponent)なので、同じ弾を武器ごとに使い分けられるようにする</param>
	/// <remarks>
	/// システム反復中に即時生成するとアーキタイプが壊れるため、World の遅延生成
	/// コマンド(ReserveCreateEntityWithData)へ積む。実際の生成は BeginFrame。
	/// </remarks>
	void Spawn(
		Engine::ECS::World&       a_world,
		Engine::Resource::Prefab* a_pPrefab,
		const Math::Vector3&  a_pos,
		const Math::Vector3&  a_velocity,
		Engine::ECS::Entity       a_shooter,
		Engine::ECS::Entity       a_homingTarget,
		float                     a_damage = 0.0f);
}
