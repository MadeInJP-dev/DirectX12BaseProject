#pragma once

#include "Engine/EditorField/EditorField.h"

namespace App::Component
{
	//==========================================================================================
	// BallisticComponent
	//
	// 投げられたものを放物線で飛ばし、地面に当たったら跳ねて、勢いが尽きたら止める。
	// 爆発で飛び散る岩の破片のような、見た目だけの小物に付ける。
	//
	// ・動かすのは BallisticSystem。重力と着地だけを自前で解くので、
	//   DesiredVelocityComponent / MovementIntegrationSystem は使わない(付けないこと)。
	// ・地面として見るのは StaticObject だけ。真下ではなく「進む向き」にレイを打つので、
	//   斜面や壁に当たっても跳ね返る。当たり判定のボディは持たない(何にも当たりに行かない)。
	// ・初速と回転は撒く側(DebrisEmitterSystem)が生成時に書き込む。
	// ・寿命(LifeTimeComponent)の残りが shrinkTime を切ったら縮めていき、0 で消える。
	//   いきなり消えると目に付くので、最後は小さくなって消えるようにしてある。
	// ・止まったら、同じエンティティの EffectAssetComponent(軌跡の砂埃)を止める。
	//==========================================================================================
	struct BallisticComponent
	{
		// ---- 設定(保存) ----
		float gravityScale  = 1.0f;		// 重力の倍率(シーンの重力 ScenePhysics に掛ける)
		float restitution   = 0.35f;	// 跳ね返りの強さ(0で跳ねない / 1で同じ速さで跳ね返る)
		float friction      = 0.4f;		// 着地のたびに地面に沿う速さを落とす割合(0〜1)
		float restSpeed     = 3.0f;		// 跳ね返る速さがこれを下回ったら止まる(m/秒)
		float groundOffset  = 0.3f;		// 中心から接地面までの距離(破片の半径くらい。m)
		float shrinkTime    = 0.4f;		// 寿命の最後にこの秒数をかけて縮んで消える(0で縮まない)
		bool  isStopEffectOnRest = true;	// 止まったら軌跡のエフェクトを止める

		// ---- ランタイム(保存しない) ----
		Math::Vector3 velocity  = { 0.0f, 0.0f, 0.0f };	// 速度(撒く側が書く)
		Math::Vector3 spinAxis  = { 0.0f, 1.0f, 0.0f };	// 回転の軸(単位ベクトル。撒く側が書く)
		float spinSpeedDeg = 0.0f;						// 回転の速さ(度/秒。撒く側が書く)
		Math::Vector3 baseScale = { 1.0f, 1.0f, 1.0f };	// 縮める前の大きさ(最初の更新で覚える)
		bool hasBaseScale = false;
		bool isResting = false;							// 止まったか
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::BallisticComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::BallisticComponent& _comp = Engine::EditorField::RefValue<App::Component::BallisticComponent>(a_pData);
		a_ar.Field("gravityScale", _comp.gravityScale);
		a_ar.Field("restitution", _comp.restitution);
		a_ar.Field("friction", _comp.friction);
		a_ar.Field("restSpeed", _comp.restSpeed);
		a_ar.Field("groundOffset", _comp.groundOffset);
		a_ar.Field("shrinkTime", _comp.shrinkTime);
		a_ar.Field("isStopEffectOnRest", _comp.isStopEffectOnRest);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::BallisticComponent& _comp = Engine::EditorField::RefValue<App::Component::BallisticComponent>(a_context.pData);
		Engine::EditorField::Field("Gravity Scale", _comp.gravityScale, 0.01f);
		Engine::EditorField::Field("Restitution", _comp.restitution, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("Friction", _comp.friction, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("Rest Speed", _comp.restSpeed, 0.05f, 0.0f);
		Engine::EditorField::Field("Ground Offset", _comp.groundOffset, 0.01f, 0.0f);
		Engine::EditorField::Field("Shrink Time", _comp.shrinkTime, 0.01f, 0.0f);
		Engine::EditorField::Field("Stop Effect On Rest", _comp.isStopEffectOnRest);

		// 実行中の値は表示のみ
		Engine::EditorField::Value("Velocity", "%.1f, %.1f, %.1f (%s)", _comp.velocity.x, _comp.velocity.y, _comp.velocity.z, _comp.isResting ? "rest" : "flying");
	}
};
