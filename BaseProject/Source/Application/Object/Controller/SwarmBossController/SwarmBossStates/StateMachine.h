#pragma once

#include "IState.h"

namespace App::Object
{
	// ワームの行動、切り替え時は各ステートからチェンジ要求がきたら次のフレームで切り替える
	enum class ESwarmBossState
	{
		Idle,				// 地面から半身を出してる状態でプレイヤーを見つめつつ立っている状態。チンアナゴ的な
		RandomWalk,			// 徘徊行動 : 攻撃と攻撃の合間に移動。抽選が行われる ,地面の中と空中で分かれる
		UperAttack,			// 地面に潜ってプレイヤーの真下へ回り込み、真上へ突き上げる
		DiveAttack,			// プレイヤーから離れて高く上がり、放物線(二次関数)を描いてプレイヤーへ急降下する
		Charge,				// プレイヤーへ向かって一直線に突進する
		CoilAttack,			// プレイヤーを中心に体を円状に巻き、地面と上を行き来しながら、各小隊長がボイドを切り離して自爆ミサイルにする
		Reorganize,			// 体力が一定量減るたびに、球体状にぐるぐるまとまって小隊長を整理する(その間は無敵)。抽選には入らない
		Death,				// 体力が一定値を切ったら、地上の高いところで球体状にまとまり、ウェーブを速めていって最後に爆散する。抜けない
		Emerge,				// 出現 : 地面の下から一直線に上空へ飛び出し、とぐろを巻きながら鳴く。出した直後だけ。抽選には入らない
		FormationAttack,	// 円陣ミサイル(第2フェーズ) : リーダーを中心に体を渦巻きの円陣に並べてプレイヤーへ向け、外周から順にボイドをミサイルにして撃つ
	};

	class SwarmBossStateMachine
	{
	public:

		// ステートの登録。調整値を読み込みで受けるので、Archive より前(生成時)に呼ぶ
		void Init();

		// チェンジ要求があればここで切り替える(要求を出したフレームの次)
		void PreUpdate(SwarmBossStateContext& a_context);

		void Update(SwarmBossStateContext& a_context);

		void PostUpdate(SwarmBossStateContext& a_context);

		// 切り替え要求。実際に切り替わるのは次の PreUpdate
		void ReserveChangeState(ESwarmBossState a_state);

		ESwarmBossState GetCurrentState() const { return m_currentState; }

		// 切り替え要求が出ていて、まだ切り替わっていないか
		bool IsChangeRequested() const { return m_isChangeRequested; }

		// デバッグ : 次の攻撃が固定されていれば a_out に入れて true(徘徊が攻撃へ移るときに見る)
		bool GetDebugNextAttack(ESwarmBossState& a_out) const
		{
			if (!m_isDebugNextAttack) return false;
			a_out = m_debugState;
			return true;
		}

		//------------------------------------------------------------------------------------------
		// シリアライズ / エディター : 登録済みの全ステートへ流す
		//------------------------------------------------------------------------------------------
		void Archive(Engine::Persistence::Archive& a_ar);
		void DrawInspector();

		// 後から足した調整値(IState::ArchiveAppended)。バイナリは書いた順に読むので、Archive の並びへ混ぜず、
		// 持ち主の保存値の末尾(区切りの中)でこちらを呼ぶ。並びは列挙の順で、ステートごとに区切る
		void ArchiveAppended(Engine::Persistence::Archive& a_ar);

	private:

		// 要求されていたステートへ切り替える(Exit → Enter)
		void ChangeState(SwarmBossStateContext& a_context);

	private:

		// ステート
		std::unordered_map<ESwarmBossState, std::unique_ptr<IState>> m_upStates = {};

		IState* m_pCurrentState = nullptr;							// 現在のステートの実体(未開始なら null)
		ESwarmBossState m_currentState = ESwarmBossState::Idle;		// 現在のステート
		ESwarmBossState m_changeState  = ESwarmBossState::Idle;		// チェンジ要求
		bool m_isChangeRequested = false;							// 要求が出ているか

		//------------------------------------------------------------------------------------------
		// デバッグ用(インスペクターから触る。保存しない)
		//   ・Change Now        … 選んだステートへ今すぐ切り替える
		//   ・Fix Next Attack   … 徘徊からの攻撃の抽選を、選んだステートに固定する
		//------------------------------------------------------------------------------------------
		ESwarmBossState m_debugState = ESwarmBossState::Charge;	// 指定するステート
		bool m_isDebugNextAttack = false;							// 次の攻撃を固定するか
	};
}
