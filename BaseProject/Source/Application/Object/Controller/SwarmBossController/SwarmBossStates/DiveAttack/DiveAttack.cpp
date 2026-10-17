#include "DiveAttack.h"

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

		// デバッグ表示で曲線を何本の線分で描くか
		constexpr int CURVE_DRAW_SEGMENTS = 24;
	}

	void SwarmBossDiveAttackState::Enter(SwarmBossStateContext& /*a_context*/)
	{
		ChangePhase(EPhase::Rise);
		m_curveT  = 0.0f;
		m_moveDir = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossDiveAttackState::Update(SwarmBossStateContext& a_context)
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

		// 入力 1 で出る速さ。曲線上の目標点をリーダーと同じ速さで進めるのに使う
		const float _moveSpeed = _world.HasComponent<Component::MovementParamsComponent>(_leader)
			? _world.RefData<Component::MovementParamsComponent>(_leader)->moveSpeed
			: 0.0f;

		// プレイヤーが居ない(倒された / まだ湧いていない)なら狙う先が無い。
		// 降り始めた後は組んだ曲線のまま続ける
		const bool _isFoundPlayer = FindPlayerPos(*a_context.pObject, m_playerPos);
		if (!_isFoundPlayer && (m_phase == EPhase::Rise || m_phase == EPhase::Hover))
		{
			Finish(a_context);
			return;
		}

		// 地面との関係(SerchGroundSystem が書いた結果)。潜るのに使う
		m_isGroundFound = false;
		m_isUnderGround = false;
		if (_world.HasComponent<Component::SerchGroundComponent>(_leader))
		{
			const Component::SerchGroundComponent& _ground = *_world.RefData<Component::SerchGroundComponent>(_leader);
			m_isGroundFound = _ground.isFoundGround != 0;
			m_isUnderGround = _ground.isUnderGround != 0;
			if (m_isGroundFound)
			{
				m_groundHeight = _ground.groundHeight;
				m_depth        = m_groundHeight - _pos.y;
			}
		}

		m_phaseTime += _dt;

		Math::Vector3 _intent = Math::Vector3(0.0f, 0.0f, 0.0f);

		// 狙う・急降下・潜る間は、体のボイドを胴回りで回す(回転を上げていくのはコントローラー)
		if (m_phase == EPhase::Hover || m_phase == EPhase::Dive || m_phase == EPhase::Burrow)
		{
			a_context.bodySpinRate = m_spinRate;
		}

		switch (m_phase)
		{
		//--------------------------------------------------------------
		// 離れる : プレイヤーから見て自分側の、高いところへ
		//--------------------------------------------------------------
		case EPhase::Rise:
		{
			// 離れる向き(水平)。プレイヤーの真上に居て向きが出ないなら +Z へ
			Math::Vector3 _away = _pos - m_playerPos;
			_away.y = 0.0f;
			if (_away.LengthSquared() <= 1e-6f) _away = Math::Vector3(0.0f, 0.0f, 1.0f);
			_away.Normalize();

			m_launchPos = m_playerPos + _away * m_launchDistance + Math::Vector3::Up() * m_launchHeight;

			const Math::Vector3 _toLaunch = m_launchPos - _pos;
			_intent = ScaledDir(_toLaunch, std::clamp(m_riseThrottle, 0.0f, 1.0f));

			if (_toLaunch.Length() <= m_arriveDistance || m_phaseTime >= m_riseMaxTime)
			{
				ChangePhase(EPhase::Hover);
			}
			break;
		}

		//--------------------------------------------------------------
		// 狙う : ゆっくりプレイヤーへ寄って体を向ける。終わった瞬間の位置で曲線を組む
		//--------------------------------------------------------------
		case EPhase::Hover:
		{
			_intent = ScaledDir(m_playerPos - _pos, std::clamp(m_hoverThrottle, 0.0f, 1.0f));

			if (m_phaseTime >= m_hoverTime)
			{
				BuildCurve(_pos, m_playerPos);
				ChangePhase(EPhase::Dive);
			}
			break;
		}

		//--------------------------------------------------------------
		// 急降下 : 曲線上の目標点を進め、それを追う
		//
		//   入力 = 目標点の進む向き × 速さ  +  目標点とのずれ × 詰める強さ
		//
		// 目標点はリーダーの速さで曲線上を進める(接線の長さで割ると、曲線上の距離になる)
		//--------------------------------------------------------------
		case EPhase::Dive:
		{
			const float _speed = _moveSpeed * std::max(m_diveSpeedScale, 0.0f);

			const float _tangentLen = CurveTangent(m_curveT).Length();
			if (_tangentLen > 1e-4f)
			{
				m_curveT += _speed * _dt / _tangentLen;
			}
			else
			{
				m_curveT = 1.0f;	// 始点と終点が重なっていて進めない
			}
			m_curveT = std::min(m_curveT, 1.0f);

			const Math::Vector3 _tangent = CurveTangent(m_curveT);
			if (_tangent.LengthSquared() > 1e-6f)
			{
				m_moveDir = _tangent;
				m_moveDir.Normalize();
			}

			_intent = m_moveDir * std::max(m_diveSpeedScale, 0.0f);
			if (_moveSpeed > 0.0f)
			{
				_intent += (CurvePos(m_curveT) - _pos) * (m_followGain / _moveSpeed);
			}

			// 曲線のデバッグ表示
			if (a_context.pObject->pServices && a_context.pObject->pServices->pDebugDraw)
			{
				auto& _debugDraw = *a_context.pObject->pServices->pDebugDraw;
				Math::Vector3 _prev = m_curveStart;
				for (int _i = 1; _i <= CURVE_DRAW_SEGMENTS; ++_i)
				{
					const Math::Vector3 _next = CurvePos(static_cast<float>(_i) / CURVE_DRAW_SEGMENTS);
					_debugDraw.DrawLine(_prev, _next, Engine::Color::RED);
					_prev = _next;
				}
			}

			// 終点(プレイヤーが居た所)を通り過ぎた(か時間切れ)なら、その勢いのまま地面へ
			if (m_curveT >= 1.0f || m_phaseTime >= m_diveMaxTime)
			{
				ChangePhase(EPhase::Burrow);
			}
			break;
		}

		//--------------------------------------------------------------
		// 潜る : 進んでいた向き(水平)へ少し進みながら、地表から決まった深さまで突っ込む。
		// 速さは急降下のまま(勢いを落とさずに地面へ入る)
		//--------------------------------------------------------------
		case EPhase::Burrow:
		{
			const float _speedScale = std::max(m_diveSpeedScale, 0.0f);
			const Math::Vector3 _forwardXZ = ScaledDir(Math::Vector3(m_moveDir.x, 0.0f, m_moveDir.z), 1.0f);

			if (m_isGroundFound)
			{
				const float _targetY = m_groundHeight - m_burrowDepth;
				const Math::Vector3 _target =
					_pos + _forwardXZ * std::max(m_burrowForward, 0.0f) + Math::Vector3(0.0f, _targetY - _pos.y, 0.0f);
				_intent = ScaledDir(_target - _pos, _speedScale);
			}
			else
			{
				// レイの届く範囲に地面が無い(まだ高い)。見つかるまで斜め下へ
				_intent = ScaledDir(_forwardXZ * 0.3f + Math::Vector3::Down(), _speedScale);
			}

			if (_intent.LengthSquared() > 1e-6f)
			{
				m_moveDir = _intent;
				m_moveDir.Normalize();
			}

			// 地中で狙いの深さに入ったら(か時間切れで)、その深さのまま進む余韻へ
			const bool _isReachedDepth = m_isUnderGround && m_depth >= m_burrowDepth - m_depthTolerance;
			if (_isReachedDepth || m_phaseTime >= m_burrowMaxTime)
			{
				ChangePhase(EPhase::Recover);
			}
			break;
		}

		//--------------------------------------------------------------
		// 余韻 : 潜った深さを保ったまま、同じ向き(水平)へ進んでから徘徊へ戻る。
		// 頭が止まると体が地表から出たまま残るので、その間に体を地中へ引き込む
		//--------------------------------------------------------------
		case EPhase::Recover:
		{
			const Math::Vector3 _forwardXZ = ScaledDir(Math::Vector3(m_moveDir.x, 0.0f, m_moveDir.z), 1.0f);
			Math::Vector3 _target = _pos + _forwardXZ * std::max(m_burrowForward, 1.0f);
			if (m_isGroundFound) _target.y = m_groundHeight - m_burrowDepth;

			_intent = ScaledDir(_target - _pos, std::clamp(m_recoverThrottle, 0.0f, 1.0f));

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

	void SwarmBossDiveAttackState::Exit(SwarmBossStateContext& a_context)
	{
		// 急降下の速さの入力を次のステートへ持ち越さない
		if (!a_context.pObject || !a_context.pObject->pWorld) return;
		auto& _world = *a_context.pObject->pWorld;

		const auto _leader = a_context.leaderEntity;
		if (!_world.IsAliveEntity(_leader)) return;
		if (!_world.HasComponent<Component::MoveIntentComponent>(_leader)) return;

		_world.RefData<Component::MoveIntentComponent>(_leader)->value = Math::Vector3(0.0f, 0.0f, 0.0f);
	}

	void SwarmBossDiveAttackState::ChangePhase(EPhase a_phase)
	{
		m_phase     = a_phase;
		m_phaseTime = 0.0f;
	}

	void SwarmBossDiveAttackState::BuildCurve(const Math::Vector3& a_start, const Math::Vector3& a_end)
	{
		// 制御点を中間の真上に置く → 縦軸の放物線になる
		m_curveStart   = a_start;
		m_curveEnd     = a_end;
		m_curveControl = (a_start + a_end) * 0.5f + Math::Vector3::Up() * m_arcHeight;
		m_curveT       = 0.0f;
	}

	Math::Vector3 SwarmBossDiveAttackState::CurvePos(float a_t) const
	{
		// B(t) = (1-t)^2 P0 + 2(1-t)t P1 + t^2 P2
		const float _u = 1.0f - a_t;
		return m_curveStart * (_u * _u) + m_curveControl * (2.0f * _u * a_t) + m_curveEnd * (a_t * a_t);
	}

	Math::Vector3 SwarmBossDiveAttackState::CurveTangent(float a_t) const
	{
		// B'(t) = 2(1-t)(P1-P0) + 2t(P2-P1)
		return (m_curveControl - m_curveStart) * (2.0f * (1.0f - a_t)) + (m_curveEnd - m_curveControl) * (2.0f * a_t);
	}

	void SwarmBossDiveAttackState::Finish(SwarmBossStateContext& a_context)
	{
		if (m_phase == EPhase::End) return;
		m_phase = EPhase::End;

		if (a_context.pMachine)
		{
			a_context.pMachine->ReserveChangeState(ESwarmBossState::RandomWalk);
		}
	}

	void SwarmBossDiveAttackState::Archive(Engine::Persistence::Archive& a_ar)
	{
		// ステートの調整値は同じ階層に並ぶので、名前の頭に DiveAttack を付けて区別する
		a_ar.Field("DiveAttackLaunchDistance", m_launchDistance);
		a_ar.Field("DiveAttackLaunchHeight", m_launchHeight);
		a_ar.Field("DiveAttackRiseThrottle", m_riseThrottle);
		a_ar.Field("DiveAttackArriveDistance", m_arriveDistance);
		a_ar.Field("DiveAttackRiseMaxTime", m_riseMaxTime);
		a_ar.Field("DiveAttackHoverTime", m_hoverTime);
		a_ar.Field("DiveAttackHoverThrottle", m_hoverThrottle);
		a_ar.Field("DiveAttackArcHeight", m_arcHeight);
		a_ar.Field("DiveAttackSpeedScale", m_diveSpeedScale);
		a_ar.Field("DiveAttackFollowGain", m_followGain);
		a_ar.Field("DiveAttackMaxTime", m_diveMaxTime);
		a_ar.Field("DiveAttackRecoverTime", m_recoverTime);
		a_ar.Field("DiveAttackRecoverThrottle", m_recoverThrottle);
	}

	void SwarmBossDiveAttackState::ArchiveAppended(Engine::Persistence::Archive& a_ar)
	{
		a_ar.Field("DiveAttackBurrowDepth", m_burrowDepth);
		a_ar.Field("DiveAttackBurrowForward", m_burrowForward);
		a_ar.Field("DiveAttackDepthTolerance", m_depthTolerance);
		a_ar.Field("DiveAttackBurrowMaxTime", m_burrowMaxTime);
		a_ar.Field("DiveAttackSpinRate", m_spinRate);
	}

	void SwarmBossDiveAttackState::DrawInspector()
	{
		Engine::EditorField::Field("離れる距離", m_launchDistance, 0.5f, 0.0f);
		Engine::EditorField::Field("上がる高さ", m_launchHeight, 0.5f);
		Engine::EditorField::Field("離れるときの移動入力の強さ", m_riseThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("到着とみなす距離", m_arriveDistance, 0.1f, 0.0f);
		Engine::EditorField::Field("離れる最長時間", m_riseMaxTime, 0.1f, 0.0f);
		Engine::EditorField::Field("狙う長さ", m_hoverTime, 0.05f, 0.0f);
		Engine::EditorField::Field("狙う間の移動入力の強さ", m_hoverThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("曲線の膨らみ", m_arcHeight, 0.5f);
		Engine::EditorField::Field("急降下の速さの倍率", m_diveSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Field("ずれを詰める強さ", m_followGain, 0.05f, 0.0f);
		Engine::EditorField::Field("急降下の最長時間", m_diveMaxTime, 0.1f, 0.0f);
		Engine::EditorField::Field("潜る深さ", m_burrowDepth, 0.5f, 0.0f);
		Engine::EditorField::Tooltip("プレイヤーを通り過ぎた後、地表からこの深さまで潜る");
		Engine::EditorField::Field("潜りながら進む量", m_burrowForward, 0.5f, 0.0f);
		Engine::EditorField::Tooltip("水平の距離。大きいほど浅い角度で地面へ突っ込む");
		Engine::EditorField::Field("深さの許容範囲", m_depthTolerance, 0.1f, 0.0f);
		Engine::EditorField::Field("潜る最長時間", m_burrowMaxTime, 0.1f, 0.0f);
		Engine::EditorField::Field("余韻の長さ", m_recoverTime, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("潜った深さのまま進む長さ");
		Engine::EditorField::Field("余韻中の移動入力の強さ", m_recoverThrottle, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("小隊長の倍率を超える速さにすると列が千切れる");
		Engine::EditorField::Field("胴回りの回転の強さ", m_spinRate, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("狙う・急降下・潜る間、ボイドを小隊長の胴回りで回す(最高速に対する割合。0 で回さない)");

		// 実行中の状態は表示のみ
		Engine::EditorField::Value("フェーズ", "%s (%.1f 秒)", std::string(magic_enum::enum_name(m_phase)).c_str(), m_phaseTime);
		Engine::EditorField::Value("プレイヤーの位置", "%.1f, %.1f, %.1f", m_playerPos.x, m_playerPos.y, m_playerPos.z);
		Engine::EditorField::Value("離れる先", "%.1f, %.1f, %.1f", m_launchPos.x, m_launchPos.y, m_launchPos.z);
		Engine::EditorField::Value("曲線上の位置 t", "%.2f", m_curveT);
		Engine::EditorField::Value("地表の高さ", "%.1f (深さ %.1f, %s)", m_groundHeight, m_depth, m_isUnderGround ? "地中" : "地上");
	}
}
