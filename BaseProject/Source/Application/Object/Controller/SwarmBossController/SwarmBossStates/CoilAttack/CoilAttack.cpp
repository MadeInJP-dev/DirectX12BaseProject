#include "CoilAttack.h"

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
#include "Application/Components/Movement/ActualVelocityComponent.h"
#include "Application/Components/Input/PlayerControllTag.h"
#include "Application/Components/Boid/SerchGroundComponent.h"
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

		// デバッグ表示で輪を何本の線分で描くか
		constexpr int RING_DRAW_SEGMENTS = 48;
	}

	void SwarmBossCoilAttackState::Enter(SwarmBossStateContext& a_context)
	{
		ChangePhase(EPhase::Approach);

		m_angle         = 0.0f;
		m_turnSign      = 1.0f;
		m_isGroundKnown = false;
		m_launchTimer   = 0.0f;
		m_launchCursor  = 0;
		m_launchCount   = 0;

		// 中心は最初に見つけたプレイヤーの位置から始める(見つからなければ Update で抜ける)
		if (a_context.pObject && a_context.pObject->pWorld && FindPlayerPos(*a_context.pObject, m_playerPos))
		{
			m_center = m_playerPos;
		}
	}

	void SwarmBossCoilAttackState::Update(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;
		const float _dt = a_context.pObject->dt;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;
		if (!_world.HasComponent<Component::LocalTransformComponent>(_leader)) return;

		// プレイヤーが居ない(倒された / まだ湧いていない)なら巻く相手が居ない
		if (!FindPlayerPos(*a_context.pObject, m_playerPos))
		{
			Finish(a_context);
			return;
		}

		const Math::Vector3 _pos = _world.RefData<Component::LocalTransformComponent>(_leader)->pos;

		// 入力 1 で出る速さ。輪の上の目標点をリーダーの速さで進めるのに使う
		const float _moveSpeed = _world.HasComponent<Component::MovementParamsComponent>(_leader)
			? _world.RefData<Component::MovementParamsComponent>(_leader)->moveSpeed
			: 0.0f;

		// 地表の高さ(SerchGroundSystem が書いた結果)。
		// レイが届かない高さに居るフレームは最後に見た値のまま。一度も見ていなければプレイヤーの足元
		if (_world.HasComponent<Component::SerchGroundComponent>(_leader))
		{
			const Component::SerchGroundComponent& _ground = *_world.RefData<Component::SerchGroundComponent>(_leader);
			if (_ground.isFoundGround)
			{
				m_groundHeight  = _ground.groundHeight;
				m_isGroundKnown = true;
			}
		}
		if (!m_isGroundKnown) m_groundHeight = m_playerPos.y;

		m_radius = CalcRadius(a_context.wormLength);
		m_phaseTime += _dt;

		Math::Vector3 _intent = Math::Vector3(0.0f, 0.0f, 0.0f);

		switch (m_phase)
		{
		//--------------------------------------------------------------
		// 輪に入る : 輪の上で一番近い点(中心から見て今の自分側)へ向かう。
		// 輪に入るまでは中心をプレイヤーにぴったり付ける
		//--------------------------------------------------------------
		case EPhase::Approach:
		{
			m_center = m_playerPos;

			Math::Vector3 _out = _pos - m_center;
			_out.y = 0.0f;
			if (_out.LengthSquared() <= 1e-6f) _out = Math::Vector3(1.0f, 0.0f, 0.0f);
			_out.Normalize();

			Math::Vector3 _entry = m_center + _out * m_radius;
			_entry.y = m_groundHeight + CalcHeight(0.0f);

			const Math::Vector3 _toEntry = _entry - _pos;
			_intent = ScaledDir(_toEntry, std::max(m_approachSpeedScale, 0.0f));

			if (_toEntry.Length() <= m_arriveDistance || m_phaseTime >= m_approachMaxTime)
			{
				// 輪の上の、今の自分に一番近い角度から回り始める
				m_angle = std::atan2(_out.z, _out.x);

				// 回る向きは今進んでいる向きに近い方(急に折り返さないように)
				m_turnSign = (Math::Random::Float(0.0f, 1.0f) < 0.5f) ? 1.0f : -1.0f;
				if (_world.HasComponent<Component::ActualVelocityComponent>(_leader))
				{
					const Math::Vector3 _vel = _world.RefData<Component::ActualVelocityComponent>(_leader)->value;
					const Math::Vector3 _tangent(-std::sin(m_angle), 0.0f, std::cos(m_angle));
					const float _side = _vel.Dot(_tangent);
					if (std::fabs(_side) > 1e-3f) m_turnSign = (_side >= 0.0f) ? 1.0f : -1.0f;
				}

				// 巻き始めてから切り離しを始めるまでの時間。以降は間隔ごと
				m_launchTimer = std::max(m_launchStartDelay, 0.0f);

				// 巻き始めた瞬間に鳴く
				a_context.cry = ESwarmBossCry::Attack;

				ChangePhase(EPhase::Coil);
			}
			break;
		}

		//--------------------------------------------------------------
		// 巻く : 輪の上を進む目標点を追う
		//
		//   入力 = 目標点の速度 / moveSpeed  +  目標点とのずれ × 詰める強さ / moveSpeed
		//
		// 目標点は水平にはリーダーの速さで輪を回り、高さは正弦波で地面と上を行き来する
		//--------------------------------------------------------------
		case EPhase::Coil:
		{
			// 中心をプレイヤーへゆっくり寄せる(高さは使わない)
			const float _follow = std::clamp(m_centerFollowGain * _dt, 0.0f, 1.0f);
			m_center.x += (m_playerPos.x - m_center.x) * _follow;
			m_center.z += (m_playerPos.z - m_center.z) * _follow;

			const float _speed = _moveSpeed * std::max(m_coilSpeedScale, 0.0f);
			if (m_radius > 1e-3f)
			{
				m_angle += m_turnSign * (_speed / m_radius) * _dt;
				m_angle  = std::remainder(m_angle, DirectX::XM_2PI);	// 長く回っても精度が落ちないように畳む
			}

			const float _cos = std::cos(m_angle);
			const float _sin = std::sin(m_angle);

			Math::Vector3 _target = m_center + Math::Vector3(_cos, 0.0f, _sin) * m_radius;
			_target.y = m_groundHeight + CalcHeight(m_phaseTime);

			const Math::Vector3 _targetVel =
				Math::Vector3(-_sin, 0.0f, _cos) * (m_turnSign * _speed) +
				Math::Vector3(0.0f, CalcHeightSpeed(m_phaseTime), 0.0f);

			if (_moveSpeed > 0.0f)
			{
				_intent = _targetVel / _moveSpeed + (_target - _pos) * (m_followGain / _moveSpeed);
			}

			// 頭の小隊長から尾へ順番に、1体ずつ切り離す
			m_launchTimer -= _dt;
			while (m_launchTimer <= 0.0f)
			{
				ReserveLaunch(a_context);
				m_launchTimer += std::max(m_launchInterval, 0.01f);	// 0だと止まらないので下限を入れる
			}

			if (m_phaseTime >= m_coilTime)
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

		WriteMissileResource(a_context);
		DrawRing(a_context);
	}

	void SwarmBossCoilAttackState::Exit(SwarmBossStateContext& a_context)
	{
		// 巻く速さの入力を次のステートへ持ち越さない
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossCoilAttackState::ChangePhase(EPhase a_phase)
	{
		m_phase     = a_phase;
		m_phaseTime = 0.0f;
	}

	float SwarmBossCoilAttackState::CalcRadius(float a_wormLength) const
	{
		// 円周 = 体の長さ なら頭と尾がちょうど繋がる
		const float _radius = std::max(a_wormLength, 0.0f) / DirectX::XM_2PI * std::max(m_radiusScale, 0.0f);
		return std::max(_radius, m_minRadius);
	}

	float SwarmBossCoilAttackState::CalcHeight(float a_time) const
	{
		// 真ん中から上がり始める(輪に入るときの高さが真ん中なので、巻き始めで跳ねない)
		const float _mid = (m_highHeight + m_lowHeight) * 0.5f;
		const float _amp = (m_highHeight - m_lowHeight) * 0.5f;
		if (m_undulationPeriod <= 0.0f) return _mid;

		return _mid + _amp * std::sin(DirectX::XM_2PI * a_time / m_undulationPeriod);
	}

	float SwarmBossCoilAttackState::CalcHeightSpeed(float a_time) const
	{
		// CalcHeight の時間微分
		if (m_undulationPeriod <= 0.0f) return 0.0f;

		const float _amp   = (m_highHeight - m_lowHeight) * 0.5f;
		const float _omega = DirectX::XM_2PI / m_undulationPeriod;
		return _amp * _omega * std::cos(_omega * a_time);
	}

	void SwarmBossCoilAttackState::ReserveLaunch(SwarmBossStateContext& a_context)
	{
		if (!a_context.pPlatoonLeaders || a_context.pPlatoonLeaders->empty()) return;
		auto& _world = *a_context.pObject->pWorld;
		if (!_world.HasResource<InstanceResource::SwarmMissileResource>()) return;

		const auto& _platoons = *a_context.pPlatoonLeaders;
		const size_t _count = _platoons.size();

		// 居なくなった小隊長は飛ばして、次に居る小隊長へ。一周しても居なければ何もしない
		for (size_t _attempt = 0; _attempt < _count; ++_attempt)
		{
			const size_t _index = m_launchCursor % _count;
			m_launchCursor = (_index + 1) % _count;

			const Engine::ECS::Entity _platoon = _platoons[_index];
			if (!_world.IsAliveEntity(_platoon)) continue;

			// ボイドが残っていなければ SwarmMissileSystem が捨てる
			_world.RefResource<InstanceResource::SwarmMissileResource>().launchRequests.push_back(_platoon);
			++m_launchCount;
			return;
		}
	}

	void SwarmBossCoilAttackState::WriteMissileResource(SwarmBossStateContext& a_context)
	{
		auto& _world = *a_context.pObject->pWorld;
		if (!_world.HasResource<InstanceResource::SwarmMissileResource>()) return;

		auto& _res = _world.RefResource<InstanceResource::SwarmMissileResource>();
		_res.ringCenter    = m_center;
		_res.launchSpeed   = m_missileLaunchSpeed;
		_res.launchTime    = m_missileLaunchTime;
		_res.launchUp      = m_missileLaunchUp;
		_res.launchOut     = m_missileLaunchOut;
		_res.launchSpread  = m_missileLaunchSpread;
		_res.launchForward = 0.0f;	// 決まった向きは使わない(円陣ミサイルが書いた値を残さない)
		_res.speed         = m_missileSpeed;
		_res.acceleration  = m_missileAcceleration;
		_res.turnSpeedDeg  = m_missileTurnSpeedDeg;
		_res.loseAngleDeg  = m_missileLoseAngleDeg;
		_res.lifeTime      = m_missileLifeTime;
		_res.explodeRadius = m_missileExplodeRadius;
		_res.damage        = m_missileDamage;
		_res.color         = m_missileColor;
		_res.intensity     = m_missileIntensity;
	}

	void SwarmBossCoilAttackState::DrawRing(SwarmBossStateContext& a_context) const
	{
		if (!a_context.pObject->pServices || !a_context.pObject->pServices->pDebugDraw) return;
		auto& _debugDraw = *a_context.pObject->pServices->pDebugDraw;

		// 高さは真ん中(実際の目標点はここから上下する)
		const float _y = m_groundHeight + CalcHeight(0.0f);

		Math::Vector3 _prev = Math::Vector3(m_center.x + m_radius, _y, m_center.z);
		for (int _i = 1; _i <= RING_DRAW_SEGMENTS; ++_i)
		{
			const float _a = DirectX::XM_2PI * static_cast<float>(_i) / RING_DRAW_SEGMENTS;
			const Math::Vector3 _next(m_center.x + std::cos(_a) * m_radius, _y, m_center.z + std::sin(_a) * m_radius);
			_debugDraw.DrawLine(_prev, _next, Engine::Color::RED);
			_prev = _next;
		}
	}

	void SwarmBossCoilAttackState::Finish(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		m_phase = EPhase::End;

		if (a_context.pMachine)
		{
			a_context.pMachine->ReserveChangeState(ESwarmBossState::RandomWalk);
		}
	}

	void SwarmBossCoilAttackState::Archive(Engine::Persistence::Archive& a_ar)
	{
		// ステートの調整値は同じ階層に並ぶので、名前の頭に CoilAttack を付けて区別する
		a_ar.Field("CoilAttackRadiusScale", m_radiusScale);
		a_ar.Field("CoilAttackMinRadius", m_minRadius);
		a_ar.Field("CoilAttackCenterFollowGain", m_centerFollowGain);
		a_ar.Field("CoilAttackApproachSpeedScale", m_approachSpeedScale);
		a_ar.Field("CoilAttackArriveDistance", m_arriveDistance);
		a_ar.Field("CoilAttackApproachMaxTime", m_approachMaxTime);
		a_ar.Field("CoilAttackSpeedScale", m_coilSpeedScale);
		a_ar.Field("CoilAttackFollowGain", m_followGain);
		a_ar.Field("CoilAttackTime", m_coilTime);
		a_ar.Field("CoilAttackLowHeight", m_lowHeight);
		a_ar.Field("CoilAttackHighHeight", m_highHeight);
		a_ar.Field("CoilAttackUndulationPeriod", m_undulationPeriod);
		a_ar.Field("CoilAttackLaunchStartDelay", m_launchStartDelay);
		a_ar.Field("CoilAttackLaunchInterval", m_launchInterval);

		a_ar.Field("CoilAttackMissileLaunchSpeed", m_missileLaunchSpeed);
		a_ar.Field("CoilAttackMissileLaunchTime", m_missileLaunchTime);
		a_ar.Field("CoilAttackMissileLaunchUp", m_missileLaunchUp);
		a_ar.Field("CoilAttackMissileLaunchOut", m_missileLaunchOut);
		a_ar.Field("CoilAttackMissileLaunchSpread", m_missileLaunchSpread);
		a_ar.Field("CoilAttackMissileSpeed", m_missileSpeed);
		a_ar.Field("CoilAttackMissileAcceleration", m_missileAcceleration);
		a_ar.Field("CoilAttackMissileTurnSpeedDeg", m_missileTurnSpeedDeg);
		a_ar.Field("CoilAttackMissileLifeTime", m_missileLifeTime);
		a_ar.Field("CoilAttackMissileExplodeRadius", m_missileExplodeRadius);
		a_ar.Field("CoilAttackMissileDamage", m_missileDamage);
		a_ar.Field("CoilAttackMissileColor", m_missileColor);
		a_ar.Field("CoilAttackMissileIntensity", m_missileIntensity);
	}

	void SwarmBossCoilAttackState::ArchiveAppended(Engine::Persistence::Archive& a_ar)
	{
		a_ar.Field("CoilAttackMissileLoseAngleDeg", m_missileLoseAngleDeg);
	}

	void SwarmBossCoilAttackState::DrawInspector()
	{
		Engine::EditorField::Field("輪の半径の倍率", m_radiusScale, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("半径 = 体の長さ ÷ (2 × 円周率) × これ(1 で頭と尾がちょうど繋がる)");
		Engine::EditorField::Field("輪の半径の下限", m_minRadius, 0.5f, 0.0f);
		Engine::EditorField::Field("中心をプレイヤーへ寄せる強さ", m_centerFollowGain, 0.01f, 0.0f);
		Engine::EditorField::Field("輪に入るまでの速さの倍率", m_approachSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Field("到着とみなす距離", m_arriveDistance, 0.1f, 0.0f);
		Engine::EditorField::Field("輪に入る最長時間", m_approachMaxTime, 0.1f, 0.0f);
		Engine::EditorField::Field("巻く速さの倍率", m_coilSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("小隊長の倍率を超える速さにすると列が千切れる");
		Engine::EditorField::Field("ずれを詰める強さ", m_followGain, 0.05f, 0.0f);
		Engine::EditorField::Field("巻く長さ", m_coilTime, 0.1f, 0.0f);
		Engine::EditorField::Field("一番低いところの高さ", m_lowHeight, 0.5f);
		Engine::EditorField::Tooltip("地表からの高さ。負で地中");
		Engine::EditorField::Field("一番高いところの高さ", m_highHeight, 0.5f);
		Engine::EditorField::Field("地面と上を1往復する周期", m_undulationPeriod, 0.05f, 0.0f);
		Engine::EditorField::Field("切り離しを始めるまでの時間", m_launchStartDelay, 0.05f, 0.0f);
		Engine::EditorField::Field("切り離す間隔", m_launchInterval, 0.01f, 0.01f);
		Engine::EditorField::Tooltip("小隊長ごとに1体ずつ、頭から尾へ順番に切り離す");

		Engine::EditorField::Line();
		Engine::EditorField::Field("ミサイル : 打ち上げの速さ", m_missileLaunchSpeed, 0.5f, 0.0f);
		Engine::EditorField::Field("ミサイル : 打ち上げの長さ", m_missileLaunchTime, 0.01f, 0.0f);
		Engine::EditorField::Field("ミサイル : 打ち上げの上向きの重み", m_missileLaunchUp, 0.01f);
		Engine::EditorField::Field("ミサイル : 打ち上げの外向きの重み", m_missileLaunchOut, 0.01f);
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
		Engine::EditorField::Value("輪", "半径 %.1f m / 中心 %.1f, %.1f", m_radius, m_center.x, m_center.z);
		Engine::EditorField::Value("角度", "%.1f 度 (%s)", DirectX::XMConvertToDegrees(m_angle), m_turnSign > 0.0f ? "+" : "-");
		Engine::EditorField::Value("切り離した数", "%u (次の小隊長 %u)", m_launchCount, static_cast<uint32_t>(m_launchCursor));
	}
}
