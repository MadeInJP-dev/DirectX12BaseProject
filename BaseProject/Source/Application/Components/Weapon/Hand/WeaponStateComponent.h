#pragma once

#include "Engine/EditorField/EditorField.h"
#include "Engine/EditorField/EditorField.inl"

namespace App::Component
{
	//================================================================================
	// 手に持つ武器のステート(カタログ)
	//
	// 左右関係なく、その武器が「何を・どれくらいの速さと威力で撃つか」を示す。
	// 武器をプレハブにして選ばせるときに、性能として見せる値はここに置く。
	//
	// ・撃ち方(連射・バースト・単発)・熱・弾倉・銃口は WeaponConfig 以下の部品で組む。
	//   手持ちの武器 = このコンポーネント + 撃ち方1つ + (熱 / 弾倉)。
	// ・本体が武器を兼ねる敵も同じ組み方で撃つ(引き金は SelfWeaponTriggerSystem が引く)。
	// ・弾を出すのは WeaponProjectileSpawnSystem。弾の威力はここの bulletAttack で上書きする
	//   (同じ弾のプレハブを武器ごとに違う威力で使えるようにするため)。0 以下ならプレハブの値のまま。
	//================================================================================
	struct WeaponStateComponent
	{
		float bulletSpeed = 20.0f;		// 弾の初速(m/s)
		float bulletAttack = 0.0f;		// 弾の攻撃力。0 以下なら弾のプレハブの値を使う

		// 撃つ弾のプレハブ
		Core::GUID bulletPrefabGUID = {};									// 記録用(セーブされる)
		Engine::Handle<Engine::Resource::Prefab> bulletPrefabHandle = {};	// ランタイム用(発射時に解決)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::WeaponStateComponent>
{
	// コンポーネントはデストラクタが走らないので、参照を返すのはここの仕事
	static void Release(void* a_pData, const Engine::ECS::EngineServices& a_services)
	{
		App::Component::WeaponStateComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponStateComponent>(a_pData);
		a_services.pResourceManager->ReleaseHandle(_comp.bulletPrefabHandle);
	}

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::WeaponStateComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponStateComponent>(a_pData);
		a_ar.Field("bulletSpeed", _comp.bulletSpeed);
		a_ar.Field("bulletAttack", _comp.bulletAttack);
		a_ar.Field("bulletPrefabGUID", _comp.bulletPrefabGUID);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::WeaponStateComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponStateComponent>(a_context.pData);

		Engine::EditorField::Field("Bullet Speed", _comp.bulletSpeed, 0.1f, 0.0f);
		Engine::EditorField::Field("Bullet Attack", _comp.bulletAttack, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("0 以下なら弾のプレハブ(ProjectileComponent)の damage を使う");

		if (Engine::EditorField::AssetField(*a_context.pWorld->RefEngineServices(), "Bullet Prefab", "Prefab", _comp.bulletPrefabGUID))
		{
			// GUIDが変わったらハンドルは作り直す(発射時に再解決)
			a_context.pWorld->RefEngineServices()->pResourceManager->ReleaseHandle(_comp.bulletPrefabHandle);
			_comp.bulletPrefabHandle = {};
		}
	}
};
