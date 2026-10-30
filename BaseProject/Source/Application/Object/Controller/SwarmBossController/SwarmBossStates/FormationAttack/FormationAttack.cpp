#include "FormationAttack.h"

// エンジン
#include "Engine/GameObject/BaseObject/BaseObject.h"
#include "Engine/Persistence/Archive/Archive.h"
#include "Engine/ECS/System/SystemContext.h"
#include "Engine/EditorField/EditorField.h"	// コンポーネントの Traits が使うので先に置く
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/Common/Color.h"

// App
#include "../../../../../ECS/World/APPWorld.h"
#include "../StateMachine.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Movement/MoveIntentComponent.h"
#include "Application/Components/Movement/MovementParamsComponent.h"
#include "Application/Components/Input/PlayerControllTag.h"
#include "Application/InstanceResource/SwarmMissileResource.h"

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

		//----------------------------------------------------------------------
		// a_cur を a_target へ最大 a_maxAngle(ラジアン)だけ回す。どちらも単位ベクトル
		//----------------------------------------------------------------------
		Math::Vector3 RotateToward(const Math::Vector3& a_cur, const Math::Vector3& a_target, float a_maxAngle)
		{
			const float _cos   = std::clamp(a_cur.Dot(a_target), -1.0f, 1.0f);
			const float _angle = std::acos(_cos);
			if (_angle <= a_maxAngle) return a_target;

			// 回転軸 = 今の向き × 目標の向き。ちょうど真後ろなら上を軸にする
			Math::Vector3 _axis = a_cur.Cross(a_target);
			if (_axis.LengthSquared() <= 1e-8f) _axis = Math::Vector3::Up();
			_axis.Normalize();

			Math::Vector3 _dir = Math::Vector3::Transform(a_cur, Math::Quaternion::CreateFromAxisAngle(_axis, a_maxAngle));
			_dir.Normalize();
			return _dir;
		}

		//----------------------------------------------------------------------
		// 面の表 a_normal に垂直な2本(面の中の軸)。上向きを基準に作るので、
		// 面が真上・真下を向くとき以外は表が回っても軸が急に裏返らない
		//----------------------------------------------------------------------
		void MakePlaneBasis(const Math::Vector3& a_normal, Math::Vector3& a_outU, Math::Vector3& a_outV)
		{
			const Math::Vector3 _ref = (std::fabs(a_normal.y) < 0.999f) ? Math::Vector3::Up() : Math::Vector3::Right();
			a_outU = _ref.Cross(a_normal);
			a_outU.Normalize();
			a_outV = a_normal.Cross(a_outU);
		}

		// デバッグ表示で円を何本の線分で描くか
		constexpr int FORMATION_DRAW_SEGMENTS = 48;
	}

	void SwarmBossFormationAttackState::Enter(SwarmBossStateContext& a_context)
	{
		ChangePhase(EPhase::Approach);

		m_roll        = 0.0f;
		m_fireTimer   = 0.0f;
		m_fireCursor  = 0;
		m_fireRound   = 0;
		m_launchCount = 0;

		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		// プレイヤーが居なければ Update で抜ける
		if (!FindPlayerPos(*a_context.pObject, m_playerPos)) return;

		//------------------------------------------------------------------
		// 円陣の中心 : プレイヤーから見てリーダーの居る側(水平)へ m_distance 離れ、足元から m_height 上
		//------------------------------------------------------------------
		Math::Vector3 _side = Math::Vector3(0.0f, 0.0f, 1.0f);
		const auto _leader = a_context.leaderEntity;
		if (_world.IsAliveEntity(_leader) && _world.HasComponent<Component::LocalTransformComponent>(_leader))
		{
			Math::Vector3 _toLeader = _world.RefData<Component::LocalTransformComponent>(_leader)->pos - m_playerPos;
			_toLeader.y = 0.0f;
			if (_toLeader.LengthSquared() > 1e-6f)
			{
				_toLeader.Normalize();
				_side = _toLeader;
			}
		}

		m_center = m_playerPos + _side * std::max(m_distance, 0.0f);
		m_center.y = m_playerPos.y + m_height;

		// 面の表は最初からプレイヤーの方(並び始めで回り込まないように)
		m_normal = (m_playerPos + Math::Vector3(0.0f, m_aimHeight, 0.0f)) - m_center;
		if (m_normal.LengthSquared() <= 1e-6f) m_normal = -_side;
		m_normal.Normalize();
	}

	void SwarmBossFormationAttackState::Update(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		const float _dt = a_context.pObject->dt;

		// プレイヤーが居ない(倒された / まだ湧いていない)なら向ける相手が居ない
		if (!FindPlayerPos(*a_context.pObject, m_playerPos))
		{
			Finish(a_context);
			return;
		}

		m_phaseTime += _dt;

		UpdateLeaderIntent(a_context);
		UpdateNormal(_dt);

		// 面の中で回る(撃つ間は速く)
		const bool _isFiring = (m_phase == EPhase::Windup || m_phase == EPhase::Fire);
		m_roll += DirectX::XMConvertToRadians(_isFiring ? m_fireRollSpeedDeg : m_rollSpeedDeg) * _dt;
		m_roll  = std::remainder(m_roll, DirectX::XM_2PI);	// 長く回っても精度が落ちないように畳む

		switch (m_phase)
		{
		//--------------------------------------------------------------
		// 中心へ向かう : 着いたら(か時間切れで)並び始める
		//--------------------------------------------------------------
		case EPhase::Approach:
		{
			auto& _world = *a_context.pObject->pWorld;
			const auto _leader = a_context.leaderEntity;

			bool _isArrived = (m_phaseTime >= m_approachMaxTime);
			if (_world.IsAliveEntity(_leader) && _world.HasComponent<Component::LocalTransformComponent>(_leader))
			{
				const Math::Vector3 _pos = _world.RefData<Component::LocalTransformComponent>(_leader)->pos;
				if ((m_center - _pos).Length() <= m_arriveDistance) _isArrived = true;
			}

			if (_isArrived) ChangePhase(EPhase::Form);
			break;
		}

		//--------------------------------------------------------------
		// 並ぶ : 頭から順に置き場所へ加わる
		//--------------------------------------------------------------
		case EPhase::Form:
		{
			if (m_phaseTime >= m_joinTime) ChangePhase(EPhase::Aim);
			break;
		}

		//--------------------------------------------------------------
		// 向く : 全機がプレイヤーへ向き直るのを待つ
		//--------------------------------------------------------------
		case EPhase::Aim:
		{
			if (m_phaseTime >= m_aimTime)
			{
				// 構え始めた瞬間に鳴く
				a_context.cry = ESwarmBossCry::Attack;
				ChangePhase(EPhase::Windup);
			}
			break;
		}

		//--------------------------------------------------------------
		// 構える : 時間が来たら外周から撃ち始める
		//--------------------------------------------------------------
		case EPhase::Windup:
		{
			if (m_phaseTime >= m_windupTime)
			{
				m_fireTimer  = 0.0f;
				m_fireCursor = 0;
				m_fireRound  = 0;
				ChangePhase(EPhase::Fire);
			}
			break;
		}

		//--------------------------------------------------------------
		// 撃つ : 尾(外周)の小隊長から頭(内側)へ順番に、1体ずつ切り離す
		//--------------------------------------------------------------
		case EPhase::Fire:
		{
			m_fireTimer -= _dt;
			while (m_fireTimer <= 0.0f && m_phase == EPhase::Fire)
			{
				if (!ReserveLaunch(a_context))
				{
					// 1巡し終えた。決めた巡の数を撃ったら戻る
					++m_fireRound;
					m_fireCursor = 0;
					if (m_fireRound >= std::max(m_fireRounds, 1u))
					{
						ChangePhase(EPhase::Recover);
					}
					break;
				}
				m_fireTimer += std::max(m_fireInterval, 0.01f);	// 0だと止まらないので下限を入れる
			}
			break;
		}

		//--------------------------------------------------------------
		// 戻る : 少し留まってから徘徊へ
		//--------------------------------------------------------------
		case EPhase::Recover:
		{
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

		// 構えてから撃ち終えるまでは、ウェーブを速める
		if (m_phase == EPhase::Windup || m_phase == EPhase::Fire)
		{
			a_context.waveSpeedScale = std::max(m_windupWaveScale, 1e-3f);
		}

		WriteFormationRequest(a_context);
		WriteMissileResource(a_context);
		DrawFormation(a_context);
	}

	void SwarmBossFormationAttackState::Exit(SwarmBossStateContext& a_context)
	{
		// 留まる入力を次のステートへ持ち越さない(円陣の依頼は頼まなくなれば解ける)
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossFormationAttackState::ChangePhase(EPhase a_phase)
	{
		m_phase     = a_phase;
		m_phaseTime = 0.0f;
	}

	void SwarmBossFormationAttackState::UpdateLeaderIntent(SwarmBossStateContext& a_context)
	{
		auto& _world = *a_context.pObject->pWorld;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;
		if (!_world.HasComponent<Component::LocalTransformComponent>(_leader)) return;

		const Math::Vector3 _toCenter = m_center - _world.RefData<Component::LocalTransformComponent>(_leader)->pos;

		// 入力 1 で出る速さ。ずれを詰める速さを入力の長さへ直すのに使う
		const float _moveSpeed = _world.HasComponent<Component::MovementParamsComponent>(_leader)
			? _world.RefData<Component::MovementParamsComponent>(_leader)->moveSpeed
			: 0.0f;

		//--------------------------------------------------------------
		// 遠いうちは m_approachSpeedScale で向かい、近づいたらずれに比例して緩める(行き過ぎない)
		//   入力 = ずれ × 詰める強さ / moveSpeed (長さは m_approachSpeedScale まで)
		//--------------------------------------------------------------
		const float _maxScale = std::max(m_approachSpeedScale, 0.0f);
		float _scale = _maxScale;
		if (_moveSpeed > 0.0f)
		{
			_scale = std::min(_toCenter.Length() * std::max(m_centerGain, 0.0f) / _moveSpeed, _maxScale);
		}

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = ScaledDir(_toCenter, _scale);
	}

	void SwarmBossFormationAttackState::UpdateNormal(float a_dt)
	{
		Math::Vector3 _target = (m_playerPos + Math::Vector3(0.0f, m_aimHeight, 0.0f)) - m_center;
		if (_target.LengthSquared() <= 1e-6f) return;
		_target.Normalize();

		const float _maxTurn = DirectX::XMConvertToRadians(std::max(m_faceTurnSpeedDeg, 0.0f)) * a_dt;
		m_normal = RotateToward(m_normal, _target, _maxTurn);
	}

	void SwarmBossFormationAttackState::WriteFormationRequest(SwarmBossStateContext& a_context) const
	{
		// 中心へ向かう間は並べない(今までどおり列で追う)
		if (m_phase == EPhase::Approach || m_phase == EPhase::End) return;

		SwarmBossFormationRequest& _request = a_context.formation;
		_request.isRequested = true;
		_request.center      = m_center;
		_request.normal      = m_normal;
		_request.roll        = m_roll;
		_request.innerRadius = std::max(m_innerRadius, 0.0f);
		_request.ringSpacing = std::max(m_ringSpacing, 0.0f);
		_request.turns       = std::max(m_turns, 0.0f);

		// 並ぶ間は頭から順に加える。以降は全員
		_request.joinRate = (m_phase == EPhase::Form && m_joinTime > 0.0f)
			? std::clamp(m_phaseTime / m_joinTime, 0.0f, 1.0f)
			: 1.0f;

		// 向く(Aim)からプレイヤーへ向ける
		_request.isLookAtTarget = (m_phase != EPhase::Form);
		_request.lookTarget     = m_playerPos + Math::Vector3(0.0f, m_aimHeight, 0.0f);
	}

	bool SwarmBossFormationAttackState::ReserveLaunch(SwarmBossStateContext& a_context)
	{
		if (!a_context.pPlatoonLeaders) return false;
		auto& _world = *a_context.pObject->pWorld;
		if (!_world.HasResource<InstanceResource::SwarmMissileResource>()) return false;

		const auto& _platoons = *a_context.pPlatoonLeaders;
		const size_t _count = _platoons.size();

		// 尾(外周)から頭(内側)へ。居なくなった小隊長は飛ばす
		while (m_fireCursor < _count)
		{
			const size_t _index = _count - 1 - m_fireCursor;
			++m_fireCursor;

			const Engine::ECS::Entity _platoon = _platoons[_index];
			if (!_world.IsAliveEntity(_platoon)) continue;

			// ボイドが残っていなければ SwarmMissileSystem が捨てる
			_world.RefResource<InstanceResource::SwarmMissileResource>().launchRequests.push_back(_platoon);
			++m_launchCount;
			return true;
		}
		return false;
	}

	void SwarmBossFormationAttackState::WriteMissileResource(SwarmBossStateContext& a_context) const
	{
		// 構える前は書かない(前の攻撃で飛んでいるミサイルの飛び方を変えないように)
		if (m_phase != EPhase::Windup && m_phase != EPhase::Fire && m_phase != EPhase::Recover) return;

		auto& _world = *a_context.pObject->pWorld;
		if (!_world.HasResource<InstanceResource::SwarmMissileResource>()) return;

		auto& _res = _world.RefResource<InstanceResource::SwarmMissileResource>();
		_res.ringCenter       = m_center;
		_res.launchSpeed      = m_missileLaunchSpeed;
		_res.launchTime       = m_missileLaunchTime;
		_res.launchUp         = m_missileLaunchUp;
		_res.launchOut        = m_missileLaunchOut;
		_res.launchForwardDir = m_normal;
		_res.launchForward    = m_missileLaunchForward;
		_res.launchSpread     = m_missileLaunchSpread;
		_res.speed            = m_missileSpeed;
		_res.acceleration     = m_missileAcceleration;
		_res.turnSpeedDeg     = m_missileTurnSpeedDeg;
		_res.loseAngleDeg     = m_missileLoseAngleDeg;
		_res.lifeTime         = m_missileLifeTime;
		_res.explodeRadius    = m_missileExplodeRadius;
		_res.damage           = m_missileDamage;
		_res.color            = m_missileColor;
		_res.intensity        = m_missileIntensity;
	}

	void SwarmBossFormationAttackState::DrawFormation(SwarmBossStateContext& a_context) const
	{
		if (!a_context.pObject->pServices || !a_context.pObject->pServices->pDebugDraw) return;
		auto& _debugDraw = *a_context.pObject->pServices->pDebugDraw;

		Math::Vector3 _u = {};
		Math::Vector3 _v = {};
		MakePlaneBasis(m_normal, _u, _v);

		// 外周の円と、面の表の向き
		const float _radius = std::max(m_innerRadius, 0.0f) + std::max(m_ringSpacing, 0.0f) * std::max(m_turns, 0.0f);

		Math::Vector3 _prev = m_center + _u * _radius;
		for (int _i = 1; _i <= FORMATION_DRAW_SEGMENTS; ++_i)
		{
			const float _a = DirectX::XM_2PI * static_cast<float>(_i) / FORMATION_DRAW_SEGMENTS;
			const Math::Vector3 _next = m_center + (_u * std::cos(_a) + _v * std::sin(_a)) * _radius;
			_debugDraw.DrawLine(_prev, _next, Engine::Color::RED);
			_prev = _next;
		}
		_debugDraw.DrawLine(m_center, m_center + m_normal * _radius * 0.5f, Engine::Color::GREEN);
	}

	void SwarmBossFormationAttackState::Finish(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		m_phase = EPhase::End;

		if (a_context.pMachine)
		{
			a_context.pMachine->ReserveChangeState(ESwarmBossState::RandomWalk);
		}
	}

	void SwarmBossFormationAttackState::ArchiveAppended(Engine::Persistence::Archive& a_ar)
	{
		// ステートの調整値は同じ階層に並ぶので、名前の頭に FormationAttack を付けて区別する
		a_ar.Field("FormationAttackDistance", m_distance);
		a_ar.Field("FormationAttackHeight", m_height);
		a_ar.Field("FormationAttackInnerRadius", m_innerRadius);
		a_ar.Field("FormationAttackRingSpacing", m_ringSpacing);
		a_ar.Field("FormationAttackTurns", m_turns);
		a_ar.Field("FormationAttackRollSpeedDeg", m_rollSpeedDeg);
		a_ar.Field("FormationAttackFireRollSpeedDeg", m_fireRollSpeedDeg);
		a_ar.Field("FormationAttackFaceTurnSpeedDeg", m_faceTurnSpeedDeg);
		a_ar.Field("FormationAttackAimHeight", m_aimHeight);
		a_ar.Field("FormationAttackApproachSpeedScale", m_approachSpeedScale);
		a_ar.Field("FormationAttackArriveDistance", m_arriveDistance);
		a_ar.Field("FormationAttackApproachMaxTime", m_approachMaxTime);
		a_ar.Field("FormationAttackCenterGain", m_centerGain);
		a_ar.Field("FormationAttackJoinTime", m_joinTime);
		a_ar.Field("FormationAttackAimTime", m_aimTime);
		a_ar.Field("FormationAttackWindupTime", m_windupTime);
		a_ar.Field("FormationAttackWindupWaveScale", m_windupWaveScale);
		a_ar.Field("FormationAttackRecoverTime", m_recoverTime);
		a_ar.Field("FormationAttackFireInterval", m_fireInterval);
		a_ar.Field("FormationAttackFireRounds", m_fireRounds);

		a_ar.Field("FormationAttackMissileLaunchSpeed", m_missileLaunchSpeed);
		a_ar.Field("FormationAttackMissileLaunchTime", m_missileLaunchTime);
		a_ar.Field("FormationAttackMissileLaunchUp", m_missileLaunchUp);
		a_ar.Field("FormationAttackMissileLaunchOut", m_missileLaunchOut);
		a_ar.Field("FormationAttackMissileLaunchForward", m_missileLaunchForward);
		a_ar.Field("FormationAttackMissileLaunchSpread", m_missileLaunchSpread);
		a_ar.Field("FormationAttackMissileSpeed", m_missileSpeed);
		a_ar.Field("FormationAttackMissileAcceleration", m_missileAcceleration);
		a_ar.Field("FormationAttackMissileTurnSpeedDeg", m_missileTurnSpeedDeg);
		a_ar.Field("FormationAttackMissileLifeTime", m_missileLifeTime);
		a_ar.Field("FormationAttackMissileExplodeRadius", m_missileExplodeRadius);
		a_ar.Field("FormationAttackMissileDamage", m_missileDamage);
		a_ar.Field("FormationAttackMissileColor", m_missileColor);
		a_ar.Field("FormationAttackMissileIntensity", m_missileIntensity);
		a_ar.Field("FormationAttackMissileLoseAngleDeg", m_missileLoseAngleDeg);
	}

	void SwarmBossFormationAttackState::DrawInspector()
	{
		Engine::EditorField::Field("プレイヤーからの距離", m_distance, 0.5f, 0.0f);
		Engine::EditorField::Tooltip("円陣の中心を、プレイヤーから見てリーダーの居る側へこれだけ離す(水平)");
		Engine::EditorField::Field("中心の高さ", m_height, 0.5f);
		Engine::EditorField::Tooltip("プレイヤーの足元から。外周の半径より低いと地面に埋まる");
		Engine::EditorField::Field("内側の半径", m_innerRadius, 0.5f, 0.0f);
		Engine::EditorField::Field("1周ごとの広がり", m_ringSpacing, 0.5f, 0.0f);
		Engine::EditorField::Field("周回数", m_turns, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("外周の半径 = 内側の半径 + 1周ごとの広がり × 周回数");
		Engine::EditorField::Field("面の中で回る速さ(度/秒)", m_rollSpeedDeg, 0.5f);
		Engine::EditorField::Field("撃つ間に回る速さ(度/秒)", m_fireRollSpeedDeg, 0.5f);
		Engine::EditorField::Field("プレイヤーへ向け直す速さ(度/秒)", m_faceTurnSpeedDeg, 0.5f, 0.0f);
		Engine::EditorField::Field("向ける先の高さ", m_aimHeight, 0.05f);

		Engine::EditorField::Line();
		Engine::EditorField::Field("中心へ向かう速さの倍率", m_approachSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Field("到着とみなす距離", m_arriveDistance, 0.1f, 0.0f);
		Engine::EditorField::Field("中心へ向かう最長時間", m_approachMaxTime, 0.1f, 0.0f);
		Engine::EditorField::Field("中心からのずれを詰める強さ", m_centerGain, 0.05f, 0.0f);
		Engine::EditorField::Field("並び終えるまでの時間", m_joinTime, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("頭から尾へ順に置き場所へ加わる");
		Engine::EditorField::Field("向き直る時間", m_aimTime, 0.05f, 0.0f);
		Engine::EditorField::Field("構える時間", m_windupTime, 0.05f, 0.0f);
		Engine::EditorField::Field("構えのウェーブの倍率", m_windupWaveScale, 0.05f, 0.0f);
		Engine::EditorField::Field("撃ち終えてから戻るまでの時間", m_recoverTime, 0.05f, 0.0f);
		Engine::EditorField::Field("切り離す間隔", m_fireInterval, 0.005f, 0.01f);
		Engine::EditorField::Tooltip("小隊長ごとに1体ずつ、外周(尾)から内側(頭)へ順番に切り離す");
		Engine::EditorField::Field("撃つ巡の数", m_fireRounds);

		Engine::EditorField::Line();
		Engine::EditorField::Field("ミサイル : 打ち上げの速さ", m_missileLaunchSpeed, 0.5f, 0.0f);
		Engine::EditorField::Field("ミサイル : 打ち上げの長さ", m_missileLaunchTime, 0.01f, 0.0f);
		Engine::EditorField::Field("ミサイル : 打ち上げの上向きの重み", m_missileLaunchUp, 0.01f);
		Engine::EditorField::Field("ミサイル : 打ち上げの外向きの重み", m_missileLaunchOut, 0.01f);
		Engine::EditorField::Field("ミサイル : 打ち上げの前向きの重み", m_missileLaunchForward, 0.01f);
		Engine::EditorField::Tooltip("円陣の面の表(プレイヤーの方)");
		Engine::EditorField::Field("ミサイル : 打ち上げのばらつき", m_missileLaunchSpread, 0.01f, 0.0f);
		Engine::EditorField::Field("ミサイル : 最高速", m_missileSpeed, 0.5f, 0.0f);
		Engine::EditorField::Field("ミサイル : 加速度", m_missileAcceleration, 0.5f, 0.0f);
		Engine::EditorField::Field("ミサイル : 曲がる速さ", m_missileTurnSpeedDeg, 1.0f, 0.0f);
		Engine::EditorField::Tooltip("度/秒。小さいほど大回りになり、避けやすい");
		Engine::EditorField::Field("ミサイル : 見失う角度", m_missileLoseAngleDeg, 1.0f, 0.0f, 180.0f);
		Engine::EditorField::Tooltip("一度捉えたあと、プレイヤーがこの角度より外へ外れたら直進する(90 = 通り過ぎたら諦める / 0 = 見失わない)");
		Engine::EditorField::Field("ミサイル : 自爆までの時間", m_missileLifeTime, 0.1f, 0.0f);
		Engine::EditorField::Field("ミサイル : 自爆する距離", m_missileExplodeRadius, 0.1f, 0.0f);
		Engine::EditorField::Field("ミサイル : ダメージ", m_missileDamage, 0.5f, 0.0f);
		Engine::EditorField::ColorField("ミサイル : 発光色", m_missileColor);
		Engine::EditorField::Field("ミサイル : 発光の強さ", m_missileIntensity, 0.1f, 0.0f);

		// 実行中の状態は表示のみ
		Engine::EditorField::Value("フェーズ", "%s (%.1f 秒)", std::string(magic_enum::enum_name(m_phase)).c_str(), m_phaseTime);
		Engine::EditorField::Value("中心", "%.1f, %.1f, %.1f", m_center.x, m_center.y, m_center.z);
		Engine::EditorField::Value("切り離した数", "%u (%u 巡目)", m_launchCount, m_fireRound + 1);
	}
}
