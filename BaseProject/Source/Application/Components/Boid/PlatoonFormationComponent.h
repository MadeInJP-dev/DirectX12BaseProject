#pragma once

#include "Engine/EditorField/EditorField.h"

namespace App::Component
{
	//==========================================================================================
	// PlatoonFormationComponent
	//
	// 小隊長への「円陣の置き場所へ行け」という命令(ワームボスの円陣ミサイル)。
	//
	// ・書くのは SwarmBossController(行動が頼んだ渦巻きの円陣から、小隊長ごとの置き場所を決めて書く)。
	// ・読むのは PlatoonFollowSystem と SwarmLookSystem。
	//     isActive のとき … 前の相手を追わずに slotPos へ寄る(速さは moveSpeed で頭打ち)
	//     isLookAt のとき … 進む向きではなく lookDir を向く(ボイドは小隊長の向きへ寄せるので一緒に向く)
	// ・isActive が false なら今までどおり前の相手の後ろを追う。
	// ・毎フレーム書き直す実行中の値なので保存しない。
	//==========================================================================================
	struct PlatoonFormationComponent
	{
		bool isActive = false;			// 置き場所へ向かうか
		bool isLookAt = false;			// lookDir を向くか(false なら進む向き)
		Math::Vector3 slotPos = {};		// 置き場所(ワールド)
		Math::Vector3 lookDir = {};		// 向ける向き(長さは問わない)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::PlatoonFormationComponent>
{
	static void Edit(CompEditContext& a_context)
	{
		App::Component::PlatoonFormationComponent& _comp = Engine::EditorField::RefValue<App::Component::PlatoonFormationComponent>(a_context.pData);
		// 毎フレーム書き直される値なので表示のみ
		Engine::EditorField::Value("Active", "%s", _comp.isActive ? "true" : "false");
		Engine::EditorField::Value("LookAt", "%s", _comp.isLookAt ? "true" : "false");
		Engine::EditorField::Value("SlotPos", "%.1f, %.1f, %.1f", _comp.slotPos.x, _comp.slotPos.y, _comp.slotPos.z);
		Engine::EditorField::Value("LookDir", "%.2f, %.2f, %.2f", _comp.lookDir.x, _comp.lookDir.y, _comp.lookDir.z);
	}
};
