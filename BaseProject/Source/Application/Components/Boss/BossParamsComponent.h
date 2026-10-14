#pragma once

#include "BossBrainStateComponent.h"
#include "BossCommandComponent.h"

namespace App::Component
{
	//==========================================================================================
	// BossParamsComponent
	//
	// 人型ボス(アーマードコア / オメガフェニックス型)の戦闘設定。保存される。
	// 実行中の値は次の2つに分けてあり、どちらも必須コンポーネントとして自動で付く。
	//   BossBrainStateComponent … 行動パターン・機動・各タイマー(BossCombatIntentSystem だけが書く)
	//   BossCommandComponent    … 戦闘開始の命令と、ミサイル一斉射の要求(外とのやり取り)
	// 以前は BossComponent に全部入っていて、要求を消すだけの側まで設定と同じ型を書いていた。
	//
	// 中身を進めるのは BossCombatIntentSystem(PreUpdate)と BossMissileSalvoSystem(PostUpdate)。
	//
	// ・ザコ敵(PatrolComponent)との違い
	//     ザコは「徘徊 → 発見 → 追跡 → 攻撃」を距離だけで自動的に始める。
	//     ボスは徘徊せず、シーケンス(SceneSequence)から戦闘開始命令が届くまで待機する。
	//     戦闘に入ってからは地面を歩かず、ブーストで空中を高速に動き回りながら撃ち合う。
	//
	// ・動かし方はプレイヤーと同じ部品を使い回している
	//     LookAngleComponent  … 「どこを向いているか」。ボスAIが相手の方向へ毎フレーム寄せる。
	//                           機体の向きは RotationSystem、上体の狙いは AdditivePoseSystem、
	//                           ブーストの向きは RobotBoostSystem がこの角度を読む。
	//     MoveIntentComponent … 視点基準の移動入力(x=横 / y=上下 / z=前後)。プレイヤーの
	//                           入力とまったく同じ意味なので CharacterMovementSystem が使える。
	//     BoostParamsComponent      … ブースト入力。RobotBoostSystem がそのまま推力に変える。
	//     ActionIntent / AimTargetPos
	//                         … 銃の発射入力と狙点。AttachmentDispatchSystem が武器の子
	//                           エンティティへ配信し、撃ち方のシステムが撃つ。
	//   つまりボス用に増やしたのは「入力を作る側」だけで、動かす側は全部既存のもの。
	//
	// ・撃つ相手は TargetEntityComponent(SearchPlayerSystem が解決する)。
	//   戦闘の開始/終了は距離ではなく命令で決めるので、発見距離は広めに取っておくこと
	//   (弾の誘導先を引く WeaponProjectileSpawnSystem が isFind を見るため)。
	//==========================================================================================
	struct BossParamsComponent
	{
		// ---- 戦闘開始 ----
		// 命令(BossCommandComponent::isCombatStarted)を待たずに開始する(単体で動きを見たいとき用)
		bool startOnSpawn    = false;

		// ---- 間合い(保存される) ----
		// 保ちたい距離を1点ではなく幅で持つ。境界ちょうどを狙うと詰める/下がるを
		// 毎フレーム往復してしまうため(ザコの keepMargin と同じ考え)。
		// ここは Standoff(既定)の値で、行動パターンごとに差し替わる。
		float keepDistance = 45.0f;		// 保ちたい間合い(m)
		float keepMargin   = 12.0f;		// その許容幅(±m)。この中では前後に動かない
		float keepHeight   = 10.0f;		// プレイヤーからどれだけ上に居たいか(m)
		float heightMargin = 3.0f;		// 高さの許容幅(±m)

		// ---- 行動パターン(保存される) ----
		// 一定時間ごとに抽選し直す。パターンは位置取りの目標を差し替えるだけで、
		// そこへ行く手順は共通(間合いを保つ / 横へ流す / 高度を合わせる)。
		float patternDuration     = 4.0f;	// 1つのパターンを続ける時間(秒)
		float patternDurationRand = 1.5f;	// その揺らぎ(±秒)

		float rushDistance     = 12.0f;		// Rush       : 詰める間合い(m)
		float rushHeight       = 3.0f;		// Rush       : そのときの高さ(相手から±m)
		float highGroundHeight = 35.0f;		// HighGround : 相手からどれだけ上へ(m)
		float lowGroundHeight  = -8.0f;		// LowGround  : 相手からどれだけ下へ(m。負で下)
		float retreatDistance  = 90.0f;		// Retreat    : 離れる間合い(m)
		float orbitStrafeScale = 1.2f;		// Orbit      : 横移動の強さ倍率

		// 抽選の重み。0 にするとそのパターンは出なくなる。
		// 「1つだけ 0 以外」にすれば、そのパターンだけで戦わせて動きを確認できる。
		float weightStandoff   = 3.0f;
		float weightRush       = 2.0f;
		float weightHighGround = 2.0f;
		float weightLowGround  = 1.5f;
		float weightOrbit      = 2.0f;
		float weightRetreat    = 1.0f;

		// ---- 旋回(保存される) ----
		float turnSpeedDeg  = 220.0f;	// 視点の水平旋回速度(度/秒)
		float pitchSpeedDeg = 160.0f;	// 視点の上下旋回速度(度/秒)
		float maxPitchDeg   = 60.0f;	// 見上げ/見下ろしの限界(±度)

		// ---- 機動(保存される) ----
		// スロットルは MoveIntent の大きさ(0..1)。実速度は MovementParamsComponent.moveSpeed と
		// BoostParamsComponent.boostPower 側で決まる。
		float strafeInterval     = 1.4f;	// 横移動の向きを切り替える間隔(秒)
		float strafeIntervalRand = 0.8f;	// その揺らぎ(±秒)。同じ周期で往復すると読まれてしまう
		float strafeThrottle     = 1.0f;	// 横移動のスロットル(0..1)

		// ---- 切り返しの静止(保存される) ----
		// 撃ち合いの最中、横に振る動きの折り返しで足を止める。
		// 常に横へ流れ続けていると狙いを置く先が定まらず、当てるのがほぼ運になってしまうため、
		// 「止まっているあいだは狙って当てられる」という隙をこちらから作る。
		// 詰め(Rush)と離脱(Retreat)では止まらない(移動そのものが目的の動きなので)。
		float strafeHoldChance   = 0.5f;	// 折り返しで止まる確率(0..1)。0 で止まらない
		float strafeHoldTimeMin  = 1.0f;	// 止まっている時間の下限(秒)
		float strafeHoldTimeMax  = 2.0f;	// 止まっている時間の上限(秒)
		float approachThrottle   = 1.0f;	// 詰めるときのスロットル(0..1)
		float backThrottle       = 1.0f;	// 下がるときのスロットル(0..1)
		float verticalThrottle   = 1.0f;	// 上下移動のスロットル(0..1)

		float boostFuelReserve   = 12.0f;	// 残量がこれを下回ったらブーストを休む(0 なら切れるまで吹かす)
		float dashInterval       = 2.2f;	// クイックブースト(単押し)の間隔(秒)
		float dashIntervalRand   = 1.0f;	// その揺らぎ(±秒)

		// ---- 銃(保存される) ----
		float gunRange     = 120.0f;	// この距離まで詰めたら撃つ(m)
		float gunConeDeg   = 35.0f;		// 正面この角度に相手が入っていたら撃つ(度)
		float gunBurstTime = 1.6f;		// 撃ち続ける時間(秒)
		float gunRestTime  = 0.9f;		// 撃つのを休む時間(秒)

		// ---- 狙い(保存される) ----
		float aimOffsetY   = 3.0f;		// 狙点を相手の原点から上へずらす量(m)。原点が足元のモデル用
		float aimLeadScale = 1.0f;		// 偏差撃ちの強さ。0 で置き撃ちなし、1 で弾速から求めた分だけ先を狙う

		// ---- ミサイル(保存される) ----
		float missileRange        = 160.0f;	// この距離まで詰めたら一斉射する(m)
		float missileInterval     = 6.0f;	// 一斉射の間隔(秒)
		float missileIntervalRand = 2.0f;	// その揺らぎ(±秒)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::BossParamsComponent>
{
	// 思考の状態と命令は実行中の値なので、プレハブに書かずに自動で付ける
	using Requires = Engine::ECS::RequireComponents<App::Component::BossBrainStateComponent, App::Component::BossCommandComponent>;

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::BossParamsComponent& _comp = Engine::EditorField::RefValue<App::Component::BossParamsComponent>(a_pData);

		a_ar.Field("startOnSpawn", _comp.startOnSpawn);

		a_ar.Field("keepDistance", _comp.keepDistance);
		a_ar.Field("keepMargin", _comp.keepMargin);
		a_ar.Field("keepHeight", _comp.keepHeight);
		a_ar.Field("heightMargin", _comp.heightMargin);

		a_ar.Field("patternDuration", _comp.patternDuration);
		a_ar.Field("patternDurationRand", _comp.patternDurationRand);
		a_ar.Field("rushDistance", _comp.rushDistance);
		a_ar.Field("rushHeight", _comp.rushHeight);
		a_ar.Field("highGroundHeight", _comp.highGroundHeight);
		a_ar.Field("lowGroundHeight", _comp.lowGroundHeight);
		a_ar.Field("retreatDistance", _comp.retreatDistance);
		a_ar.Field("orbitStrafeScale", _comp.orbitStrafeScale);

		a_ar.Field("weightStandoff", _comp.weightStandoff);
		a_ar.Field("weightRush", _comp.weightRush);
		a_ar.Field("weightHighGround", _comp.weightHighGround);
		a_ar.Field("weightLowGround", _comp.weightLowGround);
		a_ar.Field("weightOrbit", _comp.weightOrbit);
		a_ar.Field("weightRetreat", _comp.weightRetreat);

		a_ar.Field("turnSpeedDeg", _comp.turnSpeedDeg);
		a_ar.Field("pitchSpeedDeg", _comp.pitchSpeedDeg);
		a_ar.Field("maxPitchDeg", _comp.maxPitchDeg);

		a_ar.Field("strafeInterval", _comp.strafeInterval);
		a_ar.Field("strafeIntervalRand", _comp.strafeIntervalRand);
		a_ar.Field("strafeThrottle", _comp.strafeThrottle);
		a_ar.Field("strafeHoldChance", _comp.strafeHoldChance);
		a_ar.Field("strafeHoldTimeMin", _comp.strafeHoldTimeMin);
		a_ar.Field("strafeHoldTimeMax", _comp.strafeHoldTimeMax);
		a_ar.Field("approachThrottle", _comp.approachThrottle);
		a_ar.Field("backThrottle", _comp.backThrottle);
		a_ar.Field("verticalThrottle", _comp.verticalThrottle);

		a_ar.Field("boostFuelReserve", _comp.boostFuelReserve);
		a_ar.Field("dashInterval", _comp.dashInterval);
		a_ar.Field("dashIntervalRand", _comp.dashIntervalRand);

		a_ar.Field("gunRange", _comp.gunRange);
		a_ar.Field("gunConeDeg", _comp.gunConeDeg);
		a_ar.Field("gunBurstTime", _comp.gunBurstTime);
		a_ar.Field("gunRestTime", _comp.gunRestTime);

		a_ar.Field("aimOffsetY", _comp.aimOffsetY);
		a_ar.Field("aimLeadScale", _comp.aimLeadScale);

		a_ar.Field("missileRange", _comp.missileRange);
		a_ar.Field("missileInterval", _comp.missileInterval);
		a_ar.Field("missileIntervalRand", _comp.missileIntervalRand);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::BossParamsComponent& _comp = Engine::EditorField::RefValue<App::Component::BossParamsComponent>(a_context.pData);

		Engine::EditorField::Header("Combat Start");
		Engine::EditorField::Field("StartOnSpawn", _comp.startOnSpawn);
		Engine::EditorField::Tooltip("no order needed");

		Engine::EditorField::Header("Range (Standoff)");
		Engine::EditorField::Field("KeepDistance", _comp.keepDistance, 0.5f, 0.0f);
		Engine::EditorField::Field("KeepMargin", _comp.keepMargin, 0.1f, 0.0f);
		Engine::EditorField::Field("KeepHeight", _comp.keepHeight, 0.1f);
		Engine::EditorField::Field("HeightMargin", _comp.heightMargin, 0.1f, 0.0f);

		Engine::EditorField::Header("Pattern");
		Engine::EditorField::Field("PatternDuration", _comp.patternDuration, 0.1f, 0.0f);
		Engine::EditorField::Field("PatternDurationRand", _comp.patternDurationRand, 0.1f, 0.0f);
		Engine::EditorField::Field("RushDistance", _comp.rushDistance, 0.5f, 0.0f);
		Engine::EditorField::Field("RushHeight", _comp.rushHeight, 0.1f);
		Engine::EditorField::Field("HighGroundHeight", _comp.highGroundHeight, 0.5f);
		Engine::EditorField::Field("LowGroundHeight", _comp.lowGroundHeight, 0.5f);
		Engine::EditorField::Field("RetreatDistance", _comp.retreatDistance, 0.5f, 0.0f);
		Engine::EditorField::Field("OrbitStrafeScale", _comp.orbitStrafeScale, 0.05f, 0.0f, 3.0f);

		Engine::EditorField::HelpText("Weight (0 = never picked)");
		Engine::EditorField::Field("W:Standoff", _comp.weightStandoff, 0.1f, 0.0f);
		Engine::EditorField::Field("W:Rush", _comp.weightRush, 0.1f, 0.0f);
		Engine::EditorField::Field("W:HighGround", _comp.weightHighGround, 0.1f, 0.0f);
		Engine::EditorField::Field("W:LowGround", _comp.weightLowGround, 0.1f, 0.0f);
		Engine::EditorField::Field("W:Orbit", _comp.weightOrbit, 0.1f, 0.0f);
		Engine::EditorField::Field("W:Retreat", _comp.weightRetreat, 0.1f, 0.0f);

		Engine::EditorField::Header("Turn");
		Engine::EditorField::Field("TurnSpeedDeg", _comp.turnSpeedDeg, 1.0f, 0.0f);
		Engine::EditorField::Field("PitchSpeedDeg", _comp.pitchSpeedDeg, 1.0f, 0.0f);
		Engine::EditorField::Field("MaxPitchDeg", _comp.maxPitchDeg, 1.0f, 0.0f, 89.0f);

		Engine::EditorField::Header("Maneuver");
		Engine::EditorField::Field("StrafeInterval", _comp.strafeInterval, 0.05f, 0.0f);
		Engine::EditorField::Field("StrafeIntervalRand", _comp.strafeIntervalRand, 0.05f, 0.0f);
		Engine::EditorField::Field("StrafeThrottle", _comp.strafeThrottle, 0.01f, 0.0f, 1.0f);

		Engine::EditorField::HelpText("Hold (pause at strafe turn-around)");
		Engine::EditorField::Field("StrafeHoldChance", _comp.strafeHoldChance, 0.01f, 0.0f, 1.0f);
		if (Engine::EditorField::Field("StrafeHoldTimeMin", _comp.strafeHoldTimeMin, 0.05f, 0.0f))
		{
			if (_comp.strafeHoldTimeMax < _comp.strafeHoldTimeMin) _comp.strafeHoldTimeMax = _comp.strafeHoldTimeMin;
		}
		if (Engine::EditorField::Field("StrafeHoldTimeMax", _comp.strafeHoldTimeMax, 0.05f, 0.0f))
		{
			if (_comp.strafeHoldTimeMax < _comp.strafeHoldTimeMin) _comp.strafeHoldTimeMin = _comp.strafeHoldTimeMax;
		}
		Engine::EditorField::Field("ApproachThrottle", _comp.approachThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("BackThrottle", _comp.backThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("VerticalThrottle", _comp.verticalThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("BoostFuelReserve", _comp.boostFuelReserve, 0.5f, 0.0f);
		Engine::EditorField::Field("DashInterval", _comp.dashInterval, 0.05f, 0.0f);
		Engine::EditorField::Field("DashIntervalRand", _comp.dashIntervalRand, 0.05f, 0.0f);

		Engine::EditorField::Header("Gun");
		Engine::EditorField::Field("GunRange", _comp.gunRange, 1.0f, 0.0f);
		Engine::EditorField::Field("GunConeDeg", _comp.gunConeDeg, 1.0f, 0.0f, 180.0f);
		Engine::EditorField::Field("GunBurstTime", _comp.gunBurstTime, 0.05f, 0.0f);
		Engine::EditorField::Field("GunRestTime", _comp.gunRestTime, 0.05f, 0.0f);

		Engine::EditorField::Header("Aim");
		Engine::EditorField::Field("AimOffsetY", _comp.aimOffsetY, 0.05f);
		Engine::EditorField::Field("AimLeadScale", _comp.aimLeadScale, 0.05f, 0.0f, 3.0f);

		Engine::EditorField::Header("Missile");
		Engine::EditorField::Field("MissileRange", _comp.missileRange, 1.0f, 0.0f);
		Engine::EditorField::Field("MissileInterval", _comp.missileInterval, 0.1f, 0.0f);
		Engine::EditorField::Field("MissileIntervalRand", _comp.missileIntervalRand, 0.1f, 0.0f);
	}
};
