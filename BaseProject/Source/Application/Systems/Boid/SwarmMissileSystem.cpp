#include "SwarmMissileSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Boid/SwarmMissileComponent.h"
#include "Application/Components/Boid/SwarmBossBoidTag.h"
#include "Application/Components/Boid/BoidSteeringParamsComponent.h"
#include "Application/Components/Boid/BoidMembershipComponent.h"
#include "Application/Components/Boid/BoidWaveStateComponent.h"
#include "Application/Components/Boid/BoidContactDamageComponent.h"
#include "Application/Components/Camera/FollowTargetComponent.h"
#include "Application/Components/Render/EmissiveOverrideComponent.h"
#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Combat/DefenseRatioComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"

#include "Application/InstanceResource/SwarmMissileResource.h"
#include "Application/InstanceResource/SwarmContactDamageResource.h"
#include "Application/InstanceResource/HitEventResource.h"

namespace App::System
{
	//==============================================================================
	// SwarmMissileSystem
	//
	// ワームの体(ボイド)を1体ずつ切り離し、プレイヤーへ飛んでいく自爆ミサイルにする。
	// 「どの小隊長から切り離すか」を決めるのはワームボスの巻き付き攻撃(CoilAttack)で、
	// SwarmMissileResource に積まれた要求を受けて、ここが3段で処理する。
	//
	//   切り離し(PreUpdate / メインスレッド)
	//     要求された小隊長に属する生きたボイドを1体探し、群れの部品を外して
	//     SwarmMissileComponent を付ける(シグネチャ変更の予約。反映は次の BeginFrame)。
	//       外すもの … BoidSteeringParams(操舵) / FollowTarget(小隊長への追従) /
	//                   BoidWaveState(発光のウェーブ) / BoidContactDamage(体当たり)
	//       残すもの … SwarmBossBoidTag(ボスの体力に数える) / Health / Collider(撃ち落とせる)
	//     所属(platoonID)はその場で外す。反映までの間に同じボイドを二度選ばないためと、
	//     群れの写し(BoidSnapshotSystem)から先に抜くため。
	//
	//   飛行(PreUpdate / ワーカー)
	//     打ち上げ : 輪の外側・上へ launchTime 秒まっすぐ飛ぶ
	//     誘導     : 1秒に turnSpeedDeg 度まで向きを回しながら、最高速まで加速する
	//                一度プレイヤーを loseAngleDeg の内側に捉えたあと、外へ外れたら見失って直進する
	//                (通り過ぎてから回り込んで後ろから当たらないように。時間切れで落ちる)
	//     速度は目標速度(DesiredVelocity)へ書くだけで、座標を進めるのは MovementIntegrationSystem。
	//     HomingSystem と同じ理由で PreUpdate 帯に置く。
	//
	//   自爆(Update / メインスレッド)
	//     プレイヤーのカプセルに explodeRadius まで近づいたら、プレイヤーへダメージを積み、
	//     自分にも体力ぶんのダメージを積んで落とす。lifeTime を過ぎた(外れた)ものも落とす。
	//     落とすのは HealthSystem で、死亡エフェクトもボイドの EffectEventsComponent(OnDeath)のまま出る。
	//     ヒットを積むのは PreUpdate のクリアより後・HealthSystem(PostUpdate)より前なので
	//     Update 帯(BoidContactDamageSystem と同じ)。
	//==============================================================================
	namespace
	{
		// 線分 AB 上で点 P にいちばん近い点
		Math::Vector3 ClosestPointOnSegment(const Math::Vector3& a_p, const Math::Vector3& a_a, const Math::Vector3& a_b)
		{
			const Math::Vector3 _ab = a_b - a_a;
			const float _lenSq = _ab.LengthSquared();
			if (_lenSq <= 1e-8f) return a_a;

			const float _t = std::clamp((a_p - a_a).Dot(_ab) / _lenSq, 0.0f, 1.0f);
			return a_a + _ab * _t;
		}

		//--------------------------------------------------------------------------
		// 切り離した直後に飛び出す向き : 輪の外側(水平)と上と決まった向きを重みで混ぜ、少しばらす
		//--------------------------------------------------------------------------
		Math::Vector3 MakeLaunchDir(const InstanceResource::SwarmMissileResource& a_res, const Math::Vector3& a_pos)
		{
			Math::Vector3 _out = a_pos - a_res.ringCenter;
			_out.y = 0.0f;
			if (_out.LengthSquared() > 1e-6f) _out.Normalize();

			Math::Vector3 _dir = Math::Vector3::Up() * a_res.launchUp + _out * a_res.launchOut + a_res.launchForwardDir * a_res.launchForward;
			_dir += Math::Vector3(
				Math::Random::Float(-1.0f, 1.0f),
				Math::Random::Float(-1.0f, 1.0f),
				Math::Random::Float(-1.0f, 1.0f)) * a_res.launchSpread;

			if (_dir.LengthSquared() <= 1e-6f) _dir = Math::Vector3::Up();
			_dir.Normalize();
			return _dir;
		}

		//--------------------------------------------------------------------------
		// a_cur を a_target へ最大 a_maxAngle(ラジアン)だけ回す。どちらも単位ベクトル
		//--------------------------------------------------------------------------
		Math::Vector3 RotateToward(const Math::Vector3& a_cur, const Math::Vector3& a_target, float a_maxAngle)
		{
			const float _cos   = std::clamp(a_cur.Dot(a_target), -1.0f, 1.0f);
			const float _angle = std::acos(_cos);
			if (_angle <= a_maxAngle) return a_target;

			// 回転軸 = 今の向き × 目標の向き
			Math::Vector3 _axis = a_cur.Cross(a_target);
			if (_axis.LengthSquared() > 1e-8f)
			{
				_axis.Normalize();
			}
			else
			{
				// ちょうど真後ろ。軸が作れないので直交する適当な軸で回し始める
				const Math::Vector3 _ref = (std::fabs(a_cur.y) > 0.99f)
					? Math::Vector3(1.0f, 0.0f, 0.0f)
					: Math::Vector3(0.0f, 1.0f, 0.0f);
				_axis = a_cur.Cross(_ref);
				_axis.Normalize();
			}

			Math::Vector3 _dir = Math::Vector3::Transform(a_cur, Math::Quaternion::CreateFromAxisAngle(_axis, a_maxAngle));
			_dir.Normalize();
			return _dir;
		}

		// 1体ぶんの切り離し(ループの外で予約する)
		struct LaunchEntry
		{
			Engine::ECS::Entity entity = Engine::ECS::Limits::INVALID_ENTITY;
			Math::Vector3 pos = {};
		};
	}

	void SwarmMissileSystem::Init(App::ECS::APPWorld& a_world)
	{
		//--------------------------------------------------------------------------
		// 切り離し : 要求された小隊長のボイドを1体ずつミサイルにする
		//
		// 要求は1フレームに数件なので、全ボイドを1回走査して拾う。
		// 要求が無いフレームは走査しない。積む先(配列)を1本にするのでメインスレッドで回す
		//--------------------------------------------------------------------------
		a_world.ActiveCustomTask(
			Engine::ECS::ESystemType::PreUpdate,
			"SwarmMissileLaunchSystem",
			Engine::ECS::ReadList<Component::SwarmBossBoidTag, Component::BoidSteeringParamsComponent, Component::HealthComponent, Component::LocalTransformComponent>{},
			Engine::ECS::WriteList<Component::BoidMembershipComponent>{},
			[](const Engine::ECS::SystemContext& a_ctx)
			{
				if (!a_ctx.pWorld) return;
				auto& _world = *a_ctx.pWorld;
				if (!_world.HasResource<InstanceResource::SwarmMissileResource>()) return;

				InstanceResource::SwarmMissileResource& _res = _world.RefResource<InstanceResource::SwarmMissileResource>();
				if (_res.launchRequests.empty()) return;

				// 叶えた要求は無効値で潰していく
				std::vector<Engine::ECS::Entity>& _requests = _res.launchRequests;
				size_t _remaining = _requests.size();

				std::vector<LaunchEntry> _launchVec = {};
				_launchVec.reserve(_remaining);

				_world.ForEach<
					const Component::ActiveTag,
					const Component::SwarmBossBoidTag,
					const Component::BoidSteeringParamsComponent,
					const Component::HealthComponent,
					const Component::LocalTransformComponent,
					Component::BoidMembershipComponent>(
						[&](
							Engine::ECS::Chunk* a_pChunk,
							uint32_t a_count,
							const Component::ActiveTag*,
							const Component::SwarmBossBoidTag*,
							const Component::BoidSteeringParamsComponent*,
							const Component::HealthComponent* a_healthArray,
							const Component::LocalTransformComponent* a_trsArray,
							Component::BoidMembershipComponent* a_memberArray
						)
						{
							for (uint32_t _i = 0; _i < a_count && _remaining > 0; ++_i)
							{
								const Engine::ECS::Entity _platoon = a_memberArray[_i].platoonID;
								if (_platoon == Engine::ECS::Limits::INVALID_ENTITY) continue;

								// 撃ち落とされて消えるのを待っているものは飛ばさない
								if (a_healthArray[_i].isDead) continue;

								auto _it = std::find(_requests.begin(), _requests.end(), _platoon);
								if (_it == _requests.end()) continue;

								*_it = Engine::ECS::Limits::INVALID_ENTITY;
								--_remaining;

								// 群れから抜く(反映を待たずに写し・ウェーブの対象から外れる)
								a_memberArray[_i].platoonID = Engine::ECS::Limits::INVALID_ENTITY;

								_launchVec.push_back({ a_pChunk->entityData[_i], a_trsArray[_i].pos });
							}
						}
					);

				// ボイドが残っていない小隊長の要求も、ここで捨てる
				_requests.clear();

				//------------------------------------------------------------------
				// 群れの部品を外してミサイルにする(付け外しは1件の変更にまとめる。
				// ReserveAdd / ReserveRemove を重ねると、後の予約が前の予約を上書きする)
				//------------------------------------------------------------------
				const auto _missileID  = _world.GetCompTypeID<Component::SwarmMissileComponent>();
				const auto _emissiveID = _world.GetCompTypeID<Component::EmissiveOverrideComponent>();
				const auto _defenseID  = _world.GetCompTypeID<Component::DefenseRatioComponent>();

				const Engine::ECS::ComponentTypeID _removeIDs[] =
				{
					_world.GetCompTypeID<Component::BoidSteeringParamsComponent>(),
					_world.GetCompTypeID<Component::FollowTargetComponent>(),
					_world.GetCompTypeID<Component::BoidWaveStateComponent>(),
					_world.GetCompTypeID<Component::BoidContactDamageComponent>(),
				};

				for (const LaunchEntry& _entry : _launchVec)
				{
					Engine::ECS::ChangeEntityCmd _cmd = {};
					_cmd.entity = _entry.entity;
					_cmd.toSig  = _world.GetSignature(_entry.entity);

					for (const auto _id : _removeIDs) _cmd.toSig.reset(_id);
					_cmd.toSig.set(_missileID);

					// 打ち上げの向きと速さ
					Component::SwarmMissileComponent _missile = {};
					_missile.dir   = MakeLaunchDir(_res, _entry.pos);
					_missile.speed = _res.launchSpeed;
					{
						const auto* _pBytes = reinterpret_cast<const uint8_t*>(&_missile);
						_cmd.dataMap[_missileID] = std::vector<uint8_t>(_pBytes, _pBytes + sizeof(_missile));
					}

					// 発光をミサイルの色へ(ウェーブはもう塗らないので、この値が残り続ける)
					if (_cmd.toSig.test(_emissiveID))
					{
						Component::EmissiveOverrideComponent _emissive = {};
						_emissive.emissiveColor     = _res.color;
						_emissive.emissiveIntensity = _res.intensity;
						_emissive.isOverride        = true;

						const auto* _pBytes = reinterpret_cast<const uint8_t*>(&_emissive);
						_cmd.dataMap[_emissiveID] = std::vector<uint8_t>(_pBytes, _pBytes + sizeof(_emissive));
					}

					// 防御比率は 1 に戻す。体が無敵(小隊長の整理中)のまま飛び出すと、
					// 自爆のダメージも 0 になって落ちられなくなるため
					if (_cmd.toSig.test(_defenseID))
					{
						Component::DefenseRatioComponent _defense = {};
						_defense.ratio = 1.0f;

						const auto* _pBytes = reinterpret_cast<const uint8_t*>(&_defense);
						_cmd.dataMap[_defenseID] = std::vector<uint8_t>(_pBytes, _pBytes + sizeof(_defense));
					}

					_world.ReserveChangeSignature(std::move(_cmd));
				}
			}
		)
		.WritesResource<InstanceResource::SwarmMissileResource>();

		//--------------------------------------------------------------------------
		// 飛行 : 打ち上げ → 誘導。目標速度だけを書く
		//
		// 自分のチャンクの値だけを書く(リソースは読むだけ)ので、ワーカーで回す
		//--------------------------------------------------------------------------
		a_world.ActiveJobTask<Component::SwarmMissileComponent, Component::DesiredVelocityComponent, const Component::LocalTransformComponent, const Component::HealthComponent>(
			Engine::ECS::ESystemType::PreUpdate,
			"SwarmMissileFlightSystem",
			[](
				Engine::ECS::Chunk*,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				Component::SwarmMissileComponent* a_missileArray,
				Component::DesiredVelocityComponent* a_velArray,
				const Component::LocalTransformComponent* a_trsArray,
				const Component::HealthComponent* a_healthArray
			)
			{
				auto& _world = *a_ctx.pWorld;
				if (!_world.HasResource<InstanceResource::SwarmMissileResource>()) return;
				if (!_world.HasResource<InstanceResource::SwarmContactDamageResource>()) return;

				const InstanceResource::SwarmMissileResource& _res = _world.GetResource<InstanceResource::SwarmMissileResource>();
				const InstanceResource::SwarmContactDamageResource& _player = _world.GetResource<InstanceResource::SwarmContactDamageResource>();

				// 狙う点はカプセルの真ん中。プレイヤーが居なければまっすぐ飛ぶ
				const bool _isFoundPlayer = _player.isActive && _player.player != Engine::ECS::Limits::INVALID_ENTITY;
				const Math::Vector3 _targetPos = (_player.playerSegmentA + _player.playerSegmentB) * 0.5f;

				const float _maxTurn = DirectX::XMConvertToRadians(std::max(_res.turnSpeedDeg, 0.0f)) * a_ctx.dt;
				const bool  _isLoseEnabled = (_res.loseAngleDeg > 0.0f);
				const float _loseCos = std::cos(DirectX::XMConvertToRadians(_res.loseAngleDeg));

				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::SwarmMissileComponent& _missile = a_missileArray[_i];
					Component::DesiredVelocityComponent& _vel = a_velArray[_i];

					// 落ちた(自爆した / 撃ち落とされた)ものはその場で止める
					if (a_healthArray[_i].isDead || _missile.isExploded)
					{
						_vel.value = Math::Vector3(0.0f, 0.0f, 0.0f);
						continue;
					}

					_missile.time += a_ctx.dt;

					// 打ち上げが済んだら、プレイヤーへ向きを回しながら最高速まで上げる
					if (_missile.time >= _res.launchTime)
					{
						if (_isFoundPlayer && !_missile.isLost)
						{
							Math::Vector3 _toTarget = _targetPos - a_trsArray[_i].pos;
							if (_toTarget.LengthSquared() > 1e-6f)
							{
								_toTarget.Normalize();

								// 見失う : 一度捉えたあとで見失う角度の外へ外れたら、以降は直進する
								const float _cos = _missile.dir.Dot(_toTarget);
								if (_isLoseEnabled && _cos >= _loseCos) _missile.isAcquired = true;
								if (_isLoseEnabled && _missile.isAcquired && _cos < _loseCos)
								{
									_missile.isLost = true;
								}
								else
								{
									_missile.dir = RotateToward(_missile.dir, _toTarget, _maxTurn);
								}
							}
						}

						const float _step = std::max(_res.acceleration, 0.0f) * a_ctx.dt;
						_missile.speed = (_missile.speed < _res.speed)
							? std::min(_missile.speed + _step, _res.speed)
							: std::max(_missile.speed - _step, _res.speed);
					}

					_vel.value = _missile.dir * _missile.speed;
				}
			}
		)
		.ReadsResource<InstanceResource::SwarmMissileResource, InstanceResource::SwarmContactDamageResource>();

		//--------------------------------------------------------------------------
		// 自爆 : プレイヤーに近づいたら(か時間切れで)ダメージを積み、自分も落とす
		//--------------------------------------------------------------------------
		a_world.ActiveTask<Component::SwarmMissileComponent, const Component::LocalTransformComponent, const Component::HealthComponent>(
			Engine::ECS::ESystemType::Update,
			"SwarmMissileExplodeSystem",
			[](
				Engine::ECS::Chunk*               a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				Component::SwarmMissileComponent*            a_missileArray,
				const Component::LocalTransformComponent*    a_trsArray,
				const Component::HealthComponent*            a_healthArray
			)
			{
				auto& _world = *a_ctx.pWorld;
				if (!_world.HasResource<InstanceResource::SwarmMissileResource>()) return;
				if (!_world.HasResource<InstanceResource::SwarmContactDamageResource>()) return;
				if (!_world.HasResource<InstanceResource::HitEventResource>()) return;

				const InstanceResource::SwarmMissileResource& _res = _world.GetResource<InstanceResource::SwarmMissileResource>();
				const InstanceResource::SwarmContactDamageResource& _player = _world.GetResource<InstanceResource::SwarmContactDamageResource>();
				InstanceResource::HitEventResource& _hitEvents = _world.RefResource<InstanceResource::HitEventResource>();

				const bool _isFoundPlayer = _player.isActive && _player.player != Engine::ECS::Limits::INVALID_ENTITY;
				const float _hitDistance   = std::max(_res.explodeRadius, 0.0f) + _player.playerRadius;
				const float _hitDistanceSq = _hitDistance * _hitDistance;

				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::SwarmMissileComponent& _missile = a_missileArray[_i];
					const Component::HealthComponent& _health = a_healthArray[_i];
					if (_missile.isExploded || _health.isDead) continue;

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];
					const Math::Vector3 _pos = a_trsArray[_i].pos;

					//--------------------------------------------------------------
					// プレイヤーに届いたか
					//--------------------------------------------------------------
					bool _isHit = false;
					if (_isFoundPlayer)
					{
						const Math::Vector3 _closest =
							ClosestPointOnSegment(_pos, _player.playerSegmentA, _player.playerSegmentB);
						const Math::Vector3 _toPlayer = _closest - _pos;
						const float _distSq = _toPlayer.LengthSquared();

						if (_distSq < _hitDistanceSq)
						{
							_isHit = true;

							// 受けた向き(プレイヤーから見て押される向き) = ミサイルからプレイヤーへ
							Math::Vector3 _dir = _toPlayer;
							if (_distSq > 1e-8f) _dir /= std::sqrt(_distSq);
							else _dir = _missile.dir;

							InstanceResource::HitEvent _event = {};
							_event.attacker = _self;
							_event.victim   = _player.player;
							_event.hitPos   = _closest - _dir * _player.playerRadius;	// カプセルの表面
							_event.hitDir   = _dir;
							_event.damage   = _res.damage;
							_event.type     = InstanceResource::EHitEventType::Explosion;
							_hitEvents.Push(_event);
						}
					}

					// 届かず、時間も残っているなら飛び続ける
					if (!_isHit && _missile.time < _res.lifeTime) continue;

					//--------------------------------------------------------------
					// 自分を落とす : 残りの体力ぶんのダメージを自分に積む。
					// 死亡状態へ入れるのも死亡エフェクトを出すのも HealthSystem 側に任せる
					// (撃ち落とされたときと同じ流れになり、ボスの体力もそこで1減る)
					//--------------------------------------------------------------
					InstanceResource::HitEvent _selfEvent = {};
					_selfEvent.attacker = _self;
					_selfEvent.victim   = _self;
					_selfEvent.hitPos   = _pos;
					_selfEvent.hitDir   = _missile.dir;
					_selfEvent.damage   = std::max(_health.currentHealth, 0.0f) + 1.0f;
					_selfEvent.type     = InstanceResource::EHitEventType::Explosion;
					_hitEvents.Push(_selfEvent);

					_missile.isExploded = true;
				}
			}
		)
		.ReadsResource<InstanceResource::SwarmMissileResource, InstanceResource::SwarmContactDamageResource>()
		.WritesResource<InstanceResource::HitEventResource>();
	}
}
