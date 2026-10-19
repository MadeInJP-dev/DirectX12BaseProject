#pragma once

#include "../StateMachine.h"

namespace App::Object
{
	/// <summary>
	/// 徘徊 : 生成位置のまわりでランダムに目標地点を選び、そこへ向かう
	/// </summary>
	/// <remarks>
	/// コントローラーはプレイヤーのキーボード/マウスと同じ立場で、作るのは移動入力だけ。
	/// 入力を速度へ変えるのは SwarmLeaderMoveSystem、向きを変えるのは SwarmLookSystem。
	///
	/// 目標地点は「着いたら」か「時間が来たら」選び直す。時間切れも見るのは、
	/// 障害物などで着けないまま止まってしまわないようにするため。
	///
	/// 次に出す攻撃は入った瞬間に、今のフェーズ(SwarmBossPhase)の重みで抽選する
	/// </remarks>
	class SwarmBossRandomWalkState : public IState
	{
	public:
		void Enter(SwarmBossStateContext& a_context) override;
		void Update(SwarmBossStateContext& a_context) override;
		void Exit(SwarmBossStateContext& a_context) override;

		void Archive(Engine::Persistence::Archive& a_ar) override;
		void DrawInspector() override;

	private:
		// 次の目標地点を抽選する(生成位置を中心にした範囲の中)
		void PickWanderTarget(const Math::Vector3& a_center);

	private:
		//------------------------------------------------------------------------------------------
		// 調整値
		//------------------------------------------------------------------------------------------
		float m_wanderRadius   = 200.0f;	// 生成位置からこの半径内で目標地点を選ぶ(水平)
		float m_wanderHeight   = 60.0f;		// 高さの振れ幅(生成位置から ±m)
		float m_wanderInterval = 10.0f;		// 目標地点を選び直す間隔(秒)
		float m_arriveDistance = 20.0f;		// この距離まで近づいたら次の目標地点へ
		float m_throttle       = 1.0f;		// 移動入力の強さ(0〜1)

		// 攻撃には一定時間で遷移する(入るたびに最低〜最大の間で抽選)
		float m_maxDurationTime = 14.0f;	// 次に攻撃に移行するまでの最大時間(秒)
		float m_minDurationTime = 8.0f;		// 最低時間(秒)

		//------------------------------------------------------------------------------------------
		// 実行中の状態(保存しない)
		//------------------------------------------------------------------------------------------
		Math::Vector3 m_targetPos = {};		// リーダーのターゲット位置(ワールド)
		float m_wanderTimer = 0.0f;			// 次に選び直すまでの残り時間(秒)

		float m_time = 0.0f;				// このステートになってからの経過時間
		float m_attackTime = 0.0f;			// 攻撃に移行する時間(Enter で抽選)
		ESwarmBossState m_nextAttack = ESwarmBossState::Charge;	// 次に出す攻撃(Enter で抽選)
	};
}
