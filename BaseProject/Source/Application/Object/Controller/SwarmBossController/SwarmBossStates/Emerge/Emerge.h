#pragma once

#include "../IState.h"

namespace App::Object
{
	/// <summary>
	/// 出現 : 地面の下から一直線に勢いよく上空へ飛び出し、とぐろを巻きながら鳴く
	/// </summary>
	/// <remarks>
	/// 流れ : 飛び出す(Rise) → とぐろを巻く(Coil。途中で鳴く) → 徘徊へ
	///
	/// ・出した直後に入る(コントローラーが生成のときに切り替える)。体は生成の時点で
	///   地中に縦一列で並んでいるので、頭が真上へ進むと体がその軌跡をなぞって一本の柱になる。
	///   デバッグで途中から入れたときは、その場から真上へ飛び出す。
	/// ・飛び出すのは地表から m_riseHeight の高さまで(地表はリーダーの SerchGroundComponent)。
	///   地面が見つからなければ生成位置の高さを地表とみなす。
	/// ・とぐろは、飛び出した柱を輪の縁に置いた水平の輪(半径 m_coilRadius)の上を回りながら、
	///   m_coilDescendSpeed で少しずつ下がっていく螺旋。体はリーダーの軌跡をなぞるので、
	///   柱の上に輪が積み重なる形になる。目標点の追い方は巻き付き(CoilAttack)と同じ。
	/// ・巻き始めて m_roarDelay 秒たったら鳴く。鳴き始めに鳴き声を頼み(isRequestRoar)、
	///   m_roarTime 秒の間は画面を震わせる強さ(roarWeight)を頼み続ける。
	///   立ち上がり m_roarFadeIn 秒・終わり m_roarFadeOut 秒でなだらかにする。
	///   鳴いている間は発光のウェーブも m_roarWaveSpeedScale 倍に速める。
	/// ・出現の間は体(ボイド)の防御比率を m_defenseRatio(既定 0 = 無敵)にするよう頼む。
	/// ・速さは移動入力の長さで上げる(SwarmLeaderMoveSystem は 入力 × moveSpeed)。
	///   小隊長はリーダーの platoonSpeedScale 倍なので、それを超えると列が千切れる。
	/// </remarks>
	class SwarmBossEmergeState : public IState
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
			Rise,		// 飛び出す : 真上へ一直線
			Coil,		// とぐろを巻く : 螺旋を描いて回りながら鳴く
			End,		// 切り替え要求済み(次のフレームで抜ける)
		};

		// 次のフェーズへ(経過時間は0から数え直す)
		void ChangePhase(EPhase a_phase);

		// とぐろの輪を決める(今のリーダーの位置を輪の縁に置く)
		void BeginCoil(SwarmBossStateContext& a_context, const Math::Vector3& a_leaderPos);

		// 鳴き始めてからの経過 a_time での、画面を震わせる強さ(0〜1)
		float CalcRoarWeight(float a_time) const;

		// 徘徊へ戻す(要求は一度だけ出す)
		void Finish(SwarmBossStateContext& a_context);

	private:
		//------------------------------------------------------------------------------------------
		// 調整値
		//------------------------------------------------------------------------------------------
		// 飛び出す
		float m_riseSpeedScale      = 2.0f;		// 飛び出す速さ(リーダーの移動速度に対する倍率)
		float m_riseHeight          = 150.0f;	// 地表からこの高さまで飛び出したらとぐろを巻く(m)
		float m_riseMaxTime         = 10.0f;	// 飛び出す最長時間(秒。届かなくてもその場から巻く)

		// とぐろを巻く
		float m_coilRadius          = 45.0f;	// 輪の半径(m)
		float m_coilSpeedScale      = 1.2f;		// 回る速さ(リーダーの移動速度に対する倍率)
		float m_coilDescendSpeed    = 4.0f;		// 回りながら下がる速さ(m/秒。輪が積み重なる間隔になる)
		float m_followGain          = 3.0f;		// 輪の上の目標点からのずれを詰める強さ(1/秒)
		float m_coilTime            = 8.0f;		// 巻く長さ(秒)。過ぎたら徘徊へ戻る

		// 鳴く
		float m_roarDelay           = 1.0f;		// 巻き始めてから鳴くまでの時間(秒)
		float m_roarTime            = 5.0f;		// 鳴いている長さ(秒。鳴き声の長さに合わせる)
		float m_roarFadeIn          = 0.2f;		// 震えが立ち上がるまでの時間(秒)
		float m_roarFadeOut         = 1.2f;		// 震えが収まるまでの時間(秒。鳴いている長さの内側)
		float m_roarWaveSpeedScale  = 4.0f;		// 鳴いている間の発光のウェーブの速さの倍率

		float m_defenseRatio        = 0.0f;		// 出現の間の体の防御比率(0 : 無敵)

		//------------------------------------------------------------------------------------------
		// 実行中の状態(保存しない)
		//------------------------------------------------------------------------------------------
		EPhase m_phase = EPhase::Rise;
		float m_phaseTime = 0.0f;				// 今のフェーズに入ってからの経過時間(秒)
		float m_groundHeight = 0.0f;			// 最後に見た地表の高さ
		bool m_isGroundKnown = false;			// 地表の高さを1度でも見たか

		Math::Vector3 m_center = {};			// 輪の中心(ワールド。高さは使わない)
		float m_angle = 0.0f;					// 輪の上の目標点の角度(ラジアン。+X から +Z へ)
		float m_turnSign = 1.0f;				// 回る向き(+1 / -1)
		float m_coilTopY = 0.0f;				// 巻き始めの高さ(ここから下がっていく)

		bool m_isRoared = false;				// 鳴き声を頼んだか(一度だけ)
		float m_roarWeight = 0.0f;				// 今頼んでいる震えの強さ(表示用)
	};
}
