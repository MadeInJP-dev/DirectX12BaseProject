#include "SwarmBossController.h"

// エンジン
#include "Engine/ECS/System/SystemContext.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Prefab/Prefab.h"
#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"
#include "Engine/Resource/Data/EffectPrefab/EffectPrefab.h"
#include "Engine/EditorField/EditorField.h"	// コンポーネントの Traits が使うので先に置く
#include "Engine/Audio/AudioManager.h"

// App
#include "../../../ECS/World/APPWorld.h"
#include "../../../Utility/PrefabSpawnHelper.h"
#include "../../../Utility/EffectPrefabSpawnHelper.h"
#include "../../../Utility/EffectSpawnHelper.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Movement/MovementParamsComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Application/Components/Core/SpawnerComponent.h"
#include "Application/Components/Boid/SwarmBossBoidTag.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Components/Physics/SphereCollider.h"
#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Boid/BoidContactDamageComponent.h"
#include "Application/Components/Physics/CapsuleCollider.h"
#include "Application/Components/Input/PlayerControllTag.h"
#include "../../../InstanceResource/SwarmContactDamageResource.h"
#include "Engine/ECS/Component/CollisionEvent.h"
#include "Application/Components/Boid/BoidSteeringParamsComponent.h"
#include "Application/Components/Boid/BoidWaveStateComponent.h"
#include "Application/Components/Render/EmissiveOverrideComponent.h"
#include "Application/Components/Movement/LookAngleComponent.h"
#include "Application/Components/Movement/MoveIntentComponent.h"
#include "Application/Components/Boid/BoidLeaderComponent.h"
#include "Application/Components/Boid/PlatoonLeaderComponent.h"
#include "Application/Components/Boid/PlatoonSpinComponent.h"
#include "Application/Components/Boid/BoidSpownerComponent.h"
#include "Application/Components/Boid/SerchGroundComponent.h"
#include "../../../InstanceResource/WormWaveResource.h"
#include "../../../InstanceResource/WormGroundEffectResource.h"
#include "Application/Components/Boid/WarmGroundEffectComponent.h"
#include "Application/Components/Boid/SwarmMissileComponent.h"
#include "Application/Components/Boid/BoidMembershipComponent.h"
#include "Application/Components/Camera/FollowTargetComponent.h"
#include "Application/Components/Combat/DefenseRatioComponent.h"
#include "Application/Components/Boid/SwarmBurstComponent.h"
#include "Application/Components/Movement/ActualVelocityComponent.h"
#include "../../../InstanceResource/RadialBlurShakeResource.h"

namespace App::Object
{
	namespace
	{
		// 小隊長を並べる向き(リーダーの後ろ)。左手系 +Z 前方なので -Z。
		// 生成直後の LookAngle は既定(Yaw 0 = +Z 前方)なので、その後ろに並ぶ形になる
		const Math::Vector3 PLATOON_LINE_DIR = { 0.0f, 0.0f, -1.0f };

		//----------------------------------------------------------------------
		// 材料のルートにあるコンポーネントを書き換える。持っていなければ足してから渡す
		// (a_func を空にすれば「無ければ足す」だけになる)
		//----------------------------------------------------------------------
		template<typename T, typename TFunc>
		bool EditRootComponent(
			Engine::ECS::World& a_world,
			std::vector<Engine::Resource::PrefabInstanceData>& a_instanceVec,
			TFunc&& a_func)
		{
			if (a_instanceVec.empty()) return false;

			uint8_t* _pBuf = App::Utility::EnsureInstanceComponent(
				a_world, a_instanceVec[0], a_world.GetCompTypeID<T>());
			if (!_pBuf) return false;

			// バイト列の置き場はアライメントが揃っていないので、手元へ写してから触る
			T _comp = {};
			std::memcpy(&_comp, _pBuf, sizeof(T));
			a_func(_comp);
			std::memcpy(_pBuf, &_comp, sizeof(T));
			return true;
		}

		//----------------------------------------------------------------------
		// 出し切ったら終わるエフェクトか(パーツが全部、長さを持っているか)。
		// 長さ0のパーツは出しっぱなしで終わらない
		//----------------------------------------------------------------------
		bool IsOneShotEffect(const Engine::Resource::EffectAsset& a_effect)
		{
			for (const auto& _part : a_effect.GetParticleParts())
			{
				if (_part.IsValid() && _part.timing.duration <= 0.0f) return false;
			}
			for (const auto& _part : a_effect.GetMeshParts())
			{
				if (_part.IsValid() && _part.timing.duration <= 0.0f) return false;
			}
			return true;
		}

		//----------------------------------------------------------------------
		// 操作しているプレイヤーの位置(ワールド)。見つからなければ false
		//----------------------------------------------------------------------
		bool FindPlayerPos(Engine::ECS::World& a_world, Math::Vector3& a_outPos)
		{
			Engine::ECS::Entity _player = Engine::ECS::Limits::INVALID_ENTITY;
			a_world.ForEach<const Component::ActiveTag, const Component::PlayerControllTag>(
				[&](Engine::ECS::Chunk* a_pChunk, uint32_t a_count, const Component::ActiveTag*, const Component::PlayerControllTag*)
				{
					if (_player != Engine::ECS::Limits::INVALID_ENTITY || a_count == 0) return;
					_player = a_pChunk->entityData[0];
				}
			);
			if (_player == Engine::ECS::Limits::INVALID_ENTITY) return false;
			if (!a_world.HasComponent<Component::LocalTransformComponent>(_player)) return false;

			// プレイヤーは親を持たないので、ローカル座標がそのままワールド座標
			a_outPos = a_world.RefData<Component::LocalTransformComponent>(_player)->pos;
			return true;
		}

		// 無ければ既定値で足すだけ
		template<typename T>
		bool EnsureRootComponent(
			Engine::ECS::World& a_world,
			std::vector<Engine::Resource::PrefabInstanceData>& a_instanceVec)
		{
			return EditRootComponent<T>(a_world, a_instanceVec, [](T&) {});
		}

		//----------------------------------------------------------------------
		// 移動速度を流し込む(加減速は速度から決める)
		//
		// 追従できるかは速さの配分で決まるので、プレハブの値ではなく
		// こちら(群れ全体の持ち主)が決めた値を入れる。
		// 加減速が小さいと最高速に乗る前に目標が変わってしまうので、速さに比例させる
		//----------------------------------------------------------------------
		void ApplyMoveSpeed(
			Engine::ECS::World& a_world,
			std::vector<Engine::Resource::PrefabInstanceData>& a_instanceVec,
			float a_speed)
		{
			EditRootComponent<Component::MovementParamsComponent>(a_world, a_instanceVec,
				[a_speed](Component::MovementParamsComponent& a_comp)
				{
					a_comp.moveSpeed    = a_speed;
					a_comp.acceleration = a_speed * 4.0f;
					a_comp.deceleration = a_speed * 4.0f;
				}
			);
		}

		//----------------------------------------------------------------------
		// 半径 a_radius の球内に一様に散らした点(中心からの相対)
		// 同じ座標に重ねるとボイドの反発の向きが出ないのでばらまく
		//----------------------------------------------------------------------
		Math::Vector3 RandomInSphere(float a_radius)
		{
			Math::Vector3 _dir(
				Math::Random::Float(-1.0f, 1.0f),
				Math::Random::Float(-1.0f, 1.0f),
				Math::Random::Float(-1.0f, 1.0f));
			if (_dir.LengthSquared() < 1e-6f) _dir = Math::Vector3(0.0f, 0.0f, 1.0f);
			_dir.Normalize();

			// 半径は立方根で偏りを消す(そのままだと中心に寄る)
			const float _r = a_radius * std::cbrt(Math::Random::Float(0.0f, 1.0f));
			return _dir * _r;
		}

		//----------------------------------------------------------------------
		// シグネチャ変更の予約へ、コンポーネントの初期値を積む(持っている / 足すものだけ)
		//----------------------------------------------------------------------
		template<typename T>
		void SetCommandData(Engine::ECS::World& a_world, Engine::ECS::ChangeEntityCmd& a_cmd, const T& a_value)
		{
			const auto _id = a_world.GetCompTypeID<T>();
			if (!a_cmd.toSig.test(_id)) return;

			const auto* _pBytes = reinterpret_cast<const uint8_t*>(&a_value);
			a_cmd.dataMap[_id] = std::vector<uint8_t>(_pBytes, _pBytes + sizeof(T));
		}

		//----------------------------------------------------------------------
		// プレハブを読み込んで実体を返す。GUIDが未設定なら nullptr
		//
		// ここは「参照して実体化するだけ」の経路。プレハブの中身は一切書き換えない
		// (BuildSpawnInstanceData が受け取るのは BuildInstanceData が作った複製)。
		//----------------------------------------------------------------------
		const Engine::Resource::Prefab* LoadPrefab(
			Engine::GameObject::ObjectContext& a_context,
			const Core::GUID& a_guid,
			Engine::ResourceRef<Engine::Resource::Prefab>& a_inoutRef)
		{
			if (!a_guid.IsValid()) return nullptr;

			auto& _rm = *a_context.pServices->pResourceManager;

			// 生成に使うので実体ができるまで待つ。
			// 同じGUIDならキャッシュから同じハンドルが返る
			a_inoutRef = _rm.LoadImmediate<Engine::Resource::Prefab>(a_guid);

			const auto* _pPrefab = _rm.Get(a_inoutRef);
			if (!_pPrefab) return nullptr;

			// 中身が無いものは読み込みに失敗したものとして扱う。
			// 空のまま実体化すると、コンポーネントを持たないエンティティが
			// シーンに残る(そのまま保存されると余計なものが増える)
			if (_pPrefab->GetSignature().none())
			{
				ENGINE_WARNING("SwarmBossController : プレハブの中身が空です : %s", a_guid.String().c_str());
				return nullptr;
			}

			return _pPrefab;
		}
	}

	void SwarmBossController::PostDeserialize(Engine::GameObject::ObjectContext& /*a_context*/)
	{}
	void SwarmBossController::Awake(Engine::GameObject::ObjectContext& a_context)
	{
		// シーケンスのウェーブに載っていれば、呼ばれる(Appear)まで出さない
		if (m_isHoldAppear) return;

		// リーダー → 小隊長 → ボイドの順に生成
		Spawn(a_context);
	}
	void SwarmBossController::Start(Engine::GameObject::ObjectContext& /*a_context*/)
	{}
	void SwarmBossController::Release(Engine::GameObject::ObjectContext& a_context)
	{
		SequenceActor::Release(a_context);

		// サウンドインスタンスのプールはアプリ寿命なので、借りた側が必ず返す
		ReleaseRoarSound(a_context);

		// 鳴いている最中に消えても、画面の震えが残らないようにする
		m_roarWeight = 0.0f;
		if (a_context.pWorld && a_context.pWorld->HasResource<InstanceResource::RadialBlurShakeResource>())
		{
			a_context.pWorld->RefResource<InstanceResource::RadialBlurShakeResource>().Clear();
		}
	}
	void SwarmBossController::Update(Engine::GameObject::ObjectContext& a_context)
	{
		if (!m_isSpown || !a_context.pWorld) return;

		// 爆散した後はリーダーも小隊長も居ないので、行動は回さない
		if (!m_isBurst)
		{
			// 体力が一定値を切っていたら死亡へ、区切りを切っていたら小隊長の整理へ割り込む
			// (切り替わるのはこの後の行動の更新。死亡が先で、死亡中は整理しない)
			CheckDeath(a_context);
			CheckReorganize();

			// リーダーの行動(入力を作る)
			UpdateLeaderBrain(a_context);
		}

		// 体を走る発光のウェーブ(書き込むのは BoidWaveSystem)
		UpdateWave(a_context);

		// 地面の近く・地面の中で炊く砂埃(炊くのは BoidGroundEffectSystem)
		UpdateGroundEffect(a_context);

		// リーダーが潜った / 出た瞬間の大きな砂埃
		UpdateBurrowEffect(a_context);

		// 体当たりのダメージ(触れたかを見るのは BoidContactDamageSystem)
		UpdateContactDamage(a_context);

		// 咆哮の画面の震え(震わせるのは RadialBlurShakeSystem)。爆散した後は行動が回らないので止める
		if (m_isBurst) m_roarWeight = 0.0f;
		UpdateRoarBlur(a_context);

		// 残りの生存数(HP代わり)。印を数え直すだけ
		m_currentBoids = CountAliveBoids(a_context);
		if (m_currentBoids > 0) m_isBodyCounted = true;
	}

	//======================================================================================
	// リーダーの行動 : ステートマシンを回す
	//--------------------------------------------------------------------------------------
	// このクラスはプレイヤーのキーボード/マウスと同じ立場で、作るのは移動入力だけ。
	// 何をするかは各ステート(SwarmBossStates)。切り替え要求は次のフレームの PreUpdate で反映
	//======================================================================================
	void SwarmBossController::UpdateLeaderBrain(Engine::GameObject::ObjectContext& a_context)
	{
		SwarmBossStateContext _stateContext = {};
		_stateContext.pObject      = &a_context;
		_stateContext.pMachine     = &m_stateMachine;
		_stateContext.leaderEntity = m_leaderEntity;
		_stateContext.spawnPos     = m_spawnPos;
		_stateContext.pPlatoonLeaders = &m_platoonLeaderEntities;
		_stateContext.wormLength      = GetWormLength();

		m_stateMachine.PreUpdate(_stateContext);
		m_stateMachine.Update(_stateContext);
		m_stateMachine.PostUpdate(_stateContext);

		//----------------------------------------------------------------------
		// 行動からの依頼(体を作り変えるのはこのクラス)
		//----------------------------------------------------------------------
		if (_stateContext.isRequestReorganize)
		{
			ReorganizePlatoons(a_context);
		}

		// 防御比率は依頼が変わったときだけ書き換える(依頼しない行動は既定の 1)
		if (_stateContext.bodyDefenseRatio != m_bodyDefenseRatio)
		{
			ApplyBodyDefense(a_context, _stateContext.bodyDefenseRatio);
		}

		// ウェーブの倍率(走らせるのは UpdateWave)
		m_waveSpeedScale = std::max(_stateContext.waveSpeedScale, 1e-3f);

		// 爆散(死亡の最後)。防御比率の書き換えより後に行う
		if (_stateContext.burst.isRequested)
		{
			BurstBody(a_context, _stateContext.burst);
		}

		// 咆哮 : 鳴き声は頼まれた瞬間に鳴らし、画面の震えは強さを覚えておく(書くのは UpdateRoarBlur)
		if (_stateContext.isRequestRoar)
		{
			PlayRoar(a_context);
		}
		m_roarWeight = std::clamp(_stateContext.roarWeight, 0.0f, 1.0f);

		// 胴回りの回転(頼まない行動は既定の 0 = 緩めて畳む)
		UpdateBodySpin(a_context, _stateContext.bodySpinRate);
	}

	//======================================================================================
	// 胴回りの回転
	//--------------------------------------------------------------------------------------
	// 速さは目標(最高速 × 頼まれた強さ)へ加速度で寄せる。上げるときは m_spinAcceleration、
	// 下げるときは m_spinDeceleration。上げるのが遅いほど「だんだん速くなる」感じが出る。
	// 輪の広がりは、頼まれていれば 1 へ、頼まれていなければ回転が止まるにつれて 0 へ寄せる
	// (回ったまま急に輪が消えないように)。
	//
	// 小隊長への書き込みは 100 体ぶんだけなので毎フレーム行う。止まりきったら 0 を一度書いて以降は書かない
	//======================================================================================
	void SwarmBossController::UpdateBodySpin(Engine::GameObject::ObjectContext& a_context, float a_requestRate)
	{
		const float _dt = a_context.dt;
		m_spinRequestRate = std::clamp(a_requestRate, 0.0f, 1.0f);

		//----------------------------------------------------------------------
		// 速さ : 目標へ加速度で寄せる
		//----------------------------------------------------------------------
		const float _goalSpeed = std::max(m_spinMaxSpeed, 0.0f) * m_spinRequestRate;
		if (m_spinSpeed < _goalSpeed)
		{
			m_spinSpeed = std::min(m_spinSpeed + std::max(m_spinAcceleration, 0.0f) * _dt, _goalSpeed);
		}
		else
		{
			m_spinSpeed = std::max(m_spinSpeed - std::max(m_spinDeceleration, 0.0f) * _dt, _goalSpeed);
		}

		m_spinAngle += m_spinSpeed * _dt;
		m_spinAngle  = std::remainder(m_spinAngle, DirectX::XM_2PI);	// 長く回っても精度が落ちないように畳む

		//----------------------------------------------------------------------
		// 輪の広がり : 頼まれていれば広げ、やめたら回転が落ちるのに合わせて畳む
		//----------------------------------------------------------------------
		float _goalRatio = (m_spinRequestRate > 0.0f) ? 1.0f : 0.0f;
		if (_goalRatio <= 0.0f && m_spinMaxSpeed > 0.0f)
		{
			// 緩めている最中は、速さの割合までしか畳まない
			_goalRatio = std::clamp(m_spinSpeed / m_spinMaxSpeed, 0.0f, 1.0f);
		}
		const float _step = std::max(m_spinRadiusRate, 0.0f) * _dt;
		m_spinRadiusRatio += std::clamp(_goalRatio - m_spinRadiusRatio, -_step, _step);

		//----------------------------------------------------------------------
		// 小隊長へ命令を書く
		//----------------------------------------------------------------------
		const bool _isSpinning = (m_spinRadiusRatio > 0.0f);
		if (!_isSpinning && !m_isSpinWritten) return;
		m_isSpinWritten = _isSpinning;

		if (!a_context.pWorld) return;
		auto& _world = *a_context.pWorld;

		const float _radius = std::max(m_spinRadius, 0.0f) * m_spinRadiusRatio;
		for (size_t _i = 0; _i < m_platoonLeaderEntities.size(); ++_i)
		{
			const Engine::ECS::Entity _platoon = m_platoonLeaderEntities[_i];
			if (!_world.IsAliveEntity(_platoon)) continue;
			if (!_world.HasComponent<Component::PlatoonSpinComponent>(_platoon)) continue;

			auto* _pSpin = _world.RefData<Component::PlatoonSpinComponent>(_platoon);
			_pSpin->angle  = m_spinAngle + m_spinTwist * static_cast<float>(_i);
			_pSpin->radius = _radius;
		}
	}

	//======================================================================================
	// 咆哮 : 鳴き声
	//--------------------------------------------------------------------------------------
	// 初めて鳴くときに借りて、以降は同じものを頭から鳴らし直す。返すのは Release
	//======================================================================================
	void SwarmBossController::PlayRoar(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_roarSoundGUID == Core::DEFAULT_GUID) return;
		if (!a_context.pServices || !a_context.pServices->pAudioManager) return;
		auto& _audio = *a_context.pServices->pAudioManager;

		// 画面に出す音なので 2D で借りる(定位を付けない)
		if (!_audio.RefInstance(m_roarSoundHandle))
		{
			m_roarSoundHandle = _audio.CreateSoundInstance(m_roarSoundGUID, false);
		}

		if (auto* _pInstance = _audio.RefInstance(m_roarSoundHandle))
		{
			_pInstance->Stop();
			_pInstance->SetVolume(m_roarVolume);
			_pInstance->Play(false);
		}
	}

	void SwarmBossController::ReleaseRoarSound(Engine::GameObject::ObjectContext& a_context)
	{
		if (a_context.pServices && a_context.pServices->pAudioManager)
		{
			a_context.pServices->pAudioManager->ReleaseSoundInstance(m_roarSoundHandle);
		}
		m_roarSoundHandle = {};
	}

	//======================================================================================
	// 咆哮 : 画面の震え
	//--------------------------------------------------------------------------------------
	// 頼まれていない間も 0 を書く(書きっぱなしにすると震えが残る)
	//======================================================================================
	void SwarmBossController::UpdateRoarBlur(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;
		if (!a_context.pWorld->HasResource<InstanceResource::RadialBlurShakeResource>()) return;

		auto& _res = a_context.pWorld->RefResource<InstanceResource::RadialBlurShakeResource>();
		_res.weight         = m_roarWeight;
		_res.strength       = m_roarBlurStrength;
		_res.strengthJitter = m_roarBlurStrengthJitter;
		_res.centerJitter   = m_roarBlurCenterJitter;
		_res.frequency      = m_roarBlurFrequency;
	}

	//======================================================================================
	// 死亡 : 体力が一定値を切ったか
	//--------------------------------------------------------------------------------------
	// 死亡の行動は徘徊へ戻らない。小隊長の整理の最中でも割り込む。
	// 死亡中かどうかは「今のステートが死亡か」で見る。デバッグで死亡へ入れたときも
	// 死亡中として扱い、デバッグで抜けたときは体力が足りていれば元に戻る
	//======================================================================================
	void SwarmBossController::CheckDeath(Engine::GameObject::ObjectContext& a_context)
	{
		bool _isDying = (m_stateMachine.GetCurrentState() == ESwarmBossState::Death);

		// 体力が一定値を切ったら死亡へ(まとめる体が無ければ入らない)
		if (!_isDying && m_deathHp > 0 && m_currentBoids > 0 && m_currentBoids <= m_deathHp)
		{
			m_stateMachine.ReserveChangeState(ESwarmBossState::Death);
			_isDying = true;
		}

		// 爆散までの間に読み込みを済ませておく
		if (_isDying && !m_isDying) RequestLoadBurstEffect(a_context);

		m_isDying = _isDying;
	}

	void SwarmBossController::RequestLoadBurstEffect(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_burstEffectRef || m_burstEffectGUID == Core::DEFAULT_GUID) return;
		if (!a_context.pServices || !a_context.pServices->pResourceManager) return;

		m_burstEffectRef = a_context.pServices->pResourceManager->RequestLoad<Engine::Resource::EffectAsset>(m_burstEffectGUID);
	}

	//======================================================================================
	// 死亡 : 体を爆散させる
	//--------------------------------------------------------------------------------------
	// 体のボイド(群れの部品を持つもの。飛んでいるミサイルは対象外)から群れの部品を外し、
	// 中心から外向き(少し上寄り)の速度を持つ SwarmBurstComponent を付ける。
	// 付け外しは1件の変更にまとめて予約する(反映は次の BeginFrame)。
	//   ・加減速を 0 にし、実速度にも初速を入れておく(爆発なので一気に飛び出させる)
	//   ・発光は爆散の色へ、防御比率は 1 へ(落とすのは自分へのダメージなので、0 だと落ちられない)
	// リーダーと小隊長は消す。どちらもモデルを持たないので見た目は変わらない。
	//======================================================================================
	void SwarmBossController::BurstBody(Engine::GameObject::ObjectContext& a_context, const SwarmBossBurstRequest& a_request)
	{
		auto& _world = *a_context.pWorld;

		struct BoidEntry
		{
			Engine::ECS::Entity entity = Engine::ECS::Limits::INVALID_ENTITY;
			Math::Vector3 pos = {};
		};
		std::vector<BoidEntry> _boids = {};
		_boids.reserve(m_currentBoids);

		const Core::GUID _self = m_guid;
		_world.ForEach<const Component::SwarmBossBoidTag, const Component::SpawnerComponent, const Component::LocalTransformComponent, const Component::BoidSteeringParamsComponent>(
			[&](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Component::SwarmBossBoidTag*,
				const Component::SpawnerComponent* a_spawnerArray,
				const Component::LocalTransformComponent* a_trsArray,
				const Component::BoidSteeringParamsComponent*)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					if (a_spawnerArray[_i].spawnerGUID != _self) continue;
					_boids.push_back({ a_pChunk->entityData[_i], a_trsArray[_i].pos });
				}
			}
		);

		const Engine::ECS::ComponentTypeID _removeIDs[] =
		{
			_world.GetCompTypeID<Component::BoidSteeringParamsComponent>(),
			_world.GetCompTypeID<Component::FollowTargetComponent>(),
			_world.GetCompTypeID<Component::BoidWaveStateComponent>(),
			_world.GetCompTypeID<Component::BoidContactDamageComponent>(),
		};
		const auto _burstID = _world.GetCompTypeID<Component::SwarmBurstComponent>();

		const float _speedMin = std::min(a_request.speedMin, a_request.speedMax);
		const float _speedMax = std::max(a_request.speedMin, a_request.speedMax);
		const float _lifeMin  = std::min(a_request.lifeMin, a_request.lifeMax);
		const float _lifeMax  = std::max(a_request.lifeMin, a_request.lifeMax);

		for (const BoidEntry& _entry : _boids)
		{
			// 中心から外へ、少し上寄りに。中心ぴったりなら適当な向きへ
			Math::Vector3 _dir = _entry.pos - a_request.center;
			if (_dir.LengthSquared() <= 1e-6f) _dir = RandomInSphere(1.0f);
			if (_dir.LengthSquared() <= 1e-6f) _dir = Math::Vector3::Up();
			_dir.Normalize();
			_dir += Math::Vector3::Up() * std::max(a_request.upBias, 0.0f);
			_dir.Normalize();

			Component::SwarmBurstComponent _burst = {};
			_burst.velocity = _dir * Math::Random::Float(_speedMin, _speedMax);
			_burst.timer    = Math::Random::Float(_lifeMin, _lifeMax);
			_burst.gravity  = a_request.gravity;
			_burst.drag     = std::max(a_request.drag, 0.0f);

			Engine::ECS::ChangeEntityCmd _cmd = {};
			_cmd.entity = _entry.entity;
			_cmd.toSig  = _world.GetSignature(_entry.entity);
			for (const auto _id : _removeIDs) _cmd.toSig.reset(_id);
			_cmd.toSig.set(_burstID);

			SetCommandData(_world, _cmd, _burst);

			Component::EmissiveOverrideComponent _emissive = {};
			_emissive.emissiveColor     = a_request.color;
			_emissive.emissiveIntensity = a_request.intensity;
			_emissive.isOverride        = true;
			SetCommandData(_world, _cmd, _emissive);

			Component::DefenseRatioComponent _defense = {};
			_defense.ratio = 1.0f;
			SetCommandData(_world, _cmd, _defense);

			Component::ActualVelocityComponent _actual = {};
			_actual.value = _burst.velocity;
			SetCommandData(_world, _cmd, _actual);

			if (_world.HasComponent<Component::MovementParamsComponent>(_entry.entity))
			{
				Component::MovementParamsComponent _move = *_world.RefData<Component::MovementParamsComponent>(_entry.entity);
				_move.acceleration = 0.0f;
				_move.deceleration = 0.0f;
				SetCommandData(_world, _cmd, _move);
			}

			_world.ReserveChangeSignature(std::move(_cmd));
		}

		//----------------------------------------------------------------------
		// 骨組み(リーダー・小隊長)を消す。消した後は ID が使い回されるので握っている値も捨てる
		//----------------------------------------------------------------------
		for (const Engine::ECS::Entity _platoon : m_platoonLeaderEntities)
		{
			if (_world.IsAliveEntity(_platoon)) _world.ReserveReleaseEntity(_platoon);
		}
		m_platoonLeaderEntities.clear();

		if (_world.IsAliveEntity(m_leaderEntity)) _world.ReserveReleaseEntity(m_leaderEntity);
		m_leaderEntity = Engine::ECS::Limits::INVALID_ENTITY;

		//----------------------------------------------------------------------
		// 中心に大きな爆発(出し切ったら自分から消える)
		//----------------------------------------------------------------------
		if (m_burstEffectGUID != Core::DEFAULT_GUID && m_burstEffectScale > 0.0f)
		{
			App::Utility::ReserveSpawnEffectAt(_world, m_burstEffectGUID, a_request.center, true, {}, m_burstEffectScale);
		}

		m_isBurst = true;
	}

	//======================================================================================
	// 小隊長の整理 : 体力が区切りを切ったか
	//--------------------------------------------------------------------------------------
	// 区切りは「最大体力から m_reorganizeHpInterval 減るごと」。一度に複数の区切りを
	// 越えていても整理は1回(越えた数はまとめて進める)。
	// 整理の最中は割り込まない。その間に落ちたぶん(飛んでいるミサイルなど)は、
	// 整理が終わった後の判定で拾う
	//======================================================================================
	void SwarmBossController::CheckReorganize()
	{
		if (m_reorganizeHpInterval == 0 || m_maxBoid == 0) return;
		if (m_currentBoids == 0) return;	// 倒された
		if (m_isDying) return;				// 死亡の行動からは抜けない
		if (m_stateMachine.GetCurrentState() == ESwarmBossState::Reorganize) return;

		const uint32_t _lost  = m_maxBoid - std::min(m_currentBoids, m_maxBoid);
		const uint32_t _steps = _lost / m_reorganizeHpInterval;
		if (_steps <= m_reorganizeCount) return;

		m_reorganizeCount = _steps;
		m_stateMachine.ReserveChangeState(ESwarmBossState::Reorganize);
	}

	//======================================================================================
	// 小隊長の整理 : 小隊長を減らして、ボイドを割り当て直す
	//--------------------------------------------------------------------------------------
	// 残す数 = 最大数 × 今の体力 / 最大体力(切り上げ。1体以上、今の数以下)。
	// 減らすのは尾の側から。一つ前の相手(preLeader)の繋がりが切れないようにするため。
	//
	// ボイドは「元の小隊長の並び順」で並べてから、残った小隊長へ頭から均等に配る。
	// 頭の方に居たボイドは頭の方の小隊長へ付くので、体の中を大きく横切らずに済む。
	// 減らした小隊長に付いていたボイドは尾の小隊長へ寄る。
	// 切り離したミサイル(群れの部品を外したもの)は対象にしない。
	//
	// 1回きりの処理なので、ここで全ボイドを走査する(毎フレームはしない)
	//======================================================================================
	void SwarmBossController::ReorganizePlatoons(Engine::GameObject::ObjectContext& a_context)
	{
		auto& _world = *a_context.pWorld;

		// 生きている小隊長だけを、頭からの並びのまま残す
		std::vector<Engine::ECS::Entity> _platoons = {};
		_platoons.reserve(m_platoonLeaderEntities.size());
		for (const Engine::ECS::Entity _platoon : m_platoonLeaderEntities)
		{
			if (_world.IsAliveEntity(_platoon)) _platoons.push_back(_platoon);
		}
		if (_platoons.empty() || m_maxBoid == 0) return;

		// 元の並び順(頭から何番目か)。ボイドを並べるのに使う
		std::unordered_map<Engine::ECS::Entity, size_t> _oldOrder = {};
		for (size_t _i = 0; _i < _platoons.size(); ++_i) _oldOrder.emplace(_platoons[_i], _i);

		//----------------------------------------------------------------------
		// 残す数を決めて、尾から減らす
		//----------------------------------------------------------------------
		const uint32_t _alive = CountAliveBoids(a_context);
		const double _rate = static_cast<double>(_alive) / static_cast<double>(m_maxBoid);
		size_t _keep = static_cast<size_t>(std::ceil(static_cast<double>(m_maxPlatoonLeader) * _rate));
		_keep = std::clamp<size_t>(_keep, 1, _platoons.size());

		for (size_t _i = _keep; _i < _platoons.size(); ++_i)
		{
			_world.ReserveReleaseEntity(_platoons[_i]);
		}
		_platoons.resize(_keep);
		m_platoonLeaderEntities = _platoons;

		// 体の長さ = 最後尾になった小隊長の位置(ウェーブを捨てる位置に使う)
		m_tailAlongWorm = 0.0f;
		if (_world.HasComponent<Component::PlatoonLeaderComponent>(_platoons.back()))
		{
			m_tailAlongWorm = _world.RefData<Component::PlatoonLeaderComponent>(_platoons.back())->distanceAlongWorm;
		}

		//----------------------------------------------------------------------
		// ボイドを元の並び順で集める(群れの部品を持つもの = ミサイルでないもの)
		//----------------------------------------------------------------------
		struct BoidEntry
		{
			Engine::ECS::Entity entity = Engine::ECS::Limits::INVALID_ENTITY;
			size_t order = 0;	// 元の小隊長の並び順(分からなければ最後)
		};
		std::vector<BoidEntry> _boids = {};
		_boids.reserve(m_currentBoids);

		const Core::GUID _self = m_guid;
		_world.ForEach<const Component::SwarmBossBoidTag, const Component::SpawnerComponent, const Component::BoidMembershipComponent, const Component::BoidSteeringParamsComponent>(
			[&](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Component::SwarmBossBoidTag*,
				const Component::SpawnerComponent* a_spawnerArray,
				const Component::BoidMembershipComponent* a_memberArray,
				const Component::BoidSteeringParamsComponent*)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					if (a_spawnerArray[_i].spawnerGUID != _self) continue;

					const auto _it = _oldOrder.find(a_memberArray[_i].platoonID);
					const size_t _order = (_it != _oldOrder.end()) ? _it->second : _oldOrder.size();
					_boids.push_back({ a_pChunk->entityData[_i], _order });
				}
			}
		);
		if (_boids.empty()) return;

		std::stable_sort(_boids.begin(), _boids.end(),
			[](const BoidEntry& a_l, const BoidEntry& a_r) { return a_l.order < a_r.order; });

		//----------------------------------------------------------------------
		// 残った小隊長へ頭から均等に配る(走査が終わってから書く)
		//----------------------------------------------------------------------
		for (size_t _i = 0; _i < _boids.size(); ++_i)
		{
			const size_t _index = _i * _platoons.size() / _boids.size();
			const Engine::ECS::Entity _boid = _boids[_i].entity;

			_world.RefData<Component::BoidMembershipComponent>(_boid)->platoonID = _platoons[_index];

			if (_world.HasComponent<Component::FollowTargetComponent>(_boid))
			{
				auto* _pFollow = _world.RefData<Component::FollowTargetComponent>(_boid);
				_pFollow->target = _platoons[_index];
			}
			if (_world.HasComponent<Component::SpawnerComponent>(_boid))
			{
				_world.RefData<Component::SpawnerComponent>(_boid)->waveIndex = static_cast<int>(_index);
			}
		}
	}

	//======================================================================================
	// 体(ボイド)の防御比率を書き換える
	//--------------------------------------------------------------------------------------
	// 切り離したミサイルは 1 のまま(0 にすると自爆のダメージも 0 になって落ちられない)
	//======================================================================================
	void SwarmBossController::ApplyBodyDefense(Engine::GameObject::ObjectContext& a_context, float a_ratio)
	{
		auto& _world = *a_context.pWorld;
		const Core::GUID _self = m_guid;

		_world.ForEach<const Component::SwarmBossBoidTag, const Component::SpawnerComponent, Component::DefenseRatioComponent>(
			[&](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Component::SwarmBossBoidTag*,
				const Component::SpawnerComponent* a_spawnerArray,
				Component::DefenseRatioComponent* a_defenseArray)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					if (a_spawnerArray[_i].spawnerGUID != _self) continue;

					const bool _isMissile = _world.HasComponent<Component::SwarmMissileComponent>(a_pChunk->entityData[_i]);
					a_defenseArray[_i].ratio = _isMissile ? 1.0f : a_ratio;
				}
			}
		);

		m_bodyDefenseRatio = a_ratio;
	}

	//======================================================================================
	// 体を走る発光のウェーブ
	//--------------------------------------------------------------------------------------
	// 周期が来たら頭(0m)から新しく出し、毎フレーム尾へ進める。
	// 尾を抜けた(帯の幅ぶん行き過ぎた)ものは捨てる。
	//
	// ここが書くのは「どこを光らせるか」だけで、実際に発光を書き換えるのは
	// BoidWaveSystem。受け渡しは WormWaveResource 経由
	//======================================================================================
	void SwarmBossController::UpdateWave(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;

		const float _dt = a_context.dt;

		// 尾を抜けてから捨てるまでの余裕。
		// 尾ちょうどで消すと、最後の小隊のボイドが光り終わる前に消えてしまう
		const float _endPos = GetWormLength() + m_waveWidth;

		//----------------------------------------------------------------------
		// 走っているものを進める(抜けたものは落とす)
		//
		// 速さは1本ずつが持つ。出すときに今の設定値を写しているので、
		// 走っている最中に速さを変えても、その帯は出たときの速さのまま流れる
		//----------------------------------------------------------------------
		size_t _alive = 0;
		for (Component::SwarmBossWave& _wave : m_waveVec)
		{
			_wave.position += _wave.speed * _dt;
			if (_wave.position > _endPos) continue;

			m_waveVec[_alive] = _wave;
			++_alive;
		}
		m_waveVec.resize(_alive);

		//----------------------------------------------------------------------
		// 周期が来たら頭から新しく出す
		//----------------------------------------------------------------------
		m_waveTimer -= _dt;
		if (m_waveTimer <= 0.0f)
		{
			// 周期が0以下だと毎フレーム出て帯が繋がってしまうので、下限を入れる。
			// 行動から倍率を頼まれていれば、そのぶん間隔を縮める(死亡でどんどん速まる)
			m_waveTimer = std::max(m_waveInterval / m_waveSpeedScale, 0.01f);

			// 上限は超えない。一番古いものから捨てるので、詰まっても新しい帯は必ず出る
			if (m_maxWave > 0)
			{
				if (m_waveVec.size() >= m_maxWave)
				{
					m_waveVec.erase(m_waveVec.begin());
				}

				Component::SwarmBossWave _new = {};
				_new.position = 0.0f;			// 頭から
				_new.speed    = m_waveSpeed * m_waveSpeedScale;
				m_waveVec.push_back(_new);
			}
		}

		//----------------------------------------------------------------------
		// ECS側へ書き写す
		//----------------------------------------------------------------------
		auto& _waveRes = a_context.pWorld->RefResource<InstanceResource::WormWaveResource>();

		_waveRes.waves         = m_waveVec;
		_waveRes.width         = m_waveWidth;
		_waveRes.baseIntensity = m_waveBaseIntensity;
		_waveRes.peakIntensity = m_wavePeakIntensity;
		_waveRes.baseColor     = m_waveBaseColor;
		_waveRes.peakColor     = m_wavePeakColor;
		_waveRes.isActive      = true;
	}

	//======================================================================================
	// 頭から尾までの長さ(1次元)
	//--------------------------------------------------------------------------------------
	// 最後尾の小隊長の位置は生成時に決まる(CreatePlatoonLeaders で足し上げた値)。
	// 小隊長が1体も居なければリーダーだけなので0
	//======================================================================================
	float SwarmBossController::GetWormLength() const
	{
		return m_tailAlongWorm;
	}

	//======================================================================================
	// 体当たりのダメージ : 調整値とプレイヤーのカプセルを ECS 側へ書く
	//--------------------------------------------------------------------------------------
	// プレイヤーは1体(操作している機体)。カプセルを持っていなければ、
	// 位置だけの点(半径0)として扱う。見つからなければ何もさせない
	//======================================================================================
	void SwarmBossController::UpdateContactDamage(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;
		auto& _world = *a_context.pWorld;
		if (!_world.HasResource<InstanceResource::SwarmContactDamageResource>()) return;

		auto& _res = _world.RefResource<InstanceResource::SwarmContactDamageResource>();
		_res.damage     = m_contactDamage;
		_res.cooldown   = m_contactDamageCooldown;
		_res.boidRadius = m_boidColliderRadius;
		_res.Clear();

		// 操作しているプレイヤー
		Engine::ECS::Entity _player = Engine::ECS::Limits::INVALID_ENTITY;
		_world.ForEach<const Component::ActiveTag, const Component::PlayerControllTag>(
			[&](Engine::ECS::Chunk* a_pChunk, uint32_t a_count, const Component::ActiveTag*, const Component::PlayerControllTag*)
			{
				if (_player != Engine::ECS::Limits::INVALID_ENTITY || a_count == 0) return;
				_player = a_pChunk->entityData[0];
			}
		);
		if (_player == Engine::ECS::Limits::INVALID_ENTITY) return;
		if (!_world.HasComponent<Component::LocalTransformComponent>(_player)) return;

		// カプセルは縦の線分 + 半径(CapsuleCollisionSystem と同じ組み方)。
		// プレイヤーは親を持たないので、ローカル座標がそのままワールド座標
		Math::Vector3 _center = _world.RefData<Component::LocalTransformComponent>(_player)->pos;
		Math::Vector3 _half   = {};
		float _radius = 0.0f;
		if (_world.HasComponent<Component::CapsuleColliderComponent>(_player))
		{
			const auto* _pCapsule = _world.RefData<Component::CapsuleColliderComponent>(_player);
			_center += Math::Vector3(_pCapsule->offset);
			_half    = Math::Vector3(0.0f, _pCapsule->height * 0.5f, 0.0f);
			_radius  = _pCapsule->radius;
		}

		_res.player         = _player;
		_res.playerSegmentA = _center - _half;
		_res.playerSegmentB = _center + _half;
		_res.playerRadius   = _radius;
		_res.isActive       = true;
	}

	//======================================================================================
	// 潜る / 出るときの大きな砂埃
	//--------------------------------------------------------------------------------------
	// リーダーの地面との関係(SerchGroundSystem が書く)が切り替わった瞬間に、
	// リーダーの真上(真下)の地表へエフェクトプレハブを炊く。
	// 地面が見つからないフレームは切り替わりとして扱わない(レイの届かない高さに居るだけ)
	//======================================================================================
	void SwarmBossController::UpdateBurrowEffect(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld || !a_context.pServices || !a_context.pServices->pResourceManager) return;
		auto& _world = *a_context.pWorld;
		auto& _rm = *a_context.pServices->pResourceManager;

		m_burrowEffectTimer -= a_context.dt;

		if (!_world.IsAliveEntity(m_leaderEntity)) return;
		if (!_world.HasComponent<Component::SerchGroundComponent>(m_leaderEntity)) return;
		if (!_world.HasComponent<Component::LocalTransformComponent>(m_leaderEntity)) return;

		const Component::SerchGroundComponent _ground = *_world.RefData<Component::SerchGroundComponent>(m_leaderEntity);
		if (!_ground.isFoundGround) return;

		const bool _isUnderGround = _ground.isUnderGround != 0;

		// 最初に見たときは覚えるだけ(出た瞬間に炊かない)
		if (!m_isLeaderGroundKnown)
		{
			m_isLeaderGroundKnown  = true;
			m_wasLeaderUnderGround = _isUnderGround;
			return;
		}

		if (_isUnderGround == m_wasLeaderUnderGround) return;
		m_wasLeaderUnderGround = _isUnderGround;

		// 地表すれすれを泳いでいると切り替わりが続くので、間を空ける
		if (m_burrowEffectTimer > 0.0f) return;
		if (m_burrowEffectGUID == Core::DEFAULT_GUID) return;

		// 初めて炊くときに読み込む(以降は握ったまま)。
		// 中身を読むのにワールドのコンポーネント情報が要るので、同期で読む
		if (!m_burrowEffectRef)
		{
			m_burrowEffectRef = _rm.LoadImmediate<Engine::Resource::EffectPrefab>(m_burrowEffectGUID);
		}

		const Math::Vector3 _leaderPos = _world.RefData<Component::LocalTransformComponent>(m_leaderEntity)->pos;
		const Math::Vector3 _pos(_leaderPos.x, _ground.groundHeight, _leaderPos.z);

		if (App::Utility::SpawnEffectPrefab(_world, _rm, m_burrowEffectRef, _pos))
		{
			m_burrowEffectTimer = m_burrowEffectCooldown;
		}
	}

	//======================================================================================
	// 砂埃 : 調整値を ECS 側へ書き写す
	//--------------------------------------------------------------------------------------
	// 4000体ぶんのレイとエフェクトの生成は BoidGroundEffectSystem。
	// 1フレームに出した数はここで毎フレーム0に戻す(上限を数え直す)
	//======================================================================================
	void SwarmBossController::UpdateGroundEffect(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;
		if (!a_context.pWorld->HasResource<InstanceResource::WormGroundEffectResource>()) return;

		auto& _res = a_context.pWorld->RefResource<InstanceResource::WormGroundEffectResource>();

		_res.effectGUID       = m_groundEffectGUID;
		_res.maxHeight        = m_groundEffectMaxHeight;
		_res.maxDepth         = m_groundEffectMaxDepth;
		_res.nearScale        = m_groundEffectNearScale;
		_res.farScale         = m_groundEffectFarScale;
		_res.underScale       = m_groundEffectUnderScale;
		_res.interval         = m_groundEffectInterval;
		_res.maxSpawnPerFrame = m_groundEffectMaxSpawnPerFrame;
		_res.spawnedThisFrame = 0;

		// 炊けるのは、読み込みが済んでいて、出し切って消える単発のものだけ。
		// 出しっぱなしのパーツがあると destroyOnFinish で消えず、毎秒数百体ずつ溜まっていく
		m_isGroundEffectOneShot = false;

		// 既定値のまま置いた直後など、まだ読み込みを始めていなければここで始める
		if (!m_groundEffectRef && m_groundEffectGUID != Core::DEFAULT_GUID &&
			a_context.pServices && a_context.pServices->pResourceManager)
		{
			m_groundEffectRef =
				a_context.pServices->pResourceManager->RequestLoad<Engine::Resource::EffectAsset>(m_groundEffectGUID);
		}

		if (m_groundEffectRef && a_context.pServices && a_context.pServices->pResourceManager)
		{
			if (const auto* _pEffect = a_context.pServices->pResourceManager->Get(m_groundEffectRef))
			{
				m_isGroundEffectOneShot = IsOneShotEffect(*_pEffect);
			}
		}
		_res.isActive = m_isGroundEffectOneShot;
	}

	//======================================================================================
	// 生成
	//======================================================================================
	bool SwarmBossController::Spawn(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isSpown) return true;
		if (!a_context.pWorld || !a_context.pServices || !a_context.pServices->pResourceManager) return false;

		// 体を向ける Yaw。地面から出すときはプレイヤーの方へ向ける
		// (列は向きの真後ろへ埋めるので、飛び出した柱がプレイヤーの側へ少し傾く)
		m_spawnYaw = 0.0f;
		Math::Vector3 _playerPos = {};
		if (m_isEmergeOnSpawn && FindPlayerPos(*a_context.pWorld, _playerPos))
		{
			float _pitch = 0.0f;
			Component::MakeLookAngleFromDir(Math::Vector3(_playerPos.x - m_spawnPos.x, 0.0f, _playerPos.z - m_spawnPos.z), m_spawnYaw, _pitch);
		}

		// リーダーが居なければ何も出さない。
		// 置いた直後(プレハブ未設定)もここを通るので、止めずに警告だけ出す
		if (!CreateLeader(a_context)) return false;

		// リーダーが出た時点で「出し済み」にする。
		// 小隊長やボイドで失敗しても、出したリーダーの上に二重に出さないため
		m_isSpown = true;

		// 体力の区切りは満タンから数え直す
		m_reorganizeCount  = 0;
		m_bodyDefenseRatio = 1.0f;
		m_isDying          = false;
		m_isBurst          = false;
		m_isBodyCounted    = false;
		m_waveSpeedScale   = 1.0f;
		m_roarWeight       = 0.0f;
		m_spinSpeed        = 0.0f;
		m_spinAngle        = 0.0f;
		m_spinRadiusRatio  = 0.0f;
		m_isSpinWritten    = false;

		// 地面から出すなら、最初の行動を出現にする(切り替わるのは最初の行動の更新)
		if (m_isEmergeOnSpawn)
		{
			m_stateMachine.ReserveChangeState(ESwarmBossState::Emerge);
		}

		// 小隊長 → 各小隊長のボイド
		return CreatePlatoonLeaders(a_context);
	}

	bool SwarmBossController::CreateLeader(Engine::GameObject::ObjectContext& a_context)
	{
		auto& _world = *a_context.pWorld;

		// リーダー生成
		const auto* _pPrefab = LoadPrefab(a_context, m_leaderPrefabGUID, m_leaderPrefabHandle);
		if (!_pPrefab)
		{
			ENGINE_WARNING("SwarmBossController : リーダー生成用のプレハブが設定されていません");
			return false;
		}

		// プレハブのインスタンス取得(位置はここで入る)。
		// 地面から出すときは、生成位置の真下へ埋めておく
		App::Utility::SpawnParams _params = {};
		_params.pos = m_spawnPos;
		if (m_isEmergeOnSpawn) _params.pos.y -= std::max(m_emergeDepth, 0.0f);

		std::vector<Engine::Resource::PrefabInstanceData> _instanceVec = {};
		if (!App::Utility::BuildSpawnInstanceData(_world, *_pPrefab, _params, _instanceVec))
		{
			ENGINE_ERRLOG(false, "SwarmBossController : リーダー生成用のプレハブの取得に失敗");
			return false;
		}

		// リーダーに必須なコンポーネントを付与 : すでにあればスキップ
		// (LocalTransform は BuildSpawnInstanceData が足している)
		ApplyMoveSpeed(_world, _instanceVec, m_leaderSpeed);
		EnsureRootComponent<Component::DesiredVelocityComponent>(_world, _instanceVec);
		EnsureRootComponent<Component::BoidLeaderComponent>(_world, _instanceVec);

		// 地面との関係(上下にレイを打つのは SerchGroundSystem)。アッパー攻撃で潜る深さに使う
		EnsureRootComponent<Component::SerchGroundComponent>(_world, _instanceVec);

		// 移動入力の受け皿。中身を書くのはこのクラス(UpdateLeaderBrain)
		EnsureRootComponent<Component::MoveIntentComponent>(_world, _instanceVec);

		// どちらを向いているか。進んでいる向きへ寄せるのは SwarmLookSystem、
		// 体の向きにするのは RotationSystem。既定は Yaw 0 = +Z 前方で、
		// 小隊長を並べる向き(PLATOON_LINE_DIR)と揃えてある。
		// 上下も体ごと向かせる(空を泳ぐので、人型のように上体だけでは向かない)
		EditRootComponent<Component::LookAngleComponent>(_world, _instanceVec,
			[this](Component::LookAngleComponent& a_comp)
			{
				a_comp.isApplyPitchToBody = true;

				// 地面から出すときは真上(上限まで)を向いた姿勢から始める
				if (m_isEmergeOnSpawn)
				{
					a_comp.Yaw   = m_spawnYaw;
					a_comp.Pitch = a_comp.maxPitch;
				}
			}
		);

		// リーダー生成
		m_leaderEntity = App::Utility::CreateInstanceNow(_world, _instanceVec);
		if (m_leaderEntity == Engine::ECS::Limits::INVALID_ENTITY)
		{
			ENGINE_ERRLOG(false, "SwarmBossController : リーダーのエンティティを作れませんでした");
			return false;
		}

		return true;
	}

	bool SwarmBossController::CreatePlatoonLeaders(Engine::GameObject::ObjectContext& a_context)
	{
		auto& _world = *a_context.pWorld;

		m_platoonLeaderEntities.clear();
		m_currentBoids = 0;

		if (m_maxPlatoonLeader == 0) return true;

		// プレハブ取得
		const auto* _pPrefab = LoadPrefab(a_context, m_platoonPrefabGUID, m_platoonPrefab);
		if (!_pPrefab)
		{
			ENGINE_WARNING("SwarmBossController : 小隊長生成用のプレハブが設定されていません");
			return false;
		}

		m_platoonLeaderEntities.reserve(m_maxPlatoonLeader);

		// 一列に並べるので、一つ前の相手とその位置を持ち回る
		Engine::ECS::Entity _preLeader = m_leaderEntity;
		Math::Vector3 _prePos = m_spawnPos;
		if (_world.HasComponent<Component::LocalTransformComponent>(m_leaderEntity))
		{
			_prePos = _world.RefData<Component::LocalTransformComponent>(m_leaderEntity)->pos;
		}

		// 並べる向き。地面から出すときはリーダーが向いている向き(ほぼ真上)の真後ろ = 地中へ縦一列。
		// 小隊長は前の相手の真後ろを追うので、同じ向きに並べておけば出した直後に列が動かない
		Math::Vector3 _lineDir = PLATOON_LINE_DIR;
		if (m_isEmergeOnSpawn && _world.HasComponent<Component::LookAngleComponent>(m_leaderEntity))
		{
			_lineDir = -Component::MakeLookForward(*_world.RefData<Component::LookAngleComponent>(m_leaderEntity));
		}

		// ワーム上の1次元位置。頭(リーダー)を0として、間隔を足し上げていく。
		// 体が曲がっても値は変わらない(伸ばした一本の紐の上での距離)
		float _alongWorm = 0.0f;

		// 最大数分小隊長を生成
		for (uint32_t _i = 0; _i < m_maxPlatoonLeader; ++_i)
		{
			// プレハブのインスタンス取得
			std::vector<Engine::Resource::PrefabInstanceData> _instanceVec = {};
			if (!App::Utility::BuildSpawnInstanceData(_world, *_pPrefab, {}, _instanceVec))
			{
				ENGINE_ERRLOG(false, "SwarmBossController : 小隊長生成用のプレハブの取得に失敗");
				return false;
			}

			// 初期化時に一つ前の小隊長のIDをコンポーネントに覚えさせる
			// 初めの小隊長はリーダーのEntityIDを覚えさせる
			// 間隔はプレハブに保存された値を使う
			float _distance = 0.0f;
			EditRootComponent<Component::PlatoonLeaderComponent>(_world, _instanceVec,
				[&](Component::PlatoonLeaderComponent& a_comp)
				{
					a_comp.preLeader    = _preLeader;
					a_comp.platoonIndex = static_cast<int>(_i);
					_distance = a_comp.distance;

					// ワーム上での位置。一つ前の相手の位置に間隔を足したもの。
					// ボイドはこの値を基準に自分の位置を出す(BoidWaveSystem)
					a_comp.distanceAlongWorm = _alongWorm + a_comp.distance;
				});

			// 一つ前の相手の後ろへ間隔ぶん下げて置く
			const Math::Vector3 _pos = _prePos + _lineDir * _distance;
			EditRootComponent<Component::LocalTransformComponent>(_world, _instanceVec,
				[&](Component::LocalTransformComponent& a_comp)
				{
					a_comp.pos     = _pos;
					a_comp.isDirty = true;
				});

			// 必須なコンポーネントを付与 : すでにあればスキップ。
			// 速さはリーダーより速くしておく(同じだと離された分を詰められない)
			ApplyMoveSpeed(_world, _instanceVec, m_leaderSpeed * m_platoonSpeedScale);
			EnsureRootComponent<Component::DesiredVelocityComponent>(_world, _instanceVec);

			// 胴回りの回転の命令の受け皿(書くのは UpdateBodySpin、読むのは FollowLeaderSystem)
			EnsureRootComponent<Component::PlatoonSpinComponent>(_world, _instanceVec);

			// 前の相手の後ろを狙うのに前方が要る(LookAngle から作る)。
			// 上下も体ごと向く(列が潜っても機体の向きが進路と揃う)
			EditRootComponent<Component::LookAngleComponent>(_world, _instanceVec,
				[this](Component::LookAngleComponent& a_comp)
				{
					a_comp.isApplyPitchToBody = true;

					// 地面から出すときはリーダーと同じく上を向いた姿勢から
					if (m_isEmergeOnSpawn)
					{
						a_comp.Yaw   = m_spawnYaw;
						a_comp.Pitch = a_comp.maxPitch;
					}
				}
			);

			const Engine::ECS::Entity _entity = App::Utility::CreateInstanceNow(_world, _instanceVec);
			if (_entity == Engine::ECS::Limits::INVALID_ENTITY)
			{
				ENGINE_ERRLOG(false, "SwarmBossController : 小隊長のエンティティを作れませんでした");
				return false;
			}

			m_platoonLeaderEntities.push_back(_entity);
			_preLeader  = _entity;
			_prePos     = _pos;
			_alongWorm += _distance;
		}

		// 最後尾の位置 = ワームの長さ。ウェーブを捨てる位置に使う
		m_tailAlongWorm = _alongWorm;

		// ボイド生成 : 小隊長が出揃ってから数を振り分ける
		bool _isSucceeded = true;
		for (size_t _i = 0; _i < m_platoonLeaderEntities.size(); ++_i)
		{
			const Engine::ECS::Entity _platoon = m_platoonLeaderEntities[_i];

			// 位置は生成時に書き込んだ値をそのまま読む(まだ行列は組まれていない)
			Math::Vector3 _center = m_spawnPos;
			if (_world.HasComponent<Component::LocalTransformComponent>(_platoon))
			{
				_center = _world.RefData<Component::LocalTransformComponent>(_platoon)->pos;
			}

			const uint32_t _count = GetBoidCountForPlatoon(_i, m_platoonLeaderEntities.size());
			if (!CreateBoids(a_context, _platoon, static_cast<int>(_i), _count, _center))
			{
				_isSucceeded = false;
			}
		}

		return _isSucceeded;
	}

	bool SwarmBossController::CreateBoids(
		Engine::GameObject::ObjectContext& a_context,
		Engine::ECS::Entity a_platoonLeader,
		int a_platoonIndex,
		uint32_t a_count,
		const Math::Vector3& a_center)
	{
		if (a_count == 0) return true;

		auto& _world = *a_context.pWorld;
		auto& _rm = *a_context.pServices->pResourceManager;

		// 出すボイドの設定は小隊長が持っている
		if (!_world.HasComponent<Component::BoidSpownerComponent>(a_platoonLeader))
		{
			ENGINE_WARNING("SwarmBossController : 小隊長のプレハブに BoidSpownerComponent がありません");
			return false;
		}

		const Engine::Resource::Prefab* _pBoidPrefab = nullptr;
		float _radius = 0.0f;
		{
			// この後エンティティを作るとチャンクが動くことがあるので、
			// コンポーネントへのポインタはこのブロックの中だけで使う
			Component::BoidSpownerComponent* _pSpowner = _world.RefData<Component::BoidSpownerComponent>(a_platoonLeader);
			if (!_pSpowner->boidPrefabGUID.IsValid())
			{
				ENGINE_WARNING("SwarmBossController : BoidSpownerComponent のボイドプレハブが設定されていません");
				return false;
			}

			// 参照は小隊長のコンポーネントが持つ(返すのはその解放フック)。
			// 生成したばかりで入っている値は持ち主ではない(プレハブの保存値)ので、必ず1つ取る
			_rm.AcquireImmediate(_pSpowner->prefab, _pSpowner->boidPrefabGUID);

			_pBoidPrefab = _rm.Get(_pSpowner->prefab);
			_radius = _pSpowner->spawnRadius;
		}

		if (!_pBoidPrefab)
		{
			ENGINE_ERRLOG(false, "SwarmBossController : ボイド生成用のプレハブの取得に失敗");
			return false;
		}

		// ボイドの共通設定
		//   追従先 : 自分の小隊長(FollowLeaderSystem が目標地点へ流す)
		//   印     : このオブジェクトのGUID + 小隊番号(生存数を数える)
		App::Utility::SpawnParams _params = {};
		_params.followTarget     = a_platoonLeader;
		_params.spawnerGUID      = m_guid;
		_params.waveIndex        = a_platoonIndex;

		uint32_t _created = 0;
		for (uint32_t _i = 0; _i < a_count; ++_i)
		{
			_params.pos = a_center + RandomInSphere(_radius);

			std::vector<Engine::Resource::PrefabInstanceData> _instanceVec = {};
			if (!App::Utility::BuildSpawnInstanceData(_world, *_pBoidPrefab, _params, _instanceVec)) break;

			// 必須なコンポーネントを付与 : すでにあればスキップ。
			// 速さはリーダー・小隊長より速くしておく(最後尾なので一番速さが要る)。
			// 舵(maxSteeringForce)も速さに比例させないと、最高速に乗る前に曲がれなくなる
			const float _boidSpeed = m_leaderSpeed * m_boidSpeedScale;
			EditRootComponent<Component::BoidSteeringParamsComponent>(_world, _instanceVec,
				[&](Component::BoidSteeringParamsComponent& a_comp)
				{
					a_comp.maxSpeed         = _boidSpeed;
					a_comp.maxSteeringForce = _boidSpeed * 4.0f;
				}
			);
			EditRootComponent<Component::BoidMembershipComponent>(_world, _instanceVec,
				[&](Component::BoidMembershipComponent& a_comp)
				{
					a_comp.platoonID = a_platoonLeader;
				}
			);
			ApplyMoveSpeed(_world, _instanceVec, _boidSpeed);
			EnsureRootComponent<Component::DesiredVelocityComponent>(_world, _instanceVec);

			// ボスの体である印。Controller はこれを数えて体力にする
			EnsureRootComponent<Component::SwarmBossBoidTag>(_world, _instanceVec);

			// 防御比率(HealthSystem が受けたダメージに掛ける)。小隊長の整理中だけ 0 にする
			EditRootComponent<Component::DefenseRatioComponent>(_world, _instanceVec,
				[this](Component::DefenseRatioComponent& a_comp)
				{
					a_comp.ratio = m_bodyDefenseRatio;
				}
			);

			// 体当たりのダメージ(BoidContactDamageSystem)。持つのは待ち時間だけ
			EnsureRootComponent<Component::BoidContactDamageComponent>(_world, _instanceVec);

			// 体を走る発光のウェーブ(BoidWaveSystem)。
			// 計算途中の値と発光の差し替えの置き場。発光を ModelComponent へ写すのは
			// ApplyEmissiveOverrideSystem で、差し替えが立つまではプレハブの発光のまま
			EnsureRootComponent<Component::BoidWaveStateComponent>(_world, _instanceVec);
			EnsureRootComponent<Component::EmissiveOverrideComponent>(_world, _instanceVec);

			// 地面の近く・地面の中で砂埃を炊く番を待つ時間(BoidGroundEffectSystem)。
			// 最初の番をばらしておき、全員が同じフレームにレイを打たないようにする
			EditRootComponent<Component::WarmGroundEffectComponent>(_world, _instanceVec,
				[this](Component::WarmGroundEffectComponent& a_comp)
				{
					a_comp.timer = Math::Random::Float(0.0f, std::max(m_groundEffectInterval, 0.01f));
				}
			);

			// 当たり判定
			EditRootComponent<Component::ColliderComponent>(_world, _instanceVec,
				[&](Component::ColliderComponent& a_comp)
				{
					// プレイヤーの攻撃にだけ当たる(当てに来るのは弾の側)。
					// 自分からは当たりに行かず、押し出しもしないので地形はすり抜ける
					a_comp.layer        = Component::ECollisionLayer::Enemy;
					a_comp.collideLayer = Component::ECollisionLayer::None;
					a_comp.isPhysical   = 0;

					// Mesh 以外なので、ボディは描画メッシュのAABBの箱になる。
					// 半径は判定を出す側の SphereColliderComponent が持つ(下)
					a_comp.shapeType = Engine::Physics::EShapeType::Sphere;
				}
			);

			// 判定を出す側(HitDetectSystem)に要る球と、当たった結果の受け皿
			EditRootComponent<Component::SphereColliderComponent>(_world, _instanceVec,
				[&](Component::SphereColliderComponent& a_comp)
				{
					a_comp.radius = m_boidColliderRadius;
				}
			);
			EnsureRootComponent<Engine::ECS::CollisionEvent>(_world, _instanceVec);

			// 体力 : 落とされた1体ぶんがボスの体力1になる
			EditRootComponent<Component::HealthComponent>(_world, _instanceVec,
				[&](Component::HealthComponent& a_comp)
				{
					a_comp.maxHealth    = m_boidHealth;
					a_comp.releaseDelay = m_boidReleaseDelay;
				}
			);

			// 向きは所属している小隊長の向きへ寄せる(SwarmLookSystem)。上下も体ごと向く
			EditRootComponent<Component::LookAngleComponent>(_world, _instanceVec,
				[](Component::LookAngleComponent& a_comp)
				{
					a_comp.isApplyPitchToBody = true;
				}
			);

			if (App::Utility::CreateInstanceNow(_world, _instanceVec) == Engine::ECS::Limits::INVALID_ENTITY) break;
			++_created;
		}

		m_currentBoids += _created;

		if (_created != a_count)
		{
			ENGINE_WARNING("SwarmBossController : ボイドを出し切れませんでした(%u / %u)", _created, a_count);
			return false;
		}
		return true;
	}

	uint32_t SwarmBossController::GetBoidCountForPlatoon(size_t a_platoonIndex, size_t a_platoonCount) const
	{
		if (a_platoonCount == 0) return 0;

		const uint32_t _base = m_maxBoid / static_cast<uint32_t>(a_platoonCount);
		const uint32_t _rest = m_maxBoid % static_cast<uint32_t>(a_platoonCount);

		// 余りは前の小隊から1体ずつ足す(合計が最大数と一致するように)
		return _base + (a_platoonIndex < _rest ? 1u : 0u);
	}

	//======================================================================================
	// ボスの体力 : 自分が出したボイドの数
	//--------------------------------------------------------------------------------------
	// 印(SwarmBossBoidTag)を数えるだけ。ボイドのIDは持ち歩かないので、撃ち落とされて
	// 消えたぶんは次のフレームの数え上げで自然に減る。
	//
	// ・同じシーンに群れのボスが2体居ても混ざらないよう、生成元の印(SpawnerComponent)も見る。
	// ・死亡状態のものは数えない。体力が尽きてもすぐには消えず(演出の猶予)、
	//   ActiveTag はその間も付いたままなので、ここで外さないと落としたぶんが反映されない。
	//======================================================================================
	uint32_t SwarmBossController::CountAliveBoids(Engine::GameObject::ObjectContext& a_context) const
	{
		uint32_t _count = 0;
		const Core::GUID _self = m_guid;

		// 解放待ち(ActiveTag が外れたもの)は数えない
		a_context.pWorld->ForEach<const Component::ActiveTag, const Component::SwarmBossBoidTag, const Component::SpawnerComponent>(
			[&_count, &_self, &a_context](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Component::ActiveTag* /*a_activeTagArray*/,
				const Component::SwarmBossBoidTag* /*a_boidTagArray*/,
				const Component::SpawnerComponent* a_spawnerArray)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					if (a_spawnerArray[_i].spawnerGUID != _self) continue;

					// 死亡状態(消えるのを待っているだけ)のものは体力に数えない
					const Engine::ECS::Entity _entity = a_pChunk->entityData[_i];
					if (a_context.pWorld->HasComponent<Component::HealthComponent>(_entity))
					{
						const auto* _pHealth = a_context.pWorld->RefData<Component::HealthComponent>(_entity);
						if (_pHealth && _pHealth->isDead) continue;
					}

					++_count;
				}
			}
		);

		return _count;
	}

	//======================================================================================
	// シリアライズ
	//======================================================================================
	void SwarmBossController::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// ---- リーダー ----
		a_ar.GUIDField("LeaderPrefabGUID", m_leaderPrefabGUID);
		a_ar.Field("SpawnPos", m_spawnPos);

		// ---- 小隊長 ----
		a_ar.GUIDField("PlatoonPrefabGUID", m_platoonPrefabGUID);
		a_ar.Field("MaxPlatoonLeader", m_maxPlatoonLeader);

		// ---- ボイド ----
		a_ar.Field("MaxBoid", m_maxBoid);
		a_ar.Field("BoidColliderRadius", m_boidColliderRadius);
		a_ar.Field("BoidHealth", m_boidHealth);
		a_ar.Field("BoidReleaseDelay", m_boidReleaseDelay);
		a_ar.Field("ContactDamage", m_contactDamage);
		a_ar.Field("ContactDamageCooldown", m_contactDamageCooldown);

		// ---- 速さの配分 ----
		a_ar.Field("LeaderSpeed", m_leaderSpeed);
		a_ar.Field("PlatoonSpeedScale", m_platoonSpeedScale);
		a_ar.Field("BoidSpeedScale", m_boidSpeedScale);

		// ---- 行動 ----
		// 調整値は各ステートが持つ(名前は以前と同じなので既存シーンもそのまま読める)
		m_stateMachine.Archive(a_ar);
		a_ar.Field("ReorganizeHpInterval", m_reorganizeHpInterval);
		a_ar.Field("DeathHp", m_deathHp);
		a_ar.GUIDField("BurstEffectGUID", m_burstEffectGUID);
		a_ar.Field("BurstEffectScale", m_burstEffectScale);
		if (a_ar.IsLoading()) m_burstEffectRef = {};	// GUID が変わっているかもしれないので、死亡に入ったときに読み直す

		// ---- ウェーブ ----
		// 走っている位置は生成後に決まるので保存しない
		a_ar.Field("WaveSpeed", m_waveSpeed);
		a_ar.Field("WaveInterval", m_waveInterval);
		a_ar.Field("WaveWidth", m_waveWidth);
		a_ar.Field("MaxWave", m_maxWave);
		a_ar.Field("WaveBaseIntensity", m_waveBaseIntensity);
		a_ar.Field("WavePeakIntensity", m_wavePeakIntensity);
		a_ar.Field("WaveBaseColor", m_waveBaseColor);
		a_ar.Field("WavePeakColor", m_wavePeakColor);

		// ---- 砂埃 ----
		a_ar.GUIDField("GroundEffectGUID", m_groundEffectGUID);
		a_ar.Field("GroundEffectMaxHeight", m_groundEffectMaxHeight);
		a_ar.Field("GroundEffectMaxDepth", m_groundEffectMaxDepth);
		a_ar.Field("GroundEffectNearScale", m_groundEffectNearScale);
		a_ar.Field("GroundEffectFarScale", m_groundEffectFarScale);
		a_ar.Field("GroundEffectUnderScale", m_groundEffectUnderScale);
		a_ar.Field("GroundEffectInterval", m_groundEffectInterval);
		a_ar.Field("GroundEffectMaxSpawnPerFrame", m_groundEffectMaxSpawnPerFrame);

		// ---- 潜る / 出るときの大きな砂埃 ----
		// 読み込みは初めて炊くとき(UpdateBurrowEffect)。中身を読むのにワールドが要るため
		a_ar.GUIDField("BurrowEffectGUID", m_burrowEffectGUID);
		a_ar.Field("BurrowEffectCooldown", m_burrowEffectCooldown);

		// 炊くたびに読み込みが走らないよう、読んだ時点で握っておく
		if (a_ar.IsLoading() && a_context.pServices && a_context.pServices->pResourceManager)
		{
			m_groundEffectRef = (m_groundEffectGUID != Core::DEFAULT_GUID)
				? a_context.pServices->pResourceManager->RequestLoad<Engine::Resource::EffectAsset>(m_groundEffectGUID)
				: Engine::ResourceRef<Engine::Resource::EffectAsset>{};
		}

		//----------------------------------------------------------------------
		// ここから下は後から足したもの。
		// バイナリは書いた順に読むので、区切りを付けて末尾に置く(足すときは末尾へ新しい区切りで)。
		// 区切りを持たない古い .obscene では読まず、既定値のまま残す
		//----------------------------------------------------------------------
		if (!a_ar.IsLegacyLayout())
		{
			// ---- 出現と咆哮 ----
			{
				Engine::Persistence::ArchiveSection _section(a_ar, "SwarmBossEmerge");
				a_ar.Field("IsEmergeOnSpawn", m_isEmergeOnSpawn);
				a_ar.Field("EmergeDepth", m_emergeDepth);

				a_ar.GUIDField("RoarSoundGUID", m_roarSoundGUID);
				a_ar.Field("RoarVolume", m_roarVolume);
				a_ar.Field("RoarBlurStrength", m_roarBlurStrength);
				a_ar.Field("RoarBlurStrengthJitter", m_roarBlurStrengthJitter);
				a_ar.Field("RoarBlurCenterJitter", m_roarBlurCenterJitter);
				a_ar.Field("RoarBlurFrequency", m_roarBlurFrequency);
			}

			// ---- 胴回りの回転 ----
			{
				Engine::Persistence::ArchiveSection _section(a_ar, "SwarmBossSpin");
				a_ar.Field("SpinMaxSpeed", m_spinMaxSpeed);
				a_ar.Field("SpinAcceleration", m_spinAcceleration);
				a_ar.Field("SpinDeceleration", m_spinDeceleration);
				a_ar.Field("SpinRadius", m_spinRadius);
				a_ar.Field("SpinRadiusRate", m_spinRadiusRate);
				a_ar.Field("SpinTwist", m_spinTwist);
			}

			// ---- 行動(後から足した調整値。中はステートごとに区切る) ----
			{
				Engine::Persistence::ArchiveSection _section(a_ar, "SwarmBossStates");
				m_stateMachine.ArchiveAppended(a_ar);
			}
		}

		// 鳴き声が差し替わっているかもしれないので、次に鳴くときに借り直す
		if (a_ar.IsLoading()) ReleaseRoarSound(a_context);
	}

	//======================================================================================
	// エディター
	//======================================================================================
	void SwarmBossController::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices) return;
		auto& _services = *a_context.pServices;

		Engine::EditorField::Header("リーダー");
		Engine::EditorField::AssetField(_services, "リーダーのプレハブ", "Prefab", m_leaderPrefabGUID);
		Engine::EditorField::Field("生成位置", m_spawnPos, 0.1f);
		Engine::EditorField::Field("地面から出す", m_isEmergeOnSpawn);
		Engine::EditorField::Tooltip("生成位置の真下へ縦一列に埋めて出し、出現の行動(真上へ飛び出してとぐろを巻き、鳴く)から始める");
		if (m_isEmergeOnSpawn)
		{
			Engine::EditorField::Field("頭を埋める深さ", m_emergeDepth, 0.5f, 0.0f);
			Engine::EditorField::Tooltip("生成位置からの深さ。体はさらにその下へ並ぶ(体の長さぶん)");
		}

		Engine::EditorField::Header("小隊長");
		Engine::EditorField::AssetField(_services, "小隊長のプレハブ", "Prefab", m_platoonPrefabGUID);
		Engine::EditorField::Field("小隊長の最大数", m_maxPlatoonLeader);
		Engine::EditorField::Tooltip("リーダーの後ろ(-Z)へ、PlatoonLeaderComponent.distance の間隔で一列に並ぶ");

		Engine::EditorField::Header("ボイド");
		Engine::EditorField::Field("ボイドの最大数", m_maxBoid);
		if (m_maxPlatoonLeader > 0)
		{
			Engine::EditorField::HelpText("1小隊あたり %u 体(先頭から %u 小隊は +1)", m_maxBoid / m_maxPlatoonLeader, m_maxBoid % m_maxPlatoonLeader);
		}
		Engine::EditorField::HelpText("出すボイドのプレハブと広さ : 小隊長プレハブの BoidSpownerComponent");

		Engine::EditorField::Field("ボイドの判定半径", m_boidColliderRadius, 0.05f, 0.0f);
		Engine::EditorField::Field("ボイドの体力", m_boidHealth, 1.0f, 0.0f);
		Engine::EditorField::Field("ボイドが消えるまでの猶予", m_boidReleaseDelay, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("当たるのはプレイヤーの攻撃だけ(地形はすり抜ける)");

		Engine::EditorField::Field("体当たりのダメージ", m_contactDamage, 0.5f, 0.0f);
		Engine::EditorField::Field("体当たりの待ち時間", m_contactDamageCooldown, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("ボイドごとに、プレイヤーに触れたらダメージを与え、その後この秒数は判定しない");

		Engine::EditorField::Header("速さ");
		Engine::EditorField::Field("リーダーの速さ", m_leaderSpeed, 0.5f, 0.0f);
		Engine::EditorField::Field("小隊長の倍率", m_platoonSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Field("ボイドの倍率", m_boidSpeedScale, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("小隊長 %.1f / ボイド %.1f (生成時に書き込み、プレハブの値より優先)", m_leaderSpeed * m_platoonSpeedScale, m_leaderSpeed * m_boidSpeedScale);

		Engine::EditorField::Header("小隊長の整理");
		Engine::EditorField::Field("整理する体力の間隔", m_reorganizeHpInterval);
		Engine::EditorField::Tooltip("ボイドがこの数だけ減るたびに球状にまとまり、小隊長を体力の比率まで減らす(0 で整理しない)");
		if (m_reorganizeHpInterval > 0)
		{
			const int _next = static_cast<int>(m_maxBoid) - static_cast<int>(m_reorganizeHpInterval * (m_reorganizeCount + 1));
			Engine::EditorField::HelpText("次 : 体力 %d (整理済み %u 回)", _next, m_reorganizeCount);
		}
		Engine::EditorField::Value("体の防御比率", "%.2f", m_bodyDefenseRatio);

		Engine::EditorField::Header("死亡");
		Engine::EditorField::Field("死亡する体力", m_deathHp);
		Engine::EditorField::Tooltip("体力がこれ以下になったら、地上の高いところで球状にまとまり、ウェーブを速めて爆散する(0 で死亡しない)");
		if (Engine::EditorField::AssetField(_services, "爆散のエフェクト", "EffectAsset", m_burstEffectGUID))
		{
			// 差し替えたら読み直す
			m_burstEffectRef = {};
			RequestLoadBurstEffect(a_context);
		}
		if (m_burstEffectGUID == Core::DEFAULT_GUID)
		{
			Engine::EditorField::HelpText("(未設定 : 中心の爆発は出ない)");
		}
		else if (m_burstEffectRef)
		{
			// 出しっぱなしのパーツがあると destroyOnFinish で消えずに残り続ける
			const auto* _pEffect = _services.pResourceManager->Get(m_burstEffectRef);
			if (_pEffect && !IsOneShotEffect(*_pEffect))
			{
				Engine::EditorField::ErrorText("長さ0(終わらない)のパーツがあるため、消えずに残り続ける");
			}
		}
		Engine::EditorField::Field("爆散のエフェクトの大きさ", m_burstEffectScale, 0.1f, 0.0f);
		Engine::EditorField::Value("死亡中", "%s%s", m_isDying ? "はい" : "いいえ", m_isBurst ? " (爆散済み)" : "");
		Engine::EditorField::Value("ウェーブの倍率", "x %.2f", m_waveSpeedScale);

		Engine::EditorField::Header("咆哮");
		if (Engine::EditorField::AssetField(_services, "鳴き声", "Sound", m_roarSoundGUID))
		{
			// 差し替えたら次に鳴くときに借り直す
			ReleaseRoarSound(a_context);
		}
		if (m_roarSoundGUID == Core::DEFAULT_GUID)
		{
			Engine::EditorField::HelpText("(未設定 : 鳴き声は出ない)");
		}
		Engine::EditorField::Field("鳴き声の音量", m_roarVolume, 0.01f, 0.0f);
		Engine::EditorField::Field("震えの引きずり量", m_roarBlurStrength, 0.005f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("画面のラジアルブラーへ足す量(UV単位)。カメラのブラーを切っていても掛かる");
		Engine::EditorField::Field("引きずり量の揺れ幅", m_roarBlurStrengthJitter, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("中心の揺れ幅", m_roarBlurCenterJitter, 0.001f, 0.0f, 0.5f);
		Engine::EditorField::Field("揺れの速さ", m_roarBlurFrequency, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("回/秒");
		Engine::EditorField::Value("震えの強さ", "%.2f", m_roarWeight);
		if (Engine::EditorField::Button("鳴き声を試す"))
		{
			PlayRoar(a_context);
		}

		Engine::EditorField::Header("胴回りの回転(突進・ダイブ)");
		Engine::EditorField::Field("回転の最高速", m_spinMaxSpeed, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("ラジアン/秒(6.28 で 1 秒に 1 周)。速すぎるとボイドが追い付けず、輪が崩れる");
		Engine::EditorField::Field("回転を上げる速さ", m_spinAcceleration, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("ラジアン/秒^2。小さいほど、だんだん速くなっていく");
		Engine::EditorField::Field("回転を緩める速さ", m_spinDeceleration, 0.1f, 0.0f);
		Engine::EditorField::Field("輪の半径", m_spinRadius, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("小隊長からの距離(m)。ボイドごとに 0.6〜1 倍へばらける");
		Engine::EditorField::Field("輪を広げる速さ", m_spinRadiusRate, 0.05f, 0.0f);
		Engine::EditorField::Field("小隊長ごとの角度のずれ", m_spinTwist, 0.01f);
		Engine::EditorField::Tooltip("ラジアン。体全体がねじれた螺旋に見える(0 で全員が揃って回る)");
		if (m_spinMaxSpeed > 0.0f && m_spinAcceleration > 0.0f)
		{
			Engine::EditorField::HelpText("最高速まで %.1f 秒(1 秒に %.2f 周)", m_spinMaxSpeed / m_spinAcceleration, m_spinMaxSpeed / DirectX::XM_2PI);
		}
		Engine::EditorField::Value("回転", "%.2f rad/s (頼まれた強さ %.2f / 輪の広がり %.2f)", m_spinSpeed, m_spinRequestRate, m_spinRadiusRatio);

		Engine::EditorField::Header("リーダーの行動");
		m_stateMachine.DrawInspector();

		Engine::EditorField::Header("ウェーブ");
		Engine::EditorField::Field("ウェーブの速さ", m_waveSpeed, 1.0f, 0.0f);
		Engine::EditorField::Field("ウェーブの間隔", m_waveInterval, 0.05f, 0.0f);
		Engine::EditorField::Field("ウェーブの幅", m_waveWidth, 0.5f, 0.0f);
		Engine::EditorField::Field("ウェーブの最大本数", m_maxWave);
		Engine::EditorField::Field("ベースの発光の強さ", m_waveBaseIntensity, 0.05f, 0.0f);
		Engine::EditorField::Field("ピークの発光の強さ", m_wavePeakIntensity, 0.05f, 0.0f);
		Engine::EditorField::ColorField("ベースの色", m_waveBaseColor);
		Engine::EditorField::ColorField("ピークの色", m_wavePeakColor);
		Engine::EditorField::Tooltip("ブルームは 1.0 を超えた画素を拾うので、ピークはそれより上にする");

		// 頭から尾までを流れるので、1本が抜けるまでにかかる時間を出しておく
		if (m_waveSpeed > 0.0f)
		{
			Engine::EditorField::HelpText("体の長さ %.1f m / 尾まで %.1f 秒(間隔 %.1f 秒)", GetWormLength(), (GetWormLength() + m_waveWidth) / m_waveSpeed, m_waveInterval);
		}
		Engine::EditorField::Value("走っている本数", "%u", static_cast<uint32_t>(m_waveVec.size()));

		Engine::EditorField::Header("砂埃");
		if (Engine::EditorField::AssetField(
			_services, "砂埃のエフェクト", "EffectAsset", m_groundEffectGUID))
		{
			m_groundEffectRef = (m_groundEffectGUID != Core::DEFAULT_GUID)
				? _services.pResourceManager->RequestLoad<Engine::Resource::EffectAsset>(m_groundEffectGUID)
				: Engine::ResourceRef<Engine::Resource::EffectAsset>{};
		}
		if (m_groundEffectGUID == Core::DEFAULT_GUID)
		{
			Engine::EditorField::HelpText("(未設定 : 砂埃は出ない)");
		}
		else if (!m_isGroundEffectOneShot)
		{
			Engine::EditorField::ErrorText("読み込み中か、長さ0(終わらない)のパーツがあるため炊かない");
		}
		Engine::EditorField::Field("炊く高さの上限", m_groundEffectMaxHeight, 0.5f, 0.0f);
		Engine::EditorField::Field("炊く深さの上限", m_groundEffectMaxDepth, 1.0f, 0.0f);
		Engine::EditorField::Field("地表すれすれでの大きさ", m_groundEffectNearScale, 0.01f, 0.0f);
		Engine::EditorField::Field("上限の高さでの大きさ", m_groundEffectFarScale, 0.01f, 0.0f);
		Engine::EditorField::Field("地中での大きさ", m_groundEffectUnderScale, 0.01f, 0.0f);
		Engine::EditorField::Field("炊く間隔", m_groundEffectInterval, 0.05f, 0.01f);
		Engine::EditorField::Field("1フレームに出す上限", m_groundEffectMaxSpawnPerFrame);

		Engine::EditorField::Header("潜る/出るときの砂埃(リーダー)");
		if (Engine::EditorField::AssetField(
			_services, "潜る/出るときのエフェクト", "EffectPrefab", m_burrowEffectGUID))
		{
			// 差し替えたら次に炊くときに読み直す
			m_burrowEffectRef = {};
		}
		Engine::EditorField::Field("潜る/出るときの砂埃の間隔", m_burrowEffectCooldown, 0.05f, 0.0f);
		Engine::EditorField::Value("リーダーの位置", "%s", !m_isLeaderGroundKnown ? "(不明)"
			: (m_wasLeaderUnderGround ? "地中" : "地上"));

		// 間隔が来たボイドだけがレイを打つので、1フレームの本数の目安を出しておく
		if (m_groundEffectInterval > 0.0f)
		{
			Engine::EditorField::HelpText("レイ : 毎秒およそ %.0f 体(1体につき最大2本)", static_cast<float>(m_maxBoid) / m_groundEffectInterval);
		}

		// ここから下は実行中の状態なので表示のみ
		Engine::EditorField::Header("実行中");
		Engine::EditorField::Value("生成済み", "%s%s", m_isSpown ? "はい" : "いいえ",
			(!m_isSpown && m_isHoldAppear) ? " (シーケンスのウェーブ待ち)" : "");
		if (!m_isSpown)
		{
			// 置いた直後はプレハブ未設定のまま Awake を通っているので、設定してから出せるようにする
			Engine::EditorField::SameLine();
			if (Engine::EditorField::CreateSmallButton("生成"))
			{
				Spawn(a_context);
			}
		}

		Engine::EditorField::Value("リーダー", "%llu", static_cast<unsigned long long>(m_leaderEntity));
		Engine::EditorField::Value("小隊長の数", "%u / %u", static_cast<uint32_t>(m_platoonLeaderEntities.size()), m_maxPlatoonLeader);
		for (size_t _i = 0; _i < m_platoonLeaderEntities.size(); ++_i)
		{
			Engine::EditorField::BulletText("[%u] %llu", static_cast<uint32_t>(_i), static_cast<unsigned long long>(m_platoonLeaderEntities[_i]));
		}
		Engine::EditorField::Value("体力", "%u / %u (生存ボイド数)", m_currentBoids, m_maxBoid);
	}
}
