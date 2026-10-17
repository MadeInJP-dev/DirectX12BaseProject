#include "Emerge.h"

// エンジン
#include "Engine/GameObject/BaseObject/BaseObject.h"
#include "Engine/Persistence/Archive/Archive.h"
#include "Engine/ECS/System/SystemContext.h"
#include "Engine/EditorField/EditorField.h"	// コンポーネントの Traits が使うので先に置く

// App
#include "../../../../../ECS/World/APPWorld.h"
#include "../StateMachine.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Movement/MoveIntentComponent.h"
#include "Application/Components/Movement/MovementParamsComponent.h"
#include "Application/Components/Boid/SerchGroundComponent.h"

namespace App::Object
{
	namespace
	{
		// とぐろの目標点を地表からこれより下げない(m)。下がり続けて地面へ潜らないように
		constexpr float COIL_MIN_HEIGHT = 10.0f;
	}

	void SwarmBossEmergeState::Enter(SwarmBossStateContext& /*a_context*/)
	{
		ChangePhase(EPhase::Rise);

		m_isGroundKnown = false;
		m_angle         = 0.0f;
		m_turnSign      = 1.0f;
		m_isRoared      = false;
		m_roarWeight    = 0.0f;
	}

	void SwarmBossEmergeState::Update(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;
		const float _dt = a_context.pObject->dt;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;
		if (!_world.HasComponent<Component::LocalTransformComponent>(_leader)) return;

		const Math::Vector3 _pos = _world.RefData<Component::LocalTransformComponent>(_leader)->pos;

		// 入力 1 で出る速さ。輪の上の目標点をリーダーの速さで進めるのに使う
		const float _moveSpeed = _world.HasComponent<Component::MovementParamsComponent>(_leader)
			? _world.RefData<Component::MovementParamsComponent>(_leader)->moveSpeed
			: 0.0f;

		// 地表の高さ(SerchGroundSystem が書いた結果)。
		// レイが届かない高さに居るフレームは最後に見た値のまま。一度も見ていなければ生成位置の高さ
		if (_world.HasComponent<Component::SerchGroundComponent>(_leader))
		{
			const Component::SerchGroundComponent& _ground = *_world.RefData<Component::SerchGroundComponent>(_leader);
			if (_ground.isFoundGround)
			{
				m_groundHeight  = _ground.groundHeight;
				m_isGroundKnown = true;
			}
		}
		if (!m_isGroundKnown) m_groundHeight = a_context.spawnPos.y;

		m_phaseTime += _dt;

		// 出現の間は体を守る
		a_context.bodyDefenseRatio = m_defenseRatio;

		Math::Vector3 _intent = Math::Vector3(0.0f, 0.0f, 0.0f);

		switch (m_phase)
		{
		//--------------------------------------------------------------
		// 飛び出す : 真上へ一直線。決めた高さを越えたら巻き始める
		//--------------------------------------------------------------
		case EPhase::Rise:
		{
			_intent = Math::Vector3::Up() * std::max(m_riseSpeedScale, 0.0f);

			// 地表が分からない間は生成位置の高さで見る(生成は地中なので、そこで止まることはない)
			const bool _isReached = _pos.y >= m_groundHeight + m_riseHeight;
			if (_isReached || m_phaseTime >= m_riseMaxTime)
			{
				BeginCoil(a_context, _pos);
				ChangePhase(EPhase::Coil);
			}
			break;
		}

		//--------------------------------------------------------------
		// とぐろを巻く : 下がっていく輪の上を進む目標点を追う(巻き付きと同じ追い方)
		//
		//   入力 = 目標点の速度 / moveSpeed  +  目標点とのずれ × 詰める強さ / moveSpeed
		//--------------------------------------------------------------
		case EPhase::Coil:
		{
			const float _radius = std::max(m_coilRadius, 1.0f);
			const float _speed  = _moveSpeed * std::max(m_coilSpeedScale, 0.0f);

			m_angle += m_turnSign * (_speed / _radius) * _dt;
			m_angle  = std::remainder(m_angle, DirectX::XM_2PI);	// 長く回っても精度が落ちないように畳む

			const float _cos = std::cos(m_angle);
			const float _sin = std::sin(m_angle);

			// 高さは巻き始めから一定の速さで下げる。地表すれすれまで来たらそこで止める
			const float _minY = m_groundHeight + COIL_MIN_HEIGHT;
			float _targetY  = m_coilTopY - std::max(m_coilDescendSpeed, 0.0f) * m_phaseTime;
			float _descendY = -std::max(m_coilDescendSpeed, 0.0f);
			if (_targetY <= _minY)
			{
				_targetY  = _minY;
				_descendY = 0.0f;
			}

			Math::Vector3 _target = m_center + Math::Vector3(_cos, 0.0f, _sin) * _radius;
			_target.y = _targetY;

			const Math::Vector3 _targetVel =
				Math::Vector3(-_sin, 0.0f, _cos) * (m_turnSign * _speed) +
				Math::Vector3(0.0f, _descendY, 0.0f);

			if (_moveSpeed > 0.0f)
			{
				_intent = _targetVel / _moveSpeed + (_target - _pos) * (m_followGain / _moveSpeed);
			}

			//----------------------------------------------------------
			// 鳴く : 鳴き始めに鳴き声を頼み、鳴いている間は画面の震えを頼み続ける
			//----------------------------------------------------------
			const float _roarTime = m_phaseTime - std::max(m_roarDelay, 0.0f);
			m_roarWeight = CalcRoarWeight(_roarTime);

			if (_roarTime >= 0.0f && !m_isRoared)
			{
				m_isRoared = true;
				a_context.cry = ESwarmBossCry::Roar;
			}
			if (_roarTime >= 0.0f && _roarTime < m_roarTime)
			{
				a_context.waveSpeedScale = std::max(m_roarWaveSpeedScale, 1.0f);
			}
			a_context.roarWeight = m_roarWeight;

			// 鳴き終わるまでは抜けない(巻く長さが短くても、鳴き声の途中で切らない)
			const float _endTime = std::max(m_coilTime, std::max(m_roarDelay, 0.0f) + std::max(m_roarTime, 0.0f));
			if (m_phaseTime >= _endTime)
			{
				Finish(a_context);
				return;
			}
			break;
		}

		default:
			break;
		}

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = _intent;
	}

	void SwarmBossEmergeState::Exit(SwarmBossStateContext& a_context)
	{
		m_roarWeight = 0.0f;

		// 飛び出す / 巻く速さの入力を次のステートへ持ち越さない
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossEmergeState::ChangePhase(EPhase a_phase)
	{
		m_phase     = a_phase;
		m_phaseTime = 0.0f;
	}

	void SwarmBossEmergeState::BeginCoil(SwarmBossStateContext& /*a_context*/, const Math::Vector3& a_leaderPos)
	{
		// 今のリーダーの位置が輪の縁に来るよう、中心を水平に半径ぶんずらして置く。
		// ずらす向きは毎回変える(いつも同じ側へ巻くと単調になる)
		const float _radius = std::max(m_coilRadius, 1.0f);
		const float _dirAngle = Math::Random::Float(0.0f, DirectX::XM_2PI);
		const Math::Vector3 _toCenter(std::cos(_dirAngle), 0.0f, std::sin(_dirAngle));

		m_center   = a_leaderPos + _toCenter * _radius;
		m_angle    = std::atan2(-_toCenter.z, -_toCenter.x);	// 中心から見たリーダーの角度
		m_turnSign = (Math::Random::Float(0.0f, 1.0f) < 0.5f) ? 1.0f : -1.0f;
		m_coilTopY = a_leaderPos.y;
	}

	float SwarmBossEmergeState::CalcRoarWeight(float a_time) const
	{
		if (a_time < 0.0f || a_time >= m_roarTime) return 0.0f;

		// 立ち上がりと終わりをなだらかに(どちらも長さ0なら即座に切り替わる)
		const float _in  = (m_roarFadeIn > 0.0f) ? std::clamp(a_time / m_roarFadeIn, 0.0f, 1.0f) : 1.0f;
		const float _out = (m_roarFadeOut > 0.0f) ? std::clamp((m_roarTime - a_time) / m_roarFadeOut, 0.0f, 1.0f) : 1.0f;
		return std::min(_in, _out);
	}

	void SwarmBossEmergeState::Finish(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		m_phase = EPhase::End;
		m_roarWeight = 0.0f;

		if (a_context.pMachine)
		{
			a_context.pMachine->ReserveChangeState(ESwarmBossState::RandomWalk);
		}
	}

	void SwarmBossEmergeState::ArchiveAppended(Engine::Persistence::Archive& a_ar)
	{
		// ステートの調整値は同じ階層に並ぶので、名前の頭に Emerge を付けて区別する
		a_ar.Field("EmergeRiseSpeedScale", m_riseSpeedScale);
		a_ar.Field("EmergeRiseHeight", m_riseHeight);
		a_ar.Field("EmergeRiseMaxTime", m_riseMaxTime);
		a_ar.Field("EmergeCoilRadius", m_coilRadius);
		a_ar.Field("EmergeCoilSpeedScale", m_coilSpeedScale);
		a_ar.Field("EmergeCoilDescendSpeed", m_coilDescendSpeed);
		a_ar.Field("EmergeFollowGain", m_followGain);
		a_ar.Field("EmergeCoilTime", m_coilTime);
		a_ar.Field("EmergeRoarDelay", m_roarDelay);
		a_ar.Field("EmergeRoarTime", m_roarTime);
		a_ar.Field("EmergeRoarFadeIn", m_roarFadeIn);
		a_ar.Field("EmergeRoarFadeOut", m_roarFadeOut);
		a_ar.Field("EmergeRoarWaveSpeedScale", m_roarWaveSpeedScale);
		a_ar.Field("EmergeDefenseRatio", m_defenseRatio);
	}

	void SwarmBossEmergeState::DrawInspector()
	{
		Engine::EditorField::Field("飛び出す速さの倍率", m_riseSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("小隊長の倍率を超える速さにすると列が千切れる");
		Engine::EditorField::Field("飛び出す高さ", m_riseHeight, 1.0f, 0.0f);
		Engine::EditorField::Tooltip("地表からこの高さまで真上へ飛び出したら、とぐろを巻く");
		Engine::EditorField::Field("飛び出す最長時間", m_riseMaxTime, 0.1f, 0.0f);

		Engine::EditorField::Field("とぐろの半径", m_coilRadius, 0.5f, 1.0f);
		Engine::EditorField::Field("回る速さの倍率", m_coilSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Field("回りながら下がる速さ", m_coilDescendSpeed, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("m/秒。大きいほど輪の積み重なる間隔が広がる");
		Engine::EditorField::Field("ずれを詰める強さ", m_followGain, 0.05f, 0.0f);
		Engine::EditorField::Field("巻く長さ", m_coilTime, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("鳴き終わるまでは、この長さを過ぎても巻き続ける");

		Engine::EditorField::Field("鳴くまでの時間", m_roarDelay, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("巻き始めてから");
		Engine::EditorField::Field("鳴いている長さ", m_roarTime, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("画面を震わせる長さ。鳴き声の長さに合わせる");
		Engine::EditorField::Field("震えの立ち上がり", m_roarFadeIn, 0.01f, 0.0f);
		Engine::EditorField::Field("震えの収まり", m_roarFadeOut, 0.01f, 0.0f);
		Engine::EditorField::Field("鳴いている間のウェーブの倍率", m_roarWaveSpeedScale, 0.1f, 1.0f);
		Engine::EditorField::Field("出現の間の防御比率", m_defenseRatio, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("0 で無敵");

		// 実行中の状態は表示のみ
		Engine::EditorField::Value("フェーズ", "%s (%.1f 秒)", std::string(magic_enum::enum_name(m_phase)).c_str(), m_phaseTime);
		Engine::EditorField::Value("地表の高さ", "%.1f%s", m_groundHeight, m_isGroundKnown ? "" : " (未確認 : 生成位置)");
		Engine::EditorField::Value("とぐろ", "中心 %.1f, %.1f / 巻き始め %.1f", m_center.x, m_center.z, m_coilTopY);
		Engine::EditorField::Value("震えの強さ", "%.2f", m_roarWeight);
	}
}
