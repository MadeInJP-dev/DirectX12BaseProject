#pragma once

#include "../IState.h"

namespace App::Object
{
	/// <summary>
	/// 突進 : 溜めてからプレイヤーへ一直線に突っ込み、通り過ぎたら徘徊へ戻る
	/// </summary>
	/// <remarks>
	/// 流れ : 溜め(Windup) → 突進(Charge) → 余韻(Recover) → 徘徊へ
	///
	/// ・溜めの間は減速しつつプレイヤーへ向きを合わせる(向きを変えるのは SwarmLookSystem)。
	///   溜めが終わった瞬間のプレイヤーの位置へ向きを固定して走り出す。
	/// ・突進中は少しだけ曲がれる(m_homingTurnSpeed)。0 なら完全に直進。
	///   プレイヤーが後ろに回ったら曲げない(Uターンして追い続けないように)。
	/// ・速さは移動入力の長さで上げる(SwarmLeaderMoveSystem は 入力 × moveSpeed)。
	///   小隊長はリーダーの platoonSpeedScale 倍なので、それを超えると列が千切れる。
	/// ・溜めと突進の間は、体のボイドを小隊長の胴回りで回すよう頼む(bodySpinRate = m_spinRate)。
	///   回転はコントローラーがだんだん上げていくので、溜めで回り始めて突進で速くなる。
	/// ・プレイヤーが見つからなければ何もせず徘徊へ戻る
	/// </remarks>
	class SwarmBossChargeState : public IState
	{
	public:
		void Enter(SwarmBossStateContext& a_context) override;
		void Update(SwarmBossStateContext& a_context) override;
		void Exit(SwarmBossStateContext& a_context) override;

		void Archive(Engine::Persistence::Archive& a_ar) override;
		void ArchiveAppended(Engine::Persistence::Archive& a_ar) override;
		void DrawInspector() override;

	private:
		enum class EPhase
		{
			Windup,		// 溜め : 減速してプレイヤーへ向く
			Charge,		// 突進 : 固定した向きへ全力で進む
			Recover,	// 余韻 : 惰性で進んでから徘徊へ
			End,		// 切り替え要求済み(次のフレームで抜ける)
		};

		// 徘徊へ戻す(要求は一度だけ出す)
		void Finish(SwarmBossStateContext& a_context);

		// 今の向きから目標の向きへ、最大 a_maxAngle(ラジアン)だけ回した向き
		static Math::Vector3 RotateTowards(const Math::Vector3& a_from, const Math::Vector3& a_to, float a_maxAngle);

	private:
		//------------------------------------------------------------------------------------------
		// 調整値
		//------------------------------------------------------------------------------------------
		float m_windupTime        = 1.2f;	// 溜めの長さ(秒)
		float m_windupThrottle    = 0.3f;	// 溜め中の移動入力の強さ(0〜1。向きを合わせるのに少しは動かす)
		float m_chargeSpeedScale  = 1.8f;	// 突進の速さ(リーダーの移動速度に対する倍率)
		float m_homingTurnSpeed   = 0.4f;	// 突進中に曲がれる速さ(ラジアン/秒。0で直進)
		float m_maxChargeTime     = 5.0f;	// 突進の最長時間(秒。外れても止まるように)
		float m_overshootDistance = 80.0f;	// プレイヤーをこの距離だけ通り過ぎたら突進をやめる
		float m_recoverTime       = 2.0f;	// 余韻の長さ(秒)
		float m_recoverThrottle   = 0.6f;	// 余韻中の移動入力の強さ(0〜1)
		float m_spinRate          = 1.0f;	// 溜めと突進の間の胴回りの回転の強さ(0〜1。最高速に対する割合。0 で回さない)

		//------------------------------------------------------------------------------------------
		// 実行中の状態(保存しない)
		//------------------------------------------------------------------------------------------
		EPhase m_phase = EPhase::Windup;
		float m_phaseTime = 0.0f;				// 今のフェーズに入ってからの経過時間(秒)
		Math::Vector3 m_chargeDir = {};			// 突進の向き(単位ベクトル)
		Math::Vector3 m_playerPos = {};			// 最後に見たプレイヤーの位置(表示用)
	};
}
