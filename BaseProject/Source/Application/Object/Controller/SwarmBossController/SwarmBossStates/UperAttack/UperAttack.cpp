#include "UperAttack.h"

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
#include "Application/Components/Boid/SerchGroundComponent.h"

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

		// 正規化して長さを掛ける。長さ0なら0を返す
		Math::Vector3 ScaledDir(Math::Vector3 a_dir, float a_scale)
		{
			if (a_dir.LengthSquared() <= 1e-6f) return Math::Vector3(0.0f, 0.0f, 0.0f);
			a_dir.Normalize();
			return a_dir * a_scale;
		}
	}

	void SwarmBossUperAttackState::Enter(SwarmBossStateContext& /*a_context*/)
	{
		ChangePhase(EPhase::Burrow);
	}

	void SwarmBossUperAttackState::Update(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;
		const float _dt = a_context.pObject->dt;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;
		if (!_world.HasComponent<Component::LocalTransformComponent>(_leader)) return;

		// 地面が分からないと潜れない
		if (!_world.HasComponent<Component::SerchGroundComponent>(_leader))
		{
			Finish(a_context);
			return;
		}

		const Math::Vector3 _pos = _world.RefData<Component::LocalTransformComponent>(_leader)->pos;

		// 地面との関係(SerchGroundSystem が書いた結果)
		const Component::SerchGroundComponent _ground = *_world.RefData<Component::SerchGroundComponent>(_leader);
		m_isUnderGround = _ground.isUnderGround != 0;
		if (_ground.isFoundGround)
		{
			m_groundHeight = _ground.groundHeight;
			m_depth        = m_groundHeight - _pos.y;
		}

		// プレイヤーが居ない(倒された / まだ湧いていない)なら狙う先が無い。
		// 飛び出した後は最後に見た位置のまま続ける
		const bool _isFoundPlayer = FindPlayerPos(*a_context.pObject, m_playerPos);
		if (!_isFoundPlayer && (m_phase == EPhase::Burrow || m_phase == EPhase::Approach))
		{
			Finish(a_context);
			return;
		}

		// プレイヤーへの水平の向きと距離
		Math::Vector3 _toPlayerXZ = m_playerPos - _pos;
		_toPlayerXZ.y = 0.0f;
		const float _distXZ = _toPlayerXZ.Length();
		const Math::Vector3 _toPlayerDirXZ = (_distXZ > 1e-3f) ? _toPlayerXZ / _distXZ : Math::Vector3(0.0f, 0.0f, 0.0f);

		// 狙う高さ : 地表から決まった深さだけ下(地面が見つからなければ今の高さ)
		const float _targetY = _ground.isFoundGround ? (_ground.groundHeight - m_burrowDepth) : _pos.y;

		m_phaseTime += _dt;

		Math::Vector3 _intent = Math::Vector3(0.0f, 0.0f, 0.0f);

		switch (m_phase)
		{
		//--------------------------------------------------------------
		// 潜る : プレイヤーの方へ少し進みながら、地表から決まった深さまで潜る
		//--------------------------------------------------------------
		case EPhase::Burrow:
		{
			if (_ground.isFoundGround)
			{
				const Math::Vector3 _target =
					_pos + _toPlayerDirXZ * std::max(m_burrowForward, 0.0f) + Math::Vector3(0.0f, _targetY - _pos.y, 0.0f);
				_intent = ScaledDir(_target - _pos, std::clamp(m_burrowThrottle, 0.0f, 1.0f));
			}
			else
			{
				// レイの届く範囲に地面が無い(高く飛びすぎている)。見つかるまで真下へ
				_intent = Math::Vector3::Down() * std::clamp(m_burrowThrottle, 0.0f, 1.0f);
			}

			// 地中で狙いの深さに入ったら地中移動へ
			const bool _isReachedDepth = m_isUnderGround && m_depth >= m_burrowDepth - m_depthTolerance;
			if (_isReachedDepth)
			{
				ChangePhase(EPhase::Approach);
			}
			else if (m_phaseTime >= m_burrowMaxTime)
			{
				// 地中に入れていれば浅くても続ける。入れなければ諦めて徘徊へ
				if (!m_isUnderGround)
				{
					Finish(a_context);
					return;
				}
				ChangePhase(EPhase::Approach);
			}
			break;
		}

		//--------------------------------------------------------------
		// 地中移動 : 深さを保ったまま、プレイヤーの真下へ高速で向かう
		//--------------------------------------------------------------
		case EPhase::Approach:
		{
			const Math::Vector3 _target = Math::Vector3(m_playerPos.x, _targetY, m_playerPos.z);
			_intent = ScaledDir(_target - _pos, std::max(m_approachSpeedScale, 0.0f));

			// 真下に来たか、時間切れならその場で突き上げる。突き上げの瞬間に鳴く
			if (_distXZ <= m_underDistance || m_phaseTime >= m_approachMaxTime)
			{
				ChangePhase(EPhase::Uper);
				a_context.cry = ESwarmBossCry::Attack;
			}
			break;
		}

		//--------------------------------------------------------------
		// 突き上げ : 真上へ飛び出す。プレイヤーの高さを越えたら終わり
		//--------------------------------------------------------------
		case EPhase::Uper:
		{
			_intent = Math::Vector3::Up() * std::max(m_uperSpeedScale, 0.0f);

			const bool _isPassed = _pos.y >= m_playerPos.y + m_overshootHeight;
			if (_isPassed || m_phaseTime >= m_uperMaxTime)
			{
				ChangePhase(EPhase::Recover);
			}
			break;
		}

		//--------------------------------------------------------------
		// 余韻 : 同じ向き(真上)へ惰性で進んでから徘徊へ戻る
		//--------------------------------------------------------------
		case EPhase::Recover:
		{
			_intent = Math::Vector3::Up() * std::clamp(m_recoverThrottle, 0.0f, 1.0f);

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

	void SwarmBossUperAttackState::Exit(SwarmBossStateContext& a_context)
	{
		// 突き上げの速さの入力を次のステートへ持ち越さない
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossUperAttackState::ChangePhase(EPhase a_phase)
	{
		m_phase     = a_phase;
		m_phaseTime = 0.0f;
	}

	void SwarmBossUperAttackState::Finish(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		m_phase = EPhase::End;

		if (a_context.pMachine)
		{
			a_context.pMachine->ReserveChangeState(ESwarmBossState::RandomWalk);
		}
	}

	void SwarmBossUperAttackState::Archive(Engine::Persistence::Archive& a_ar)
	{
		// ステートの調整値は同じ階層に並ぶので、名前の頭に UperAttack を付けて区別する
		a_ar.Field("UperAttackBurrowDepth", m_burrowDepth);
		a_ar.Field("UperAttackDepthTolerance", m_depthTolerance);
		a_ar.Field("UperAttackBurrowForward", m_burrowForward);
		a_ar.Field("UperAttackBurrowThrottle", m_burrowThrottle);
		a_ar.Field("UperAttackBurrowMaxTime", m_burrowMaxTime);
		a_ar.Field("UperAttackApproachSpeedScale", m_approachSpeedScale);
		a_ar.Field("UperAttackUnderDistance", m_underDistance);
		a_ar.Field("UperAttackApproachMaxTime", m_approachMaxTime);
		a_ar.Field("UperAttackSpeedScale", m_uperSpeedScale);
		a_ar.Field("UperAttackOvershootHeight", m_overshootHeight);
		a_ar.Field("UperAttackMaxTime", m_uperMaxTime);
		a_ar.Field("UperAttackRecoverTime", m_recoverTime);
		a_ar.Field("UperAttackRecoverThrottle", m_recoverThrottle);
	}

	void SwarmBossUperAttackState::DrawInspector()
	{
		Engine::EditorField::Field("潜る深さ", m_burrowDepth, 0.5f, 0.0f);
		Engine::EditorField::Field("深さの許容範囲", m_depthTolerance, 0.1f, 0.0f);
		Engine::EditorField::Field("潜りながら進む量", m_burrowForward, 0.5f, 0.0f);
		Engine::EditorField::Field("潜るときの移動入力の強さ", m_burrowThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("潜る最長時間", m_burrowMaxTime, 0.1f, 0.0f);
		Engine::EditorField::Field("地中移動の速さの倍率", m_approachSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Field("真下とみなす距離", m_underDistance, 0.1f, 0.0f);
		Engine::EditorField::Field("地中移動の最長時間", m_approachMaxTime, 0.1f, 0.0f);
		Engine::EditorField::Field("突き上げの速さの倍率", m_uperSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Field("突き上げをやめる高さ", m_overshootHeight, 0.5f, 0.0f);
		Engine::EditorField::Field("突き上げの最長時間", m_uperMaxTime, 0.1f, 0.0f);
		Engine::EditorField::Field("余韻の長さ", m_recoverTime, 0.05f, 0.0f);
		Engine::EditorField::Field("余韻中の移動入力の強さ", m_recoverThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("小隊長の倍率を超える速さにすると列が千切れる");

		// 実行中の状態は表示のみ
		Engine::EditorField::Value("フェーズ", "%s (%.1f 秒)", std::string(magic_enum::enum_name(m_phase)).c_str(), m_phaseTime);
		Engine::EditorField::Value("プレイヤーの位置", "%.1f, %.1f, %.1f", m_playerPos.x, m_playerPos.y, m_playerPos.z);
		Engine::EditorField::Value("地表の高さ", "%.1f (深さ %.1f, %s)", m_groundHeight, m_depth, m_isUnderGround ? "地中" : "地上");
	}
}
