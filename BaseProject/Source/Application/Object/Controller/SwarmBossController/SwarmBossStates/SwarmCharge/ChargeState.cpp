#include "ChargeState.h"

// エンジン
#include "Engine/GameObject/BaseObject/BaseObject.h"
#include "Engine/Persistence/Archive/Archive.h"
#include "Engine/ECS/System/SystemContext.h"
#include "Engine/EditorField/EditorField.h"	// コンポーネントの Traits が使うので先に置く

// App
#include "../../../../../ECS/World/APPWorld.h"
#include "../StateMachine.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Movement/MoveIntentComponent.h"
#include "Application/Components/Input/PlayerControllTag.h"

namespace App::Object
{
	namespace
	{
		//----------------------------------------------------------------------
		// 操作しているプレイヤーの位置(ワールド)。見つからなければ false
		//
		// プレイヤーは親を持つとは限らないので、行列から位置を取る
		//----------------------------------------------------------------------
		bool FindPlayerPos(Engine::GameObject::ObjectContext& a_context, Math::Vector3& a_outPos)
		{
			Engine::ECS::Entity _player = Engine::ECS::Limits::INVALID_ENTITY;

			a_context.pWorld->ForEach<const Component::ActiveTag, const Component::PlayerControllTag>(
				[&](
					Engine::ECS::Chunk* a_pChunk,
					uint32_t a_count,
					const Component::ActiveTag* /*a_activeTagArray*/,
					const Component::PlayerControllTag* /*a_playerTagArray*/
				)
				{
					if (_player != Engine::ECS::Limits::INVALID_ENTITY || a_count == 0) return;
					_player = a_pChunk->entityData[0];
				}
			);

			if (_player == Engine::ECS::Limits::INVALID_ENTITY) return false;

			auto& _world = *a_context.pWorld;
			if (_world.HasComponent<Component::WorldMatrixComponent>(_player))
			{
				a_outPos = _world.RefData<Component::WorldMatrixComponent>(_player)->worldMat.Translation();
				return true;
			}
			if (_world.HasComponent<Component::LocalTransformComponent>(_player))
			{
				a_outPos = _world.RefData<Component::LocalTransformComponent>(_player)->pos;
				return true;
			}
			return false;
		}
	}

	void SwarmBossChargeState::Enter(SwarmBossStateContext& /*a_context*/)
	{
		m_phase     = EPhase::Windup;
		m_phaseTime = 0.0f;
		m_chargeDir = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossChargeState::Update(SwarmBossStateContext& a_context)
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

		// プレイヤーが居ない(倒された / まだ湧いていない)なら突っ込む先が無い
		const bool _isFoundPlayer = FindPlayerPos(*a_context.pObject, m_playerPos);
		if (!_isFoundPlayer && m_phase != EPhase::Recover)
		{
			Finish(a_context);
			return;
		}

		// プレイヤーへの向き(重なっているときは今の突進の向きを使う)
		Math::Vector3 _toPlayer = m_playerPos - _pos;
		const float _distToPlayer = _toPlayer.Length();
		Math::Vector3 _toPlayerDir = (_distToPlayer > 1e-3f) ? _toPlayer / _distToPlayer : m_chargeDir;

		m_phaseTime += _dt;

		Math::Vector3 _intent = Math::Vector3(0.0f, 0.0f, 0.0f);

		switch (m_phase)
		{
		//--------------------------------------------------------------
		// 溜め : 減速しながらプレイヤーへ向く。終わった瞬間の向きで固定する
		//--------------------------------------------------------------
		case EPhase::Windup:
		{
			_intent = _toPlayerDir * std::clamp(m_windupThrottle, 0.0f, 1.0f);

			// 溜めの間に回り始める
			a_context.bodySpinRate = m_spinRate;

			if (m_phaseTime >= m_windupTime)
			{
				m_chargeDir = _toPlayerDir;
				m_phase     = EPhase::Charge;
				m_phaseTime = 0.0f;
			}
			break;
		}

		//--------------------------------------------------------------
		// 突進 : 固定した向きへ進む。プレイヤーが前に居る間だけ少し曲げる
		//--------------------------------------------------------------
		case EPhase::Charge:
		{
			const bool _isPlayerAhead = _toPlayerDir.Dot(m_chargeDir) > 0.0f;

			if (_isPlayerAhead && m_homingTurnSpeed > 0.0f)
			{
				m_chargeDir = RotateTowards(m_chargeDir, _toPlayerDir, m_homingTurnSpeed * _dt);
			}

			_intent = m_chargeDir * std::max(m_chargeSpeedScale, 0.0f);

			// 突っ込んでいる間も回し続ける(コントローラーが最高速まで上げていく)
			a_context.bodySpinRate = m_spinRate;

			// 通り過ぎて十分離れたか、時間切れで終わり
			const bool _isPassed = !_isPlayerAhead && _distToPlayer >= m_overshootDistance;
			if (_isPassed || m_phaseTime >= m_maxChargeTime)
			{
				m_phase     = EPhase::Recover;
				m_phaseTime = 0.0f;
			}
			break;
		}

		//--------------------------------------------------------------
		// 余韻 : 同じ向きへ惰性で進んでから徘徊へ戻る
		//--------------------------------------------------------------
		case EPhase::Recover:
		{
			_intent = m_chargeDir * std::clamp(m_recoverThrottle, 0.0f, 1.0f);

			if (m_phaseTime >= m_recoverTime)
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

	void SwarmBossChargeState::Exit(SwarmBossStateContext& a_context)
	{
		// 突進の速さの入力を次のステートへ持ち越さない
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossChargeState::Finish(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		m_phase = EPhase::End;

		if (a_context.pMachine)
		{
			a_context.pMachine->ReserveChangeState(ESwarmBossState::RandomWalk);
		}
	}

	Math::Vector3 SwarmBossChargeState::RotateTowards(const Math::Vector3& a_from, const Math::Vector3& a_to, float a_maxAngle)
	{
		const float _cos   = std::clamp(a_from.Dot(a_to), -1.0f, 1.0f);
		const float _angle = std::acos(_cos);
		if (_angle <= a_maxAngle) return a_to;

		// 回転軸 = 今の向き × 目標の向き。真後ろ(軸が作れない)は曲げない
		Math::Vector3 _axis = a_from.Cross(a_to);
		const float _axisLenSq = _axis.LengthSquared();
		if (_axisLenSq <= 1e-8f) return a_from;
		_axis /= std::sqrt(_axisLenSq);

		const Math::Quaternion _rot = Math::Quaternion::CreateFromAxisAngle(_axis, a_maxAngle);
		Math::Vector3 _dir = Math::Vector3::Transform(a_from, _rot);
		_dir.Normalize();
		return _dir;
	}

	void SwarmBossChargeState::Archive(Engine::Persistence::Archive& a_ar)
	{
		// ステートの調整値は同じ階層に並ぶので、名前の頭に Charge を付けて区別する
		a_ar.Field("ChargeWindupTime", m_windupTime);
		a_ar.Field("ChargeWindupThrottle", m_windupThrottle);
		a_ar.Field("ChargeSpeedScale", m_chargeSpeedScale);
		a_ar.Field("ChargeHomingTurnSpeed", m_homingTurnSpeed);
		a_ar.Field("ChargeMaxTime", m_maxChargeTime);
		a_ar.Field("ChargeOvershootDistance", m_overshootDistance);
		a_ar.Field("ChargeRecoverTime", m_recoverTime);
		a_ar.Field("ChargeRecoverThrottle", m_recoverThrottle);
	}

	void SwarmBossChargeState::ArchiveAppended(Engine::Persistence::Archive& a_ar)
	{
		a_ar.Field("ChargeSpinRate", m_spinRate);
	}

	void SwarmBossChargeState::DrawInspector()
	{
		Engine::EditorField::Field("溜めの長さ", m_windupTime, 0.05f, 0.0f);
		Engine::EditorField::Field("溜め中の移動入力の強さ", m_windupThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("突進の速さの倍率", m_chargeSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Field("突進中に曲がれる速さ(ラジアン/秒)", m_homingTurnSpeed, 0.01f, 0.0f);
		Engine::EditorField::Field("突進の最長時間", m_maxChargeTime, 0.1f, 0.0f);
		Engine::EditorField::Field("通り過ぎてから止まる距離", m_overshootDistance, 0.5f, 0.0f);
		Engine::EditorField::Field("余韻の長さ", m_recoverTime, 0.05f, 0.0f);
		Engine::EditorField::Field("余韻中の移動入力の強さ", m_recoverThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("小隊長の倍率を超える速さにすると列が千切れる");
		Engine::EditorField::Field("胴回りの回転の強さ", m_spinRate, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("溜めと突進の間、ボイドを小隊長の胴回りで回す(最高速に対する割合。0 で回さない)");

		// 実行中の状態は表示のみ
		Engine::EditorField::Value("フェーズ", "%s (%.1f 秒)", std::string(magic_enum::enum_name(m_phase)).c_str(), m_phaseTime);
		Engine::EditorField::Value("プレイヤーの位置", "%.1f, %.1f, %.1f", m_playerPos.x, m_playerPos.y, m_playerPos.z);
	}
}
