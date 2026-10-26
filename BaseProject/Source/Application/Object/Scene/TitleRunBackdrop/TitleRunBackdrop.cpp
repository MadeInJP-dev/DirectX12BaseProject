#include "TitleRunBackdrop.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/ECS/World/World.h"
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/EditorField/EditorField.h"

#include "../../ObjectPicker.h"
#include "Application/Object/Scene/PlayerSpawner/PlayerSpawner.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Components/Movement/LookAngleComponent.h"
#include "Application/Components/Movement/MoveIntentComponent.h"
#include "Application/Components/Movement/BoostIntentComponent.h"
#include "Application/Components/Movement/BoostStateComponent.h"
#include "Application/Components/Movement/BoostParamsComponent.h"
#include "Application/Components/Movement/ChargeDashComponent.h"
#include "Application/Components/Movement/ActualVelocityComponent.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Components/Camera/FocusParamComponent.h"
#include "Application/Systems/Transform/HierarchyTransform.h"

namespace App::Object
{
	namespace
	{
		constexpr float TWO_PI = DirectX::XM_2PI;

		// 定点の置き場所を決めるときに、進路を辿る刻み(秒)
		constexpr float PREDICT_STEP = 1.0f / 60.0f;

		// 定点の足元の高さを探すレイ(置き場所の上から下へ)
		constexpr float GROUND_PROBE_UP = 50.0f;
		constexpr float GROUND_PROBE_LENGTH = 100.0f;

		// ピントを合わせる機体の高さ(m)
		constexpr float FOCUS_HEIGHT = 2.5f;

		// 指数減衰の補間係数。rate が 0 以下ならすぐ寄せる
		float DampFactor(float a_rate, float a_dt)
		{
			if (!(a_rate > 0.0f) || !(a_dt > 0.0f)) return 1.0f;
			return 1.0f - std::exp(-a_rate * a_dt);
		}

		// 角度(度)を [-180, 180) へ畳む
		float WrapDegree(float a_deg)
		{
			float _deg = std::fmod(a_deg + 180.0f, 360.0f);
			if (_deg < 0.0f) _deg += 360.0f;
			return _deg - 180.0f;
		}

		// 周期 a_period(秒)の正弦波。周期が 0 以下なら揺らさない
		float Sway(float a_time, float a_period)
		{
			if (!(a_period > 0.0f)) return 0.0f;
			return std::sin(TWO_PI * a_time / a_period);
		}

		// Yaw(度)の水平前方 / 右(左手系 +Z 前方。LookAngleComponent と同じ規約)
		Math::Vector3 ForwardOf(float a_yawDeg)
		{
			const float _rad = DirectX::XMConvertToRadians(a_yawDeg);
			return { std::sin(_rad), 0.0f, std::cos(_rad) };
		}
		Math::Vector3 RightOf(float a_yawDeg)
		{
			const float _rad = DirectX::XMConvertToRadians(a_yawDeg);
			return { std::cos(_rad), 0.0f, -std::sin(_rad) };
		}

		ETitleShot NextShot(ETitleShot a_shot)
		{
			const int _next = (static_cast<int>(a_shot) + 1) % static_cast<int>(ETitleShot::Count);
			return static_cast<ETitleShot>(_next);
		}
	}

	//======================================================================================
	// 更新
	//======================================================================================
	void TitleRunBackdrop::Update(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;

		m_time += a_context.dt;

		// 機体はスポナーの Start で出る。出るまでは何もしない
		const Engine::ECS::Entity _runner = FindRunner(a_context);
		if (_runner == Engine::ECS::Limits::INVALID_ENTITY) return;

		DriveRunner(a_context, _runner);
		FollowFloor(a_context, _runner);
		PlaceCamera(a_context, _runner);
	}

	Engine::ECS::Entity TitleRunBackdrop::FindRunner(Engine::GameObject::ObjectContext& a_context) const
	{
		const auto* _pSpawner = Picker::Find<PlayerSpawner>(a_context.pObjectManager, m_spawnerGUID);
		if (!_pSpawner) return Engine::ECS::Limits::INVALID_ENTITY;

		const Engine::ECS::Entity _runner = _pSpawner->GetPlayerEntity();
		if (_runner == Engine::ECS::Limits::INVALID_ENTITY || !a_context.pWorld->IsAliveEntity(_runner))
		{
			return Engine::ECS::Limits::INVALID_ENTITY;
		}
		return _runner;
	}

	//======================================================================================
	// 機体を走らせる
	//--------------------------------------------------------------------------------------
	// 入力の代わりに機体のコンポーネントへ直接書く。プレイヤーの入力(InputMoveSystem)は
	// PlayerControllTag を持つものにしか書かないので、入力で動かない機体なら上書きされない。
	// 書いた値は次のフレームの ECS が読む。
	//
	// ・チャージダッシュは溜めずに「出ている」状態にする。出ていなければ出た瞬間の印
	//   (isJustDashed)も立てて、撃ち出しの演出と音を一度鳴らす。
	//   ダッシュは発動時の向き(dashDir)へ直進する作りなので、視線を回したぶん毎フレーム書き直す。
	//   移動入力は前だけにしておく(後ろ向きの入力はダッシュを止める。横の入力は横へ流れる)。
	// ・チャージダッシュを持たない機体はブーストの押しっぱなしで走らせる。
	//   押した瞬間(isBoostTriger)を立てると踏み込みの初動が毎フレーム入るので立てない。
	// ・ダッシュもブーストも燃料が尽きると止まるので、満タンに戻し続ける。
	//======================================================================================
	void TitleRunBackdrop::DriveRunner(Engine::GameObject::ObjectContext& a_context, Engine::ECS::Entity a_runner)
	{
		auto& _world = *a_context.pWorld;

		float _yaw = 0.0f;
		if (auto* _pLook = _world.RefData<Component::LookAngleComponent>(a_runner))
		{
			_pLook->Yaw = WrapDegree(_pLook->Yaw + CalcTurnRate(m_time) * a_context.dt);
			_pLook->Pitch = 0.0f;
			_yaw = _pLook->Yaw;
		}

		if (auto* _pMove = _world.RefData<Component::MoveIntentComponent>(a_runner))
		{
			_pMove->value = { 0.0f, 0.0f, 1.0f };
		}

		// チャージダッシュ
		bool _isDash = false;
		if (_world.HasComponent<Component::ChargeDashComponent>(a_runner))
		{
			if (auto* _pDash = _world.RefData<Component::ChargeDashComponent>(a_runner))
			{
				if (!_pDash->isDashing)
				{
					_pDash->isDashing = true;
					_pDash->isJustDashed = true;
					_pDash->dashElapsed = 0.0f;
				}
				_pDash->dashDir = ForwardOf(_yaw);
				_pDash->isChargeIntent = false;
				_pDash->isChargeRelease = false;
				_isDash = true;
			}
		}

		// ダッシュできない機体はブーストで走らせる
		if (auto* _pBoost = _world.RefData<Component::BoostIntentComponent>(a_runner))
		{
			_pBoost->isBoostTriger = false;
			_pBoost->isBoostIntent = !_isDash;
		}

		// 燃料が切れると止まるので、満タンに戻し続ける
		auto* _pState = _world.RefData<Component::BoostStateComponent>(a_runner);
		const auto* _pParams = _world.RefData<Component::BoostParamsComponent>(a_runner);
		if (_pState && _pParams)
		{
			_pState->currentFuel = _pParams->maxFuel;
		}
	}

	float TitleRunBackdrop::CalcTurnRate(float a_time) const
	{
		return m_turnRate + m_turnSwayRate * Sway(a_time, m_turnSwayPeriod);
	}

	float TitleRunBackdrop::CalcRunSpeed(Engine::ECS::World& a_world, Engine::ECS::Entity a_runner) const
	{
		if (a_world.HasComponent<Component::ChargeDashComponent>(a_runner))
		{
			const auto* _pDash = a_world.RefData<Component::ChargeDashComponent>(a_runner);
			if (_pDash && _pDash->isDashing) return _pDash->dashSpeed;
		}
		if (const auto* _pActual = a_world.RefData<Component::ActualVelocityComponent>(a_runner))
		{
			return Math::Vector3(_pActual->value.x, 0.0f, _pActual->value.z).Length();
		}
		return 0.0f;
	}

	//======================================================================================
	// 地面を足元へ寄せる
	//--------------------------------------------------------------------------------------
	// 刻み(m_floorSnap)を模様の周期の倍数にしてあるので、寄せても模様は動いて見えない。
	// 高さと向きは置いたまま、水平の位置だけを動かす。子のタイルは親に付いて動く。
	//
	// 地面の当たり判定は静的なボディ(RegisterPhysicsBodySystem が Start で置いたまま)なので、
	// 寄せたときにここで物理空間の位置も合わせる。合わせないと足元のレイが地面に当たらなくなる。
	// ボディがまだ無い(作られる前の)フレームは、寄せたことにせず次で試す。
	//======================================================================================
	void TitleRunBackdrop::FollowFloor(Engine::GameObject::ObjectContext& a_context, Engine::ECS::Entity a_runner)
	{
		auto& _world = *a_context.pWorld;

		const Engine::ECS::Entity _floor = _world.GetEntity(m_floorGUID);
		if (!_world.IsAliveEntity(_floor)) return;

		const auto* _pRunnerTrans = _world.RefData<Component::LocalTransformComponent>(a_runner);
		auto* _pFloorTrans = _world.RefData<Component::LocalTransformComponent>(_floor);
		if (!_pRunnerTrans || !_pFloorTrans) return;

		const float _snap = (std::max)(m_floorSnap, 1.0f);

		Math::Vector3 _goal = _pFloorTrans->pos;
		_goal.x = std::round(_pRunnerTrans->pos.x / _snap) * _snap;
		_goal.z = std::round(_pRunnerTrans->pos.z / _snap) * _snap;

		if (m_isFloorPlaced && _goal.x == m_floorPos.x && _goal.z == m_floorPos.z) return;

		_pFloorTrans->pos = _goal;
		_pFloorTrans->isDirty = true;

		if (!SyncFloorBodies(_world, _floor)) return;

		m_floorPos = _goal;
		m_isFloorPlaced = true;
	}

	//--------------------------------------------------------------------------------------
	// 地面の当たり判定を合わせる
	//
	// 地面そのものと、地面を親に持つ子(タイル)が対象。行列は親を辿って組む(登録と同じ)。
	// 子は親子の番号(parentID)で見分ける。孫までは辿らない(タイルは親の直下に並べる)
	//--------------------------------------------------------------------------------------
	bool TitleRunBackdrop::SyncFloorBodies(Engine::ECS::World& a_world, Engine::ECS::Entity a_floor) const
	{
		if (!a_world.HasResource<Engine::Physics::PhysicsWorld>()) return true;
		auto& _physicsWorld = a_world.RefResource<Engine::Physics::PhysicsWorld>();

		bool _isAllSynced = true;
		auto _sync = [&](Engine::ECS::Entity a_entity, const Component::ColliderComponent& a_collider)
			{
				if (!a_collider.physicsBody.IsValid())
				{
					_isAllSynced = false;
					return;
				}
				_physicsWorld.SetBodyTransform(
					a_collider.physicsBody, a_entity, App::System::HierarchyTransform::CalcWorldMatrix(a_world, a_entity));
			};

		if (a_world.HasComponent<Component::ColliderComponent>(a_floor))
		{
			if (const auto* _pCollider = a_world.RefData<Component::ColliderComponent>(a_floor))
			{
				_sync(a_floor, *_pCollider);
			}
		}

		a_world.ForEach<const Component::ColliderComponent, const Component::HierarchyComponent>(
			[&](Engine::ECS::Chunk* a_pChunk, uint32_t a_count,
				const Component::ColliderComponent* a_colliderArray, const Component::HierarchyComponent* a_hierarchyArray)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					if (a_hierarchyArray[_i].parentID != a_floor) continue;
					_sync(a_pChunk->entityData[_i], a_colliderArray[_i]);
				}
			});

		return _isAllSynced;
	}

	//======================================================================================
	// カメラを置く
	//--------------------------------------------------------------------------------------
	// 映し方は ETitleShot の順に、それぞれの長さだけ続けて切り替える(つなぎは無いカット)。
	// 長さが 0 以下の映し方は飛ばす。
	//
	// 横と肩越しの基準は機体の進む向き(視線の Yaw)。そのまま使うと S 字の揺れでカメラまで
	// 振られるので、m_cameraFollowRate でなまして追わせる。位置は機体にぴったり付ける
	// (速いので、位置までなますと機体が画面の外へ置いていかれる)。
	//
	// 定点は切り替わった瞬間に置き場所を決め、あとは動かさずに向きだけで機体を追う。
	// 向きは m_flybyTrackRate でなますので、目の前を通り過ぎる瞬間に少し遅れて首を振る。
	//======================================================================================
	void TitleRunBackdrop::PlaceCamera(Engine::GameObject::ObjectContext& a_context, Engine::ECS::Entity a_runner)
	{
		auto& _world = *a_context.pWorld;

		const Engine::ECS::Entity _camera = _world.GetEntity(m_cameraGUID);
		if (!_world.IsAliveEntity(_camera)) return;

		auto* _pCameraTrans = _world.RefData<Component::LocalTransformComponent>(_camera);
		const auto* _pRunnerTrans = _world.RefData<Component::LocalTransformComponent>(a_runner);
		const auto* _pLook = _world.RefData<Component::LookAngleComponent>(a_runner);
		if (!_pCameraTrans || !_pRunnerTrans || !_pLook) return;

		// 進む向きをなまして追う(最初は合わせる)
		if (!m_isCameraYawReady)
		{
			m_cameraYaw = _pLook->Yaw;
			m_isCameraYawReady = true;
		}
		const float _yawDelta = WrapDegree(_pLook->Yaw - m_cameraYaw);
		m_cameraYaw = WrapDegree(m_cameraYaw + _yawDelta * DampFactor(m_cameraFollowRate, a_context.dt));

		// 映し方を切り替える(長さ 0 のものは飛ばす。全部 0 なら今のまま)
		m_shotTimer += a_context.dt;
		if (m_shotTimer >= GetShotDuration(m_shot))
		{
			ETitleShot _next = NextShot(m_shot);
			for (int _i = 0; _i < static_cast<int>(ETitleShot::Count) && GetShotDuration(_next) <= 0.0f; ++_i)
			{
				_next = NextShot(_next);
			}
			if (GetShotDuration(_next) > 0.0f)
			{
				m_shot = _next;
				m_shotTimer = 0.0f;
				m_isShotStarted = false;
			}
		}

		const Math::Vector3 _runnerPos = _pRunnerTrans->pos;
		Math::Vector3 _camPos = {};
		Math::Quaternion _camQuat = {};

		switch (m_shot)
		{
		case ETitleShot::Flyby:
		{
			const Math::Vector3 _target = _runnerPos + Math::Vector3(0.0f, m_flybyLookHeight, 0.0f);

			// 切り替わった瞬間に置き場所を決め、その時点の機体へ向ける。左右は毎回入れ替える
			if (!m_isShotStarted)
			{
				m_flybySign = -m_flybySign;
				m_flybyPos = PredictFlybyPos(_world, a_runner);
				m_flybyQuat = Math::Quaternion::LookRotation(_target - m_flybyPos, Math::Vector3::Up());
			}

			const Math::Quaternion _goalQuat = Math::Quaternion::LookRotation(_target - m_flybyPos, Math::Vector3::Up());
			m_flybyQuat = Math::Quaternion::Slerp(m_flybyQuat, _goalQuat, DampFactor(m_flybyTrackRate, a_context.dt));

			_camPos = m_flybyPos;
			_camQuat = m_flybyQuat;
			break;
		}
		case ETitleShot::Shoulder:
		case ETitleShot::Side:
		default:
		{
			Math::Vector3 _lookAt = {};
			if (m_shot == ETitleShot::Shoulder) CalcShoulderShot(_runnerPos, _camPos, _lookAt);
			else								CalcSideShot(_runnerPos, _camPos, _lookAt);

			_camQuat = Math::Quaternion::LookRotation(_lookAt - _camPos, Math::Vector3::Up());
			break;
		}
		}
		m_isShotStarted = true;

		_pCameraTrans->pos = _camPos;
		_pCameraTrans->quat = _camQuat;
		_pCameraTrans->isDirty = true;

		// ピントは機体に合わせる(被写界深度を使うカメラだけ)
		if (_world.HasComponent<Component::FocusParamComponent>(_camera))
		{
			if (auto* _pFocus = _world.RefData<Component::FocusParamComponent>(_camera))
			{
				_pFocus->focusDistance = (_runnerPos + Math::Vector3(0.0f, FOCUS_HEIGHT, 0.0f) - _camPos).Length();
			}
		}
	}

	//--------------------------------------------------------------------------------------
	// 横 : 置く向き = 後ろ * cos(角度) + 右 * sin(角度)   (0 = 真後ろ、90 = 右、180 = 正面)
	//--------------------------------------------------------------------------------------
	void TitleRunBackdrop::CalcSideShot(const Math::Vector3& a_runnerPos, Math::Vector3& a_outPos, Math::Vector3& a_outLookAt) const
	{
		const float _angle = m_sideAngle + m_sideSwayAngle * Sway(m_time, m_sideSwayPeriod);
		const float _angleRad = DirectX::XMConvertToRadians(_angle);
		const Math::Vector3 _dir = -ForwardOf(m_cameraYaw) * std::cos(_angleRad) + RightOf(m_cameraYaw) * std::sin(_angleRad);

		a_outPos = a_runnerPos + _dir * m_sideDistance + Math::Vector3(0.0f, m_sideHeight, 0.0f);
		a_outLookAt = a_runnerPos + Math::Vector3(0.0f, m_sideLookHeight, 0.0f);

		// 機体を画面の横へ寄せる : カメラから見た右(視線 -_dir を Y 軸まわりに 90 度)の逆へ見る点をずらす
		const Math::Vector3 _camRight = { -_dir.z, 0.0f, _dir.x };
		a_outLookAt -= _camRight * m_sideFrameShift;
	}

	//--------------------------------------------------------------------------------------
	// 肩越し : 後ろ・右・上へずらした位置から、進む向きの遠くを見る
	//--------------------------------------------------------------------------------------
	void TitleRunBackdrop::CalcShoulderShot(const Math::Vector3& a_runnerPos, Math::Vector3& a_outPos, Math::Vector3& a_outLookAt) const
	{
		const Math::Vector3 _forward = ForwardOf(m_cameraYaw);
		const Math::Vector3 _right = RightOf(m_cameraYaw);

		a_outPos = a_runnerPos - _forward * m_shoulderBack + _right * m_shoulderRight + Math::Vector3(0.0f, m_shoulderHeight, 0.0f);
		a_outLookAt = a_runnerPos + _forward * m_shoulderLookAhead + Math::Vector3(0.0f, m_shoulderLookHeight, 0.0f);
	}

	//--------------------------------------------------------------------------------------
	// 定点の置き場所 : 機体の m_flybyLeadTime 秒先の位置から、進路の横へ m_flybySide 離れたところ
	//
	// 先の位置は、視線の回り方(CalcTurnRate)と今の速さで進路を辿って求める。
	// 回り方は時間だけで決まるので、実際の進路とほぼ重なる。
	// 高さは置き場所の地面から測る(地面には起伏があるので、機体の高さを使うと埋まることがある)
	//--------------------------------------------------------------------------------------
	Math::Vector3 TitleRunBackdrop::PredictFlybyPos(Engine::ECS::World& a_world, Engine::ECS::Entity a_runner) const
	{
		const auto* _pRunnerTrans = a_world.RefData<Component::LocalTransformComponent>(a_runner);
		const auto* _pLook = a_world.RefData<Component::LookAngleComponent>(a_runner);
		if (!_pRunnerTrans || !_pLook) return {};

		const float _speed = CalcRunSpeed(a_world, a_runner);
		const float _leadTime = (std::max)(m_flybyLeadTime, 0.0f);
		const int _stepCount = (std::max)(1, static_cast<int>(std::ceil(_leadTime / PREDICT_STEP)));
		const float _step = _leadTime / static_cast<float>(_stepCount);

		Math::Vector3 _pos = _pRunnerTrans->pos;
		float _yaw = _pLook->Yaw;
		float _time = m_time;
		for (int _i = 0; _i < _stepCount; ++_i)
		{
			_yaw += CalcTurnRate(_time) * _step;
			_pos += ForwardOf(_yaw) * (_speed * _step);
			_time += _step;
		}

		Math::Vector3 _camPos = _pos + RightOf(_yaw) * (m_flybySide * m_flybySign);

		// 足元の地面の高さ。見つからなければ機体の高さを使う
		float _groundY = _pRunnerTrans->pos.y;
		if (a_world.HasResource<Engine::Physics::PhysicsWorld>())
		{
			Math::Ray _ray = {};
			_ray.origin = _camPos + Math::Vector3(0.0f, GROUND_PROBE_UP, 0.0f);
			_ray.direction = { 0.0f, -1.0f, 0.0f };
			_ray.maxDistance = GROUND_PROBE_LENGTH;

			Engine::Physics::RayHit _hit = {};
			if (a_world.GetResource<Engine::Physics::PhysicsWorld>().CastRay(_ray, Engine::Physics::QUERY_ALL_LAYERS, a_runner, _hit))
			{
				_groundY = _hit.position.y;
			}
		}
		_camPos.y = _groundY + m_flybyHeight;
		return _camPos;
	}

	float TitleRunBackdrop::GetShotDuration(ETitleShot a_shot) const
	{
		switch (a_shot)
		{
		case ETitleShot::Side:		return m_sideDuration;
		case ETitleShot::Shoulder:	return m_shoulderDuration;
		case ETitleShot::Flyby:		return m_flybyDuration;
		default:					return 0.0f;
		}
	}

	//======================================================================================
	// シリアライズ
	//======================================================================================
	void TitleRunBackdrop::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		a_ar.GUIDField("SpawnerGUID", m_spawnerGUID);
		a_ar.GUIDField("FloorGUID", m_floorGUID);
		a_ar.GUIDField("CameraGUID", m_cameraGUID);
		a_ar.Field("FloorSnap", m_floorSnap);

		a_ar.Field("TurnRate", m_turnRate);
		a_ar.Field("TurnSwayRate", m_turnSwayRate);
		a_ar.Field("TurnSwayPeriod", m_turnSwayPeriod);

		a_ar.Field("CameraFollowRate", m_cameraFollowRate);

		a_ar.Field("SideDuration", m_sideDuration);
		a_ar.Field("SideAngle", m_sideAngle);
		a_ar.Field("SideSwayAngle", m_sideSwayAngle);
		a_ar.Field("SideSwayPeriod", m_sideSwayPeriod);
		a_ar.Field("SideDistance", m_sideDistance);
		a_ar.Field("SideHeight", m_sideHeight);
		a_ar.Field("SideLookHeight", m_sideLookHeight);

		a_ar.Field("ShoulderDuration", m_shoulderDuration);
		a_ar.Field("ShoulderBack", m_shoulderBack);
		a_ar.Field("ShoulderRight", m_shoulderRight);
		a_ar.Field("ShoulderHeight", m_shoulderHeight);
		a_ar.Field("ShoulderLookAhead", m_shoulderLookAhead);
		a_ar.Field("ShoulderLookHeight", m_shoulderLookHeight);

		a_ar.Field("FlybyDuration", m_flybyDuration);
		a_ar.Field("FlybyLeadTime", m_flybyLeadTime);
		a_ar.Field("FlybySide", m_flybySide);
		a_ar.Field("FlybyHeight", m_flybyHeight);
		a_ar.Field("FlybyLookHeight", m_flybyLookHeight);
		a_ar.Field("FlybyTrackRate", m_flybyTrackRate);

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.Field("SideFrameShift", m_sideFrameShift);
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void TitleRunBackdrop::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;
		auto& _world = *a_context.pWorld;

		Engine::EditorField::Header("対象");
		Picker::DrawCombo<PlayerSpawner>("Spawner", a_context.pObjectManager, m_spawnerGUID);
		Engine::EditorField::Tooltip("走らせる機体を出すスポナー。Controllable を切っておく(入力で上書きされないように)");

		DrawEntityGUIDField(_world, "Floor Entity", m_floorGUID);
		Engine::EditorField::Tooltip("足元へ寄せる地面。直下の子(タイル)も一緒に動き、当たり判定も合わせる");
		if (Engine::EditorField::Field("Floor Snap", m_floorSnap, 0.5f, 1.0f, 10000.0f))
		{
			m_isFloorPlaced = false;
		}
		Engine::EditorField::Tooltip("地面を寄せる刻み(m)。タイルを並べた間隔の倍数にする(砂のタイルは 199m 間隔)");

		DrawEntityGUIDField(_world, "Camera Entity", m_cameraGUID);

		Engine::EditorField::Header("走り方");
		Engine::EditorField::Field("Turn Rate", m_turnRate, 0.1f);
		Engine::EditorField::Tooltip("回る速さ(度/秒)。円の半径は 速さ / 回る速さ。0 だと真っすぐ走り続けて原点から離れていく");
		Engine::EditorField::Field("Turn Sway Rate", m_turnSwayRate, 0.1f);
		Engine::EditorField::Field("Turn Sway Period", m_turnSwayPeriod, 0.1f, 0.0f, 120.0f);

		Engine::EditorField::Header("映し方");
		Engine::EditorField::Field("Follow Rate", m_cameraFollowRate, 0.1f, 0.0f, 60.0f);
		Engine::EditorField::Tooltip("横と肩越しで、進む向きへ追う速さ。大きいほど遅れない(0 ですぐ合わせる)");

		Engine::EditorField::Line();
		Engine::EditorField::Text("横");
		Engine::EditorField::Field("Side Duration", m_sideDuration, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Tooltip("この映し方を続ける秒数。0 で使わない");
		Engine::EditorField::Field("Side Angle", m_sideAngle, 0.5f);
		Engine::EditorField::Tooltip("0 = 真後ろ、90 = 右、180 = 正面");
		Engine::EditorField::Field("Side Sway Angle", m_sideSwayAngle, 0.5f);
		Engine::EditorField::Field("Side Sway Period", m_sideSwayPeriod, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Field("Side Distance", m_sideDistance, 0.1f, 0.5f, 100.0f);
		Engine::EditorField::Field("Side Height", m_sideHeight, 0.05f);
		Engine::EditorField::Field("Side Look Height", m_sideLookHeight, 0.05f);
		Engine::EditorField::Field("Side Frame Shift", m_sideFrameShift, 0.05f);
		Engine::EditorField::Tooltip("機体を画面の横へ寄せる量(m)。正で右へ(左に文字を置くとき)");

		Engine::EditorField::Line();
		Engine::EditorField::Text("肩越し");
		Engine::EditorField::Field("Shoulder Duration", m_shoulderDuration, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Field("Shoulder Back", m_shoulderBack, 0.05f);
		Engine::EditorField::Field("Shoulder Right", m_shoulderRight, 0.05f);
		Engine::EditorField::Field("Shoulder Height", m_shoulderHeight, 0.05f);
		Engine::EditorField::Field("Shoulder Look Ahead", m_shoulderLookAhead, 0.5f);
		Engine::EditorField::Field("Shoulder Look Height", m_shoulderLookHeight, 0.05f);

		Engine::EditorField::Line();
		Engine::EditorField::Text("定点");
		Engine::EditorField::Field("Flyby Duration", m_flybyDuration, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Field("Flyby Lead Time", m_flybyLeadTime, 0.05f, 0.0f, 30.0f);
		Engine::EditorField::Tooltip("機体が目の前を通るまでの秒数。大きいほど遠くから飛んでくる");
		Engine::EditorField::Field("Flyby Side", m_flybySide, 0.1f);
		Engine::EditorField::Tooltip("進路から横へ離す距離(m)。切り替わるたびに左右を入れ替える");
		Engine::EditorField::Field("Flyby Height", m_flybyHeight, 0.05f);
		Engine::EditorField::Field("Flyby Look Height", m_flybyLookHeight, 0.05f);
		Engine::EditorField::Field("Flyby Track Rate", m_flybyTrackRate, 0.1f, 0.0f, 60.0f);
		Engine::EditorField::Tooltip("首を振る速さ。大きいほど遅れない(0 ですぐ合わせる)");

		// 実行中の状態は表示のみ
		Engine::EditorField::Header("Runtime");
		Engine::EditorField::Value("Runner", "%llu", static_cast<unsigned long long>(FindRunner(a_context)));
		Engine::EditorField::Value("Shot", "%d (%.1f s)", static_cast<int>(m_shot), m_shotTimer);
		Engine::EditorField::Value("Floor", "%.1f, %.1f", m_floorPos.x, m_floorPos.z);
	}

	void TitleRunBackdrop::DrawEntityGUIDField(Engine::ECS::World& a_world, const char* a_label, Core::GUID& a_inoutGUID)
	{
		Engine::ECS::Entity _entity = a_world.GetEntity(a_inoutGUID);
		if (Engine::EditorField::Field(a_label, _entity))
		{
			if (a_world.IsAliveEntity(_entity) && a_world.HasComponent<Engine::ECS::GUIDComponent>(_entity))
			{
				a_inoutGUID = a_world.RefData<Engine::ECS::GUIDComponent>(_entity)->guid;
			}
		}
		Engine::EditorField::Text("%s", a_inoutGUID.String().c_str());
	}
}
