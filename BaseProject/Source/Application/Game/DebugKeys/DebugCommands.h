#pragma once

namespace App::Game
{
	//==========================================================================================
	// デバッグキー(DebugKeys)からゲームへ渡す命令と状態
	//
	// 実体は DebugKeys が1つだけ持ち、ワールドへは DebugCommandResource で入口を置く。
	// 起動設定でデバッグキーを切っていると DebugKeys を作らないので入口も置かれない。受け取る側は
	// 引けなければ何もしないこと(デバッグの有無で処理を分けずに済む)。
	//
	//   状態 : 押すたびに切り替わり、シーンをまたいで残る
	//   予約 : 押したフレームだけ立つ。受け取る側(SceneSequence / SwarmBossController)が
	//          そのフレームの更新で拾い、DebugKeys がシーンの更新を終えたところで下ろす
	//==========================================================================================
	struct DebugCommands
	{
		// ---- 状態 ----
		bool isPlayerInvincible = false;		// プレイヤーがダメージを受けない(HealthSystem が見る)

		// ---- 予約(1フレームだけ) ----
		bool isSkipWaveReserved       = false;	// 出ている敵を倒して次のウェーブを出す(SceneSequence)
		bool isSkipToLastWaveReserved = false;	// 最後のウェーブ(ボス)まで飛ばす(SceneSequence)
		bool isBossLowHealthReserved  = false;	// ボスの体力を死亡の手前まで減らす(SwarmBossController)

		// 予約を下ろす。状態はそのまま
		void ClearReserved()
		{
			isSkipWaveReserved       = false;
			isSkipToLastWaveReserved = false;
			isBossLowHealthReserved  = false;
		}
	};
}
