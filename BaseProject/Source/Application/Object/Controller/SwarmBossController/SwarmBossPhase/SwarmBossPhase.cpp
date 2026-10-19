#include "SwarmBossPhase.h"

// エンジン
#include "Engine/Persistence/Archive/Archive.h"
#include "Engine/EditorField/EditorField.h"

namespace App::Object
{
	namespace
	{
		// インスペクターに出す攻撃の名前(ATTACK_STATES の並び)
		constexpr std::array<const char*, SwarmBossPhase::ATTACK_STATES.size()> ATTACK_DISPLAY_NAMES =
		{
			"抽選の重み : 突進",
			"抽選の重み : アッパー",
			"抽選の重み : ダイブ",
			"抽選の重み : 巻き付き",
			"抽選の重み : 円陣ミサイル",
		};
	}

	ESwarmBossState SwarmBossPhase::PickAttack() const
	{
		float _total = 0.0f;
		for (const float _weight : m_attackWeights) _total += std::max(_weight, 0.0f);
		if (_total <= 0.0f) return ESwarmBossState::Charge;

		// 0〜合計の中で引いた値が、どの重みの区間に入ったか
		float _pick = Math::Random::Float(0.0f, _total);
		for (size_t _i = 0; _i < ATTACK_STATES.size(); ++_i)
		{
			const float _weight = std::max(m_attackWeights[_i], 0.0f);
			if (_pick < _weight) return ATTACK_STATES[_i];
			_pick -= _weight;
		}

		// 浮動小数の誤差で最後を越えたときは、重みを持つ最後のもの
		for (size_t _i = ATTACK_STATES.size(); _i > 0; --_i)
		{
			if (m_attackWeights[_i - 1] > 0.0f) return ATTACK_STATES[_i - 1];
		}
		return ESwarmBossState::Charge;
	}

	void SwarmBossPhase::Archive(Engine::Persistence::Archive& a_ar, const std::string& a_prefix)
	{
		for (size_t _i = 0; _i < ATTACK_STATES.size(); ++_i)
		{
			a_ar.Field(a_prefix + "AttackWeight" + std::string(magic_enum::enum_name(ATTACK_STATES[_i])), m_attackWeights[_i]);
		}
		a_ar.Field(a_prefix + "EndReorganizeCount", m_endReorganizeCount);
		a_ar.Field(a_prefix + "EntryState", m_entryState);
	}

	void SwarmBossPhase::DrawInspector()
	{
		for (size_t _i = 0; _i < ATTACK_STATES.size(); ++_i)
		{
			Engine::EditorField::Field(ATTACK_DISPLAY_NAMES[_i], m_attackWeights[_i], 0.05f, 0.0f);
		}

		Engine::EditorField::Field("次のフェーズへ移る整理の回数", m_endReorganizeCount);
		Engine::EditorField::Tooltip("この回数目の小隊長の整理で次のフェーズへ移る(0 で最後のフェーズ)");

		Engine::EditorField::Field("フェーズに入って最初の行動", m_entryState);
		Engine::EditorField::Tooltip("整理が終わって徘徊へ戻ったとき、抽選を待たずにこの行動へ入る(RandomWalk なら抽選を待つ)");
	}
}
