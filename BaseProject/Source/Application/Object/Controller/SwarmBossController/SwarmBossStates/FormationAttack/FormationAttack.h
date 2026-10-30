#pragma once

#include "../IState.h"

namespace App::Object
{
	/// <summary>
	/// 円陣ミサイル(第2フェーズ) : リーダーを中心に体を渦巻きの円陣に並べ、全機をプレイヤーへ向けてから、
	/// 外周(尾)から内側(頭)へ順々に、ボイドを1体ずつ切り離して自爆ミサイルとして撃つ
	/// </summary>
	/// <remarks>
	/// 流れ : 中心へ向かう(Approach) → 並ぶ(Form) → 向く(Aim) → 構える(Windup) → 撃つ(Fire) → 戻る(Recover) → 徘徊へ
	///
	/// ・円陣の中心は、入った瞬間にプレイヤーから見てリーダーの居る側へ m_distance 離れた、
	///   プレイヤーの足元から m_height 上の点。リーダーはそこへ向かい、着いたら留まる。
	/// ・円陣は面の上の渦巻き(アルキメデスの螺旋)。頭の小隊長が内側、尾が外側で、m_turns 周ぶん巻く。
	///   面の表はプレイヤーの方で、プレイヤーが動くと m_faceTurnSpeedDeg で向き直る。
	///   面の中では m_rollSpeedDeg(撃つ間は m_fireRollSpeedDeg)でゆっくり回る。
	///   小隊長ごとの置き場所を決めて書くのはコントローラー(このステートは依頼 formation を出すだけ)。
	/// ・並ぶときは頭から尾へ m_joinTime かけて順に加わる(体が中心から巻き取られていくように見える)。
	///   まだ加わっていない小隊長は今までどおり前の相手を追う。
	/// ・向く(Aim)からは、小隊長をプレイヤーへ向ける。ボイドは小隊長の向きへ寄せるので全機が向く。
	/// ・構えるときに攻撃の鳴き声を頼み、発光のウェーブを速める。
	/// ・撃つときは尾の小隊長から頭へ順番に、m_fireInterval 秒ごとに「ボイドを1体切り離せ」と
	///   SwarmMissileResource へ積む。頭まで行ったら1巡で、m_fireRounds 巡したら戻る。
	///   ミサイルを作る・飛ばす・自爆させるのは SwarmMissileSystem(巻き付きと同じ)。
	///   打ち上げの向きはプレイヤーの方(面の表)を混ぜるので、円陣から前へ撃ち出すように見える。
	/// ・プレイヤーが居なければ何もせず徘徊へ戻る。
	/// ・後から足したステートなので、調整値はすべて ArchiveAppended へ書く。
	/// </remarks>
	class SwarmBossFormationAttackState : public IState
	{
	public:
		void Enter(SwarmBossStateContext& a_context) override;
		void Update(SwarmBossStateContext& a_context) override;
		void Exit(SwarmBossStateContext& a_context) override;

		void ArchiveAppended(Engine::Persistence::Archive& a_ar) override;
		void DrawInspector() override;

	private:
		enum class EPhase
		{
			Approach,	// 中心へ向かう : リーダーが円陣の中心へ
			Form,		// 並ぶ : 頭から順に渦巻きの置き場所へ
			Aim,		// 向く : 全機がプレイヤーへ向き直る
			Windup,		// 構える : 鳴いてウェーブを速める
			Fire,		// 撃つ : 外周から順にボイドを切り離す
			Recover,	// 戻る : 撃ち終えてから少し留まる
			End,		// 切り替え要求済み(次のフレームで抜ける)
		};

		// 次のフェーズへ(経過時間は0から数え直す)
		void ChangePhase(EPhase a_phase);

		// リーダーを円陣の中心へ寄せる入力を書く(遠ければ m_approachSpeedScale で向かい、近ければ留まる)
		void UpdateLeaderIntent(SwarmBossStateContext& a_context);

		// 円陣の面の表をプレイヤーの方へ向き直らせる
		void UpdateNormal(float a_dt);

		// 円陣の依頼を組む(並べる割合・向けるかはフェーズで決まる)
		void WriteFormationRequest(SwarmBossStateContext& a_context) const;

		// 尾から頭へ向かって、次に居る小隊長から1体切り離す要求を積む。1巡し終えていれば false
		bool ReserveLaunch(SwarmBossStateContext& a_context);

		// ミサイルの調整値と円陣の中心を SwarmMissileResource へ書き写す
		void WriteMissileResource(SwarmBossStateContext& a_context) const;

		// 円陣の外周と面の向きのデバッグ表示
		void DrawFormation(SwarmBossStateContext& a_context) const;

		// 徘徊へ戻す(要求は一度だけ出す)
		void Finish(SwarmBossStateContext& a_context);

	private:
		//------------------------------------------------------------------------------------------
		// 調整値
		//------------------------------------------------------------------------------------------
		// 円陣の置き場所
		float m_distance           = 120.0f;	// プレイヤーから円陣の中心までの水平距離(m)
		float m_height             = 60.0f;		// 円陣の中心の、プレイヤーの足元からの高さ(m)

		// 渦巻き
		float m_innerRadius        = 12.0f;		// 頭の小隊長を置く半径(m)
		float m_ringSpacing        = 12.0f;		// 1周ごとに外へ広がる幅(m)
		float m_turns              = 3.0f;		// 巻く周回数
		float m_rollSpeedDeg       = 8.0f;		// 面の中で回る速さ(度/秒)
		float m_fireRollSpeedDeg   = 25.0f;		// 撃っている間に面の中で回る速さ(度/秒)
		float m_faceTurnSpeedDeg   = 30.0f;		// 面の表をプレイヤーへ向け直す速さ(度/秒)
		float m_aimHeight          = 1.0f;		// 向ける先の、プレイヤーの足元からの高さ(m)

		// 中心へ向かう
		float m_approachSpeedScale = 1.5f;		// 中心へ向かう速さ(リーダーの移動速度に対する倍率)
		float m_arriveDistance     = 15.0f;		// 中心にこの距離まで近づいたら並び始める
		float m_approachMaxTime    = 8.0f;		// 中心へ向かう最長時間(秒。着かなくてもその場から並ぶ)
		float m_centerGain         = 2.0f;		// 中心からのずれを詰める強さ(1/秒)

		// 各段の長さ
		float m_joinTime           = 3.0f;		// 頭から尾まで並び終えるまでの時間(秒)
		float m_aimTime            = 1.5f;		// プレイヤーへ向き直る時間(秒)
		float m_windupTime         = 1.2f;		// 構える時間(秒)
		float m_windupWaveScale    = 3.0f;		// 構えてから撃ち終えるまでのウェーブの倍率
		float m_recoverTime        = 1.5f;		// 撃ち終えてから徘徊へ戻るまでの時間(秒)

		// 撃つ
		float m_fireInterval       = 0.05f;		// 次の小隊長が切り離すまでの間隔(秒。外周から内側へ順番)
		uint32_t m_fireRounds      = 2;			// 外周から内側まで何巡撃つか

		// ミサイル(SwarmMissileResource へ書き写す。意味はあちらを参照)
		float m_missileLaunchSpeed   = 40.0f;
		float m_missileLaunchTime    = 0.35f;
		float m_missileLaunchUp      = 0.2f;
		float m_missileLaunchOut     = 0.3f;
		float m_missileLaunchForward = 1.0f;	// プレイヤーの方(面の表)の重み
		float m_missileLaunchSpread  = 0.25f;
		float m_missileSpeed         = 95.0f;
		float m_missileAcceleration  = 80.0f;
		float m_missileTurnSpeedDeg  = 70.0f;
		float m_missileLoseAngleDeg  = 90.0f;	// 見失う角度
		float m_missileLifeTime      = 6.0f;
		float m_missileExplodeRadius = 3.0f;
		float m_missileDamage        = 10.0f;
		Math::Vector3 m_missileColor = { 1.0f, 0.15f, 0.05f };
		float m_missileIntensity     = 10.0f;

		//------------------------------------------------------------------------------------------
		// 実行中の状態(保存しない)
		//------------------------------------------------------------------------------------------
		EPhase m_phase = EPhase::Approach;
		float m_phaseTime = 0.0f;				// 今のフェーズに入ってからの経過時間(秒)
		Math::Vector3 m_playerPos = {};			// 最後に見たプレイヤーの位置(足元)

		Math::Vector3 m_center = {};			// 円陣の中心(ワールド)
		Math::Vector3 m_normal = { 0.0f, 0.0f, 1.0f };	// 円陣の面の表の向き(単位ベクトル)
		float m_roll = 0.0f;					// 面の中での回転(ラジアン)

		float m_fireTimer = 0.0f;				// 次に切り離すまでの残り時間(秒)
		size_t m_fireCursor = 0;				// 今の巡で尾から数えていくつ進んだか
		uint32_t m_fireRound = 0;				// 撃ち終えた巡の数
		uint32_t m_launchCount = 0;				// この攻撃で切り離しを要求した数(表示用)
	};
}
