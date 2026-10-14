#pragma once

namespace App::Component
{
	//==========================================================================================
	// WeaponFireComponent
	//
	// このフレームに撃ったか、撃ったなら銃口の位置と向き。(旧 GunFireComponent)
	//
	// ・撃つかどうかは撃ち方のシステム(AutoFire / BurstFire / SemiAutoFire)が毎フレーム書き直す。
	//   位置と向きはその後で WeaponMuzzleSystem が MuzzleComponent から埋める。
	//   読むのは WeaponProjectileSpawnSystem(弾の生成)と MuzzleFlashSystem(銃口の光)。
	// ・判定は自分の武器の値だけで決まるのでチャンク並列に回し、
	//   生成(構造変更の予約)と再生(エフェクト)だけをメインスレッドに残している。
	//   共有の配列(Resource)へ積まず武器ごとに持つのは、Job から同時に積むと取り合いになるため。
	// ・撃ち方のコンポーネントの必須コンポーネントなので、プレハブに書かなくても付く。保存しない。
	//==========================================================================================
	struct WeaponFireComponent
	{
		bool isFired = false;									// このフレームに撃ったか

		Math::Vector3 spawnPos = { 0.0f, 0.0f, 0.0f };			// 弾を出す位置(ワールド)
		Math::Vector3 shootDir = { 0.0f, 0.0f, 1.0f };			// 射出方向(ワールド、単位ベクトル)
		Math::Vector3 muzzleLocalPos = { 0.0f, 0.0f, 0.0f };	// 武器ローカルの銃口位置(マズルフラッシュ用)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::WeaponFireComponent>
{
	static void Edit(CompEditContext& a_context)
	{
		App::Component::WeaponFireComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponFireComponent>(a_context.pData);
		Engine::EditorField::Value("Fired", "%s", _comp.isFired ? "true" : "false");
	}
};
