#pragma once

#include "../IState.h"

namespace App::Object
{
	/// <summary>
	/// アッパー : 地面に潜ってプレイヤーの真下へ回り込み、真上へ突き上げる
	/// </summary>
	/// <remarks>
	/// 流れ : 潜る(Burrow) → 地中移動(Approach) → 突き上げ(Uper) → 余韻(Recover) → 徘徊へ
	///
	/// ・地面との関係はリーダーの SerchGroundComponent(SerchGroundSystem が上下にレイを打って書く)を読む。
	/// ・潜るときは地表から m_burrowDepth だけ下を狙う。体(小隊長・ボイド)はリーダーの軌跡を
	///   なぞって付いてくるので、頭が十分深ければ地中移動の間に体が地表から出ない。
	/// ・地中移動は地表の起伏に沿って同じ深さを保ったまま、プレイヤーの真下へ高速で向かう。
	/// ・真下に着いたら(または時間切れで)真上へ突き上げ、プレイヤーの高さを越えたら
	///   突進と同じく惰性で少し進んでから徘徊へ戻る。
	/// ・突き上げに移った瞬間に攻撃の鳴き声を頼む(画面は震わせない)。
	/// ・潜れない(地面が見つからない)/ プレイヤーが居ないときは何もせず徘徊へ戻る。
	/// ・速さは移動入力の長さで上げる(SwarmLeaderMoveSystem は 入力 × moveSpeed)。
	///   小隊長はリーダーの platoonSpeedScale 倍なので、それを超えると列が千切れる。
	/// </remarks>
	class SwarmBossUperAttackState : public IState
	{
	public:
		void Enter(SwarmBossStateContext& a_context) override;
		void Update(SwarmBossStateContext& a_context) override;
		void Exit(SwarmBossStateContext& a_context) override;

		void Archive(Engine::Persistence::Archive& a_ar) override;
		void DrawInspector() override;

	private:
		enum class EPhase
		{
			Burrow,		// 潜る : 地表から決まった深さまで潜る
			Approach,	// 地中移動 : 深さを保ったままプレイヤーの真下へ
			Uper,		// 突き上げ : 真上へ飛び出す
			Recover,	// 余韻 : 惰性で進んでから徘徊へ
			End,		// 切り替え要求済み(次のフレームで抜ける)
		};

		// 次のフェーズへ(経過時間は0から数え直す)
		void ChangePhase(EPhase a_phase);

		// 徘徊へ戻す(要求は一度だけ出す)
		void Finish(SwarmBossStateContext& a_context);

	private:
		//------------------------------------------------------------------------------------------
		// 調整値
		//------------------------------------------------------------------------------------------
		// 潜る
		float m_burrowDepth         = 30.0f;	// 地表からこの深さまで潜る(体が地表から出ないように)
		float m_depthTolerance      = 5.0f;		// 狙いの深さからこの範囲に入ったら地中移動へ
		float m_burrowForward       = 20.0f;	// 潜りながらプレイヤーの方へ進む量(水平。0で真下へ潜る)
		float m_burrowThrottle      = 1.0f;		// 潜るときの移動入力の強さ(0〜1)
		float m_burrowMaxTime       = 5.0f;		// 潜る最長時間(秒。地面に入れなければ徘徊へ戻る)

		// 地中移動
		float m_approachSpeedScale  = 1.8f;		// 地中移動の速さ(リーダーの移動速度に対する倍率)
		float m_underDistance       = 5.0f;		// プレイヤーとの水平距離がこれ以下で真下に来たとみなす
		float m_approachMaxTime     = 6.0f;		// 地中移動の最長時間(秒。間に合わなければその場で突き上げる)

		// 突き上げ
		float m_uperSpeedScale      = 2.0f;		// 突き上げの速さ(リーダーの移動速度に対する倍率)
		float m_overshootHeight     = 60.0f;	// プレイヤーの高さをこれだけ越えたら突き上げをやめる
		float m_uperMaxTime         = 3.0f;		// 突き上げの最長時間(秒)

		// 余韻
		float m_recoverTime         = 1.5f;		// 余韻の長さ(秒)
		float m_recoverThrottle     = 0.6f;		// 余韻中の移動入力の強さ(0〜1)

		//------------------------------------------------------------------------------------------
		// 実行中の状態(保存しない)
		//------------------------------------------------------------------------------------------
		EPhase m_phase = EPhase::Burrow;
		float m_phaseTime = 0.0f;				// 今のフェーズに入ってからの経過時間(秒)
		Math::Vector3 m_playerPos = {};			// 最後に見たプレイヤーの位置
		float m_groundHeight = 0.0f;			// 最後に見た地表の高さ(表示用)
		float m_depth = 0.0f;					// 地表からの深さ(表示用。地上なら負)
		bool m_isUnderGround = false;			// 地中に居るか(表示用)
	};
}
