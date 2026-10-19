#pragma once

#include "../SwarmBossStates/StateMachine.h"

namespace App::Object
{
	/// <summary>
	/// ボスのフェーズ : 徘徊から出す攻撃の組み合わせと、次のフェーズへ移る区切り
	/// </summary>
	/// <remarks>
	/// ・フェーズが持つのは「徘徊(RandomWalk)の抽選に入る攻撃と重み」。
	///   小隊長の整理・死亡・出現はどのフェーズでもコントローラーが割り込ませるので、ここには入らない。
	/// ・次のフェーズへ移るのは、小隊長の整理が m_endReorganizeCount 回目の整理を頼んだとき
	///   (体を組み直した瞬間)。0 なら最後のフェーズで、以降は移らない。
	/// ・移った後、整理が終わって徘徊へ戻ったら、抽選を待たずに m_entryState へ入る
	///   (RandomWalk のままなら抽選を待つ)。
	/// ・調整値の保存名は使う側が頭に付ける名前 + 項目名(例 : "Phase1" + "AttackWeightCharge")。
	/// </remarks>
	class SwarmBossPhase
	{
	public:
		// 抽選に入りうる攻撃。重みはこの並びで持つ(足すときは末尾へ)
		static constexpr std::array<ESwarmBossState, 5> ATTACK_STATES =
		{
			ESwarmBossState::Charge,
			ESwarmBossState::UperAttack,
			ESwarmBossState::DiveAttack,
			ESwarmBossState::CoilAttack,
			ESwarmBossState::FormationAttack,
		};

		SwarmBossPhase() = default;
		SwarmBossPhase(
			const std::array<float, ATTACK_STATES.size()>& a_attackWeights,
			uint32_t a_endReorganizeCount,
			ESwarmBossState a_entryState)
			: m_attackWeights(a_attackWeights)
			, m_endReorganizeCount(a_endReorganizeCount)
			, m_entryState(a_entryState)
		{}

		// 次に出す攻撃を重みで抽選する(全部0なら突進)
		ESwarmBossState PickAttack() const;

		uint32_t GetEndReorganizeCount() const { return m_endReorganizeCount; }
		ESwarmBossState GetEntryState() const { return m_entryState; }

		//------------------------------------------------------------------------------------------
		// シリアライズ / エディター
		//------------------------------------------------------------------------------------------
		void Archive(Engine::Persistence::Archive& a_ar, const std::string& a_prefix);
		void DrawInspector();

	private:
		//------------------------------------------------------------------------------------------
		// 調整値
		//------------------------------------------------------------------------------------------
		std::array<float, ATTACK_STATES.size()> m_attackWeights = {};	// 抽選の重み(ATTACK_STATES の並び。0で出さない)
		uint32_t m_endReorganizeCount = 0;								// この回数目の整理で次のフェーズへ(0 : 最後のフェーズ)
		ESwarmBossState m_entryState = ESwarmBossState::RandomWalk;		// フェーズに入って最初に出す行動
	};
}
