#include "StateMachine.h"

#include "SwarmRandomWalk/RandomWalkState.h"
#include "SwarmCharge/ChargeState.h"
#include "UperAttack/UperAttack.h"
#include "DiveAttack/DiveAttack.h"
#include "CoilAttack/CoilAttack.h"
#include "Reorganize/Reorganize.h"
#include "Death/Death.h"
#include "Emerge/Emerge.h"

namespace App::Object
{
	namespace
	{
		//----------------------------------------------------------------------
		// インスペクターに出すステートの名前(日本語 + 列挙子の名前)。
		// デバッグの選択欄は列挙子の名前で並ぶので、対応が分かるように併記する
		//----------------------------------------------------------------------
		std::string GetStateDisplayName(ESwarmBossState a_state)
		{
			const char* _name = "";
			switch (a_state)
			{
			case ESwarmBossState::Idle:       _name = "待機";                 break;
			case ESwarmBossState::RandomWalk: _name = "徘徊";                 break;
			case ESwarmBossState::UperAttack: _name = "アッパー";             break;
			case ESwarmBossState::DiveAttack: _name = "ダイブ";               break;
			case ESwarmBossState::Charge:     _name = "突進";                 break;
			case ESwarmBossState::CoilAttack: _name = "巻き付き";             break;
			case ESwarmBossState::Reorganize: _name = "小隊長の整理";         break;
			case ESwarmBossState::Death:      _name = "死亡";                 break;
			case ESwarmBossState::Emerge:     _name = "出現";                 break;
			default:                          break;
			}
			return std::string(_name) + " (" + std::string(magic_enum::enum_name(a_state)) + ")";
		}
	}

	void SwarmBossStateMachine::Init()
	{
		m_upStates.clear();
		m_pCurrentState = nullptr;

		// 作ったものから足していく
		m_upStates.emplace(ESwarmBossState::RandomWalk, std::make_unique<SwarmBossRandomWalkState>());
		m_upStates.emplace(ESwarmBossState::Charge, std::make_unique<SwarmBossChargeState>());
		m_upStates.emplace(ESwarmBossState::UperAttack, std::make_unique<SwarmBossUperAttackState>());
		m_upStates.emplace(ESwarmBossState::DiveAttack, std::make_unique<SwarmBossDiveAttackState>());
		m_upStates.emplace(ESwarmBossState::CoilAttack, std::make_unique<SwarmBossCoilAttackState>());
		m_upStates.emplace(ESwarmBossState::Reorganize, std::make_unique<SwarmBossReorganizeState>());
		m_upStates.emplace(ESwarmBossState::Death, std::make_unique<SwarmBossDeathState>());
		m_upStates.emplace(ESwarmBossState::Emerge, std::make_unique<SwarmBossEmergeState>());

		// 最初の行動。1フレーム目の PreUpdate で入る
		// (地面から出す設定なら、コントローラーが生成のときに出現へ切り替え直す)
		ReserveChangeState(ESwarmBossState::RandomWalk);
	}

	void SwarmBossStateMachine::PreUpdate(SwarmBossStateContext& a_context)
	{
		if (m_isChangeRequested)
		{
			ChangeState(a_context);
		}
	}

	void SwarmBossStateMachine::Update(SwarmBossStateContext& a_context)
	{
		if (m_pCurrentState)
		{
			m_pCurrentState->Update(a_context);
		}
	}

	void SwarmBossStateMachine::PostUpdate(SwarmBossStateContext& /*a_context*/)
	{}

	void SwarmBossStateMachine::ReserveChangeState(ESwarmBossState a_state)
	{
		m_changeState = a_state;
		m_isChangeRequested = true;
	}

	void SwarmBossStateMachine::ChangeState(SwarmBossStateContext& a_context)
	{
		m_isChangeRequested = false;

		auto _it = m_upStates.find(m_changeState);
		if (_it == m_upStates.end() || !_it->second)
		{
			// まだ作っていないステート。今のステートを続ける
			ENGINE_WARNING("SwarmBossStateMachine : 未登録のステートです : %s",
				std::string(magic_enum::enum_name(m_changeState)).c_str());
			return;
		}

		if (m_pCurrentState)
		{
			m_pCurrentState->Exit(a_context);
		}

		m_currentState  = m_changeState;
		m_pCurrentState = _it->second.get();
		m_pCurrentState->Enter(a_context);
	}

	void SwarmBossStateMachine::Archive(Engine::Persistence::Archive& a_ar)
	{
		// ※ 並びは unordered_map の順。ここへ足すと既存のバイナリの読み順が崩れるので、
		//    後から足す調整値は ArchiveAppended へ書く
		for (auto& [_state, _upState] : m_upStates)
		{
			if (_upState) _upState->Archive(a_ar);
		}
	}

	void SwarmBossStateMachine::ArchiveAppended(Engine::Persistence::Archive& a_ar)
	{
		// 列挙の順で回し、ステートごとに区切る(どれかのステートに足しても、ほかの読み出しがずれない)
		for (const ESwarmBossState _state : magic_enum::enum_values<ESwarmBossState>())
		{
			auto _it = m_upStates.find(_state);
			if (_it == m_upStates.end() || !_it->second) continue;

			Engine::Persistence::ArchiveSection _section(a_ar, magic_enum::enum_name(_state));
			_it->second->ArchiveAppended(a_ar);
		}
	}

	void SwarmBossStateMachine::DrawInspector()
	{
		Engine::EditorField::Value("現在のステート", "%s", m_pCurrentState
			? GetStateDisplayName(m_currentState).c_str()
			: "(未開始)");

		//------------------------------------------------------------------
		// デバッグ : 次のステートを指定する
		//------------------------------------------------------------------
		Engine::EditorField::Header("デバッグ");
		Engine::EditorField::Field("指定するステート", m_debugState);
		if (Engine::EditorField::Button("今すぐ切り替える"))
		{
			// 切り替わるのは次のフレーム(普段の切り替えと同じ)
			ReserveChangeState(m_debugState);
		}
		Engine::EditorField::Tooltip("選んだステートへ次のフレームで切り替える");
		Engine::EditorField::Field("次の攻撃を固定", m_isDebugNextAttack);
		Engine::EditorField::Tooltip("徘徊から攻撃へ移るとき、抽選せずに選んだステートへ入る");

		for (auto& [_state, _upState] : m_upStates)
		{
			if (!_upState) continue;

			Engine::EditorField::Header(GetStateDisplayName(_state).c_str());
			Engine::EditorField::IDScope _id(static_cast<int>(_state));
			_upState->DrawInspector();
		}
	}
}
