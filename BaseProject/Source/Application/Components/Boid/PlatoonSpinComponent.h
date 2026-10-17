#pragma once

#include "Engine/EditorField/EditorField.h"

namespace App::Component
{
	//==========================================================================================
	// PlatoonSpinComponent
	//
	// 小隊長からボイドへの「胴回りを回れ」という命令。
	//
	// ・書くのは SwarmBossController(行動が頼んだ回転を、毎フレーム小隊長ごとに書く)。
	// ・読むのは FollowLeaderSystem。ボイドの目標地点を、小隊長の位置から
	//   「小隊長の進む向きを軸にした輪」の上へずらす。輪の上の角度 = angle + ボイドごとの位相。
	//   angle が進むので、ボイドは小隊長を起点に胴回りをぐるぐる回る。
	// ・radius が 0 なら今までどおり小隊長の位置を目標にする(回らない)。
	// ・毎フレーム書き直す実行中の値なので保存しない。
	//==========================================================================================
	struct PlatoonSpinComponent
	{
		float angle  = 0.0f;	// 輪の上の基準の角度(ラジアン)
		float radius = 0.0f;	// 輪の半径(m)。0 で回さない
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::PlatoonSpinComponent>
{
	static void Edit(CompEditContext& a_context)
	{
		App::Component::PlatoonSpinComponent& _comp = Engine::EditorField::RefValue<App::Component::PlatoonSpinComponent>(a_context.pData);
		// 毎フレーム書き直される値なので表示のみ
		Engine::EditorField::Value("Angle", "%.1f deg", DirectX::XMConvertToDegrees(_comp.angle));
		Engine::EditorField::Value("Radius", "%.2f m", _comp.radius);
	}
};
