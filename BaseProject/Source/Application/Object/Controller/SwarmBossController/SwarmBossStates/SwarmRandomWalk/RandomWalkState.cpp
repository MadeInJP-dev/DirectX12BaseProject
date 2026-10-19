#include "RandomWalkState.h"

// エンジン
#include "Engine/GameObject/BaseObject/BaseObject.h"
#include "Engine/Persistence/Archive/Archive.h"
#include "Engine/ECS/System/SystemContext.h"
#include "Engine/EditorField/EditorField.h"	// コンポーネントの Traits が使うので先に置く

// App
#include "../../../../../ECS/World/APPWorld.h"
#include "../StateMachine.h"
#include "../../SwarmBossPhase/SwarmBossPhase.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Movement/MoveIntentComponent.h"

namespace App::Object
{
	void SwarmBossRandomWalkState::Enter(SwarmBossStateContext& a_context)
	{
		// 入った瞬間に行き先を決める(前のステートの目標を引きずらない)
		PickWanderTarget(a_context.spawnPos);

		// 計測開始。攻撃へ移る時間はここで決める
		m_time = 0.0f;
		m_attackTime = Math::Random::Float(
			std::min(m_minDurationTime, m_maxDurationTime),
			std::max(m_minDurationTime, m_maxDurationTime));

		// 何を出すかもここで決めておく(抽選の重みは今のフェーズが持つ)
		m_nextAttack = a_context.pPhase ? a_context.pPhase->PickAttack() : ESwarmBossState::Charge;
	}

	void SwarmBossRandomWalkState::Update(SwarmBossStateContext& a_context)
	{
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		m_time += a_context.pObject->dt;

		// 時間が来たら攻撃へ(切り替わるのは次のフレーム。それまでは徘徊を続ける)。
		// デバッグで次の攻撃が固定されていれば、抽選の結果よりそちらを優先する
		if (m_time >= m_attackTime && a_context.pMachine)
		{
			ESwarmBossState _next = m_nextAttack;
			a_context.pMachine->GetDebugNextAttack(_next);
			a_context.pMachine->ReserveChangeState(_next);
		}

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;
		if (!_world.HasComponent<Component::LocalTransformComponent>(_leader)) return;

		const Math::Vector3 _pos = _world.RefData<Component::LocalTransformComponent>(_leader)->pos;

		// 目標地点を選び直すか
		m_wanderTimer -= a_context.pObject->dt;

		const Math::Vector3 _toTarget = m_targetPos - _pos;
		if (m_wanderTimer <= 0.0f || _toTarget.Length() <= m_arriveDistance)
		{
			PickWanderTarget(a_context.spawnPos);
		}

		// 目標地点へ向かう入力。長さがスロットルになる(向きは世界空間)
		Math::Vector3 _dir = m_targetPos - _pos;
		if (_dir.LengthSquared() > 1e-6f)
		{
			_dir.Normalize();
			_dir *= std::clamp(m_throttle, 0.0f, 1.0f);
		}
		else
		{
			_dir = Math::Vector3(0.0f, 0.0f, 0.0f);
		}

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = _dir;
	}

	void SwarmBossRandomWalkState::Exit(SwarmBossStateContext& a_context)
	{
		// 入力を残したまま抜けると、次のステートが書かない限り走り続けるので止めておく
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossRandomWalkState::PickWanderTarget(const Math::Vector3& a_center)
	{
		// 水平は円の中から、高さは振れ幅の中から選ぶ。
		// 円内の一様分布にするため半径は平方根を取る(そのまま掛けると中心に寄る)
		const float _angle  = Math::Random::Float(0.0f, DirectX::XM_2PI);
		const float _radius = m_wanderRadius * std::sqrt(Math::Random::Float(0.0f, 1.0f));

		m_targetPos = a_center + Math::Vector3(
			std::cos(_angle) * _radius,
			Math::Random::Float(-m_wanderHeight, m_wanderHeight),
			std::sin(_angle) * _radius);

		m_wanderTimer = m_wanderInterval;
	}

	void SwarmBossRandomWalkState::Archive(Engine::Persistence::Archive& a_ar)
	{
		// コントローラーに直に持っていた頃と同じ名前(既存シーンをそのまま読める)
		// 目標地点は走り出してから抽選するので保存しない
		a_ar.Field("WanderRadius", m_wanderRadius);
		a_ar.Field("WanderHeight", m_wanderHeight);
		a_ar.Field("WanderInterval", m_wanderInterval);
		a_ar.Field("ArriveDistance", m_arriveDistance);
		a_ar.Field("Throttle", m_throttle);
		a_ar.Field("AttackIntervalMin", m_minDurationTime);
		a_ar.Field("AttackIntervalMax", m_maxDurationTime);
		// 攻撃の抽選の重みはフェーズ(SwarmBossPhase)へ移した
	}

	void SwarmBossRandomWalkState::DrawInspector()
	{
		Engine::EditorField::Field("徘徊の半径", m_wanderRadius, 0.5f, 0.0f);
		Engine::EditorField::Field("徘徊の高さの振れ幅", m_wanderHeight, 0.5f, 0.0f);
		Engine::EditorField::Field("目標地点を選び直す間隔", m_wanderInterval, 0.1f, 0.0f);
		Engine::EditorField::Field("到着とみなす距離", m_arriveDistance, 0.1f, 0.0f);
		Engine::EditorField::Field("移動入力の強さ", m_throttle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("攻撃までの時間(最短)", m_minDurationTime, 0.1f, 0.0f);
		Engine::EditorField::Field("攻撃までの時間(最長)", m_maxDurationTime, 0.1f, 0.0f);

		// 目標地点は毎フレーム上書きされるので表示のみ
		Engine::EditorField::Value("目標地点", "%.1f, %.1f, %.1f (選び直しまで %.1f 秒)", m_targetPos.x, m_targetPos.y, m_targetPos.z, m_wanderTimer);
		Engine::EditorField::Value("次の攻撃", "%.1f / %.1f 秒 -> %s", m_time, m_attackTime, std::string(magic_enum::enum_name(m_nextAttack)).c_str());
	}
}
