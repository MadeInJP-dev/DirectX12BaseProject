#pragma once

#include "Engine/EditorField/EditorField.h"
#include "Engine/EditorField/EditorField.inl"

namespace App::Component
{
	//================================================================================
	// 肩につける武器(ミサイル)のステート(カタログ)
	//
	// 手持ちの WeaponStateComponent と同じく、「何を・どれくらいの速さと威力で撃つか」を示す。
	// 撃ち方(何発を・どの間隔で・どこから)は MissileFireComponent。
	//
	// ・肩の武器 = このコンポーネント + MissileFireComponent(+ 銃口は必須で付く)。
	// ・ミサイルの威力はここの missileAttack で上書きする。0 以下ならプレハブの値のまま。
	// ・誘導の強さ(旋回の速さ)はミサイルのプレハブの HomingComponent が持つ。
	//================================================================================
	struct MissileStateComponent
	{
		float missileSpeed = 8.0f;		// ミサイルの初速(m/s)
		float missileAttack = 0.0f;		// ミサイルの攻撃力。0 以下ならプレハブの値を使う

		// 撃つミサイルのプレハブ
		Core::GUID missilePrefabGUID = {};									// 記録用(セーブされる)
		Engine::Handle<Engine::Resource::Prefab> missilePrefabHandle = {};	// ランタイム用(発射時に解決)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::MissileStateComponent>
{
	static void Release(void* a_pData, const Engine::ECS::EngineServices& a_services)
	{
		App::Component::MissileStateComponent& _comp = Engine::EditorField::RefValue<App::Component::MissileStateComponent>(a_pData);
		a_services.pResourceManager->ReleaseHandle(_comp.missilePrefabHandle);
	}

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::MissileStateComponent& _comp = Engine::EditorField::RefValue<App::Component::MissileStateComponent>(a_pData);
		a_ar.Field("missileSpeed", _comp.missileSpeed);
		a_ar.Field("missileAttack", _comp.missileAttack);
		a_ar.Field("missilePrefabGUID", _comp.missilePrefabGUID);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::MissileStateComponent& _comp = Engine::EditorField::RefValue<App::Component::MissileStateComponent>(a_context.pData);

		Engine::EditorField::Field("Missile Speed", _comp.missileSpeed, 0.1f, 0.0f);
		Engine::EditorField::Field("Missile Attack", _comp.missileAttack, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("0 以下ならミサイルのプレハブ(ProjectileComponent)の damage を使う");

		if (Engine::EditorField::AssetField(*a_context.pWorld->RefEngineServices(), "Missile Prefab", "Prefab", _comp.missilePrefabGUID))
		{
			a_context.pWorld->RefEngineServices()->pResourceManager->ReleaseHandle(_comp.missilePrefabHandle);
			_comp.missilePrefabHandle = {};
		}
	}
};
