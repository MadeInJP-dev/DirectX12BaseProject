#pragma once

#include "../IState.h"

namespace App::Object
{
	/// <summary>
	/// 巻き付き : プレイヤーを中心に体を円状に巻き、地面と上を行き来しながら、
	/// 各小隊長がボイドを1体ずつ切り離して自爆ミサイルとしてプレイヤーへ飛ばす
	/// </summary>
	/// <remarks>
	/// 流れ : 輪に入る(Approach) → 巻く(Coil) → 徘徊へ
	///
	/// ・輪の半径は体の長さ ÷ 2π(頭と尾がちょうど繋がる大きさ)に m_radiusScale を掛けたもの。
	/// ・巻く間は、輪の上を進む目標点をリーダーが追う(目標点の進む速度 + ずれを詰める分)。
	///   目標点の高さは地表を基準に m_lowHeight 〜 m_highHeight を正弦波で行き来するので、
	///   体(小隊長・ボイド)はリーダーの軌跡をなぞって、地面から出たり潜ったりする輪になる。
	/// ・輪の中心はプレイヤーへゆっくり寄せる(m_centerFollowGain。0 なら巻き始めの位置のまま)。
	/// ・巻き始めて m_launchStartDelay 秒たったら、頭の小隊長から尾へ順番に
	///   m_launchInterval 秒ごとに「ボイドを1体切り離せ」と SwarmMissileResource へ積む。
	///   尾まで行ったら頭へ戻る。ボイドを探してミサイルにする・飛ばす・自爆させるのは
	///   SwarmMissileSystem で、ミサイルの調整値もここから毎フレーム書き写す。
	/// ・切り離したボイドは SwarmBossBoidTag を持ったままなので、自爆しても撃ち落とされても
	///   ボスの体力が1減る(体を削って撃つ攻撃)。
	/// ・速さは移動入力の長さで上げる(SwarmLeaderMoveSystem は 入力 × moveSpeed)。
	///   小隊長はリーダーの platoonSpeedScale 倍なので、それを超えると列が千切れる。
	/// ・巻き始めた瞬間に攻撃の鳴き声を頼む(画面は震わせない)。
	/// ・プレイヤーが居なければ何もせず徘徊へ戻る。
	/// </remarks>
	class SwarmBossCoilAttackState : public IState
	{
	public:
		void Enter(SwarmBossStateContext& a_context) override;
		void Update(SwarmBossStateContext& a_context) override;
		void Exit(SwarmBossStateContext& a_context) override;

		void Archive(Engine::Persistence::Archive& a_ar) override;
		// 後から足した調整値(見失う角度)。バイナリの読み順を崩さないよう Archive とは分ける
		void ArchiveAppended(Engine::Persistence::Archive& a_ar) override;
		void DrawInspector() override;

	private:
		enum class EPhase
		{
			Approach,	// 輪に入る : 輪の上の一番近い点へ向かう
			Coil,		// 巻く : 輪の上を回りながら上下し、ボイドを切り離す
			End,		// 切り替え要求済み(次のフレームで抜ける)
		};

		// 次のフェーズへ(経過時間は0から数え直す)
		void ChangePhase(EPhase a_phase);

		// 輪の半径(体の長さから)
		float CalcRadius(float a_wormLength) const;

		// 巻き始めからの経過時間 a_time での、地表からの高さとその変化の速さ
		float CalcHeight(float a_time) const;
		float CalcHeightSpeed(float a_time) const;

		// 次の小隊長から1体切り離す要求を積む(頭から尾へ。居なくなった小隊長は飛ばす)
		void ReserveLaunch(SwarmBossStateContext& a_context);

		// ミサイルの調整値と輪の中心を SwarmMissileResource へ書き写す
		void WriteMissileResource(SwarmBossStateContext& a_context);

		// 輪のデバッグ表示
		void DrawRing(SwarmBossStateContext& a_context) const;

		// 徘徊へ戻す(要求は一度だけ出す)
		void Finish(SwarmBossStateContext& a_context);

	private:
		//------------------------------------------------------------------------------------------
		// 調整値
		//------------------------------------------------------------------------------------------
		// 輪
		float m_radiusScale        = 1.0f;		// 輪の半径 = 体の長さ ÷ 2π × これ(1で頭と尾がちょうど繋がる)
		float m_minRadius          = 30.0f;		// 輪の半径の下限(m。体が短くてもプレイヤーに重ならないように)
		float m_centerFollowGain   = 0.3f;		// 輪の中心をプレイヤーへ寄せる強さ(1/秒。0で巻き始めの位置のまま)

		// 輪に入る
		float m_approachSpeedScale = 1.5f;		// 輪に入るまでの速さ(リーダーの移動速度に対する倍率)
		float m_arriveDistance     = 20.0f;		// 輪の上の点にこの距離まで近づいたら巻き始める
		float m_approachMaxTime    = 8.0f;		// 輪に入る最長時間(秒。着かなくてもその場から巻く)

		// 巻く
		float m_coilSpeedScale     = 1.5f;		// 巻く速さ(リーダーの移動速度に対する倍率)
		float m_followGain         = 3.0f;		// 輪の上の目標点からのずれを詰める強さ(1/秒)
		float m_coilTime           = 15.0f;		// 巻く長さ(秒)。過ぎたら徘徊へ戻る
		float m_lowHeight          = -20.0f;	// 一番低いところの地表からの高さ(m。負で地中)
		float m_highHeight         = 30.0f;		// 一番高いところの地表からの高さ(m)
		float m_undulationPeriod   = 5.0f;		// 地面と上を1往復する周期(秒)

		// 切り離し
		float m_launchStartDelay   = 1.0f;		// 巻き始めてから切り離しを始めるまでの時間(秒)
		float m_launchInterval     = 0.1f;		// 次の小隊長が切り離すまでの間隔(秒。頭から尾へ順番)

		// ミサイル(SwarmMissileResource へ書き写す。意味はあちらを参照)
		float m_missileLaunchSpeed   = 40.0f;
		float m_missileLaunchTime    = 0.4f;
		float m_missileLaunchUp      = 1.0f;
		float m_missileLaunchOut     = 0.5f;
		float m_missileLaunchSpread  = 0.3f;
		float m_missileSpeed         = 95.0f;
		float m_missileAcceleration  = 80.0f;
		float m_missileTurnSpeedDeg  = 90.0f;
		float m_missileLoseAngleDeg  = 90.0f;	// 見失う角度(後から足した値なので ArchiveAppended で保存)
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
		Math::Vector3 m_playerPos = {};			// 最後に見たプレイヤーの位置

		Math::Vector3 m_center = {};			// 輪の中心(ワールド。高さは使わない)
		float m_radius = 0.0f;					// 輪の半径(m)
		float m_angle = 0.0f;					// 輪の上の目標点の角度(ラジアン。+X から +Z へ)
		float m_turnSign = 1.0f;				// 回る向き(+1 / -1)
		float m_groundHeight = 0.0f;			// 最後に見た地表の高さ(リーダーの真下)
		bool m_isGroundKnown = false;			// 地表の高さを1度でも見たか

		float m_launchTimer = 0.0f;				// 次に切り離すまでの残り時間(秒)
		size_t m_launchCursor = 0;				// 次に切り離す小隊長の番号(頭から)
		uint32_t m_launchCount = 0;				// この攻撃で切り離しを要求した数(表示用)
	};
}
