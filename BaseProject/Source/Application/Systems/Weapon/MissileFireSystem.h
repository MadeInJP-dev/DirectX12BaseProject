#pragma once

#include "Application/ECS/ISystem/APPISystem.h"

namespace App::System
{
	//==========================================================================================
	// MissileFireSystem
	//
	// 肩の武器(MissileFireComponent)が受けた一斉射の要求を、武器の側で撃ち出す。
	// (旧 MissileSalvo。持ち主の側で撃っていたものを武器の側へ移した)
	//
	//   要求を出す : MissileSalvoSystem(プレイヤー) / BossMissileSalvoSystem(ボス)
	//   撃つ       : ここ。MissileStateComponent の弾を、MuzzleComponent を基準に並べた位置から、
	//                散らし角を付けて burstTime 間隔で撃つ。撃ち切ったら waitTime 待つ
	//
	// ・何発を・どの間隔で・どこから撃つかを武器が持つので、武器を付け替えれば撃ち方も変わる。
	//   持ち主は「誰を狙うか」を渡すだけ。
	// ・PostUpdate に置く理由
	//     発射位置を武器の「今フレームの」ワールド行列から取るため。
	//     要求を出す側(PostUpdate)の後ろへ回す。
	// ・弾の生成は ProjectileSpawn 経由(遅延生成)なのでメインスレッドで回す。
	//==========================================================================================
	class MissileFireSystem : public App::ECS::APPISystem
	{
	public:

		void Init(App::ECS::APPWorld& a_world) override;
	};
}
