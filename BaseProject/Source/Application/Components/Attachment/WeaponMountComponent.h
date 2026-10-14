#pragma once

namespace App::Component
{
	// 武器の取り付け口の種類
	enum class EWeaponMount : uint32_t
	{
		RightHand,	// 右手 : 手持ちの武器(右クリック)
		LeftHand,	// 左手 : 手持ちの武器(左クリック)
		Shoulder,	// 肩   : 肩の武器(ミサイル)
	};

	//==========================================================================================
	// WeaponMountComponent
	//
	// 機体(プレイヤーの本体)の子に置く「武器の取り付け口」の印。
	//
	// ・取り付け口は手や肩のノードに FollowAnimationNodeComponent で追従するだけの空のエンティティ。
	//   武器はその子として出す(PlayerSpawner が出撃構成から付ける)。
	// ・手の位置(どのノードか)は機体ごとに違い、握り方のずれ(武器のローカル位置)は武器ごとに違う。
	//   取り付け口を挟むと、機体と武器を別々のプレハブにしたまま組み合わせられる。
	// ・付けた武器は、本体の AttachmentSlotsComponent の対応するスロットへ ID で入る
	//   (RightHand → rightWeapon / LeftHand → leftWeapon / Shoulder → missile)。
	//==========================================================================================
	struct WeaponMountComponent
	{
		EWeaponMount mount = EWeaponMount::RightHand;
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::WeaponMountComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::WeaponMountComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponMountComponent>(a_pData);
		a_ar.Field("mount", _comp.mount);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::WeaponMountComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponMountComponent>(a_context.pData);
		Engine::EditorField::Field("Mount", _comp.mount);
		Engine::EditorField::Tooltip("この取り付け口へ付ける武器の種類。出撃構成(GlobalGameContext::loadout)のどれを付けるかが決まる");
	}
};
