#include "HomingSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Weapon/Projectile/HomingComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Combat/AimPointComponent.h"

namespace App::System
{
	//==============================================================================
	// HomingSystem
	//
	// HomingComponent を持つ投射物(誘導弾)を、targetEntity へ向けて曲げる。
	// targetEntity は発射した瞬間に WeaponProjectileSpawnSystem / MissileFireSystem が埋める(敵なら索敵で見つけた
	// エンティティ、プレイヤーなら狙点が当たっている相手)。
	//
	// ・速度ベクトルの「向き」だけを回し、大きさ(弾速)はそのまま保つ。
	//   進む処理自体は PositionIntegrationSystem(Physics 帯)が行うので、
	//   ここは速度を書き換えるだけでよい。
	// ・PreUpdate 帯に置く理由
	//     「今フレームどっちへ進むか」を決めるだけなので、敵の行動決定
	//     (EnemyMoveIntentSystem)と同じ帯が素直。位置は前フレームの積分結果を読む。
	//     加えて、Update 帯に置くとシステムのソートが循環する。
	//     LockOnRotationSystem が Velocity を読んで LocalTransform を書き、
	//     こちらは LocalTransform を読んで Velocity を書くため、
	//     依存グラフ(コンポーネント単位・ワールド全体)が互いを指してしまう。
	//     弾とプレイヤーはアーキタイプが別で実際には競合しないが、
	//     グラフはそこまで見ないので帯を分けて避ける。
	// ・turnSpeed は 1 秒あたりの旋回角(ラジアン/秒)。この角度以上は曲がれないので、
	//   小さいほど大回り、大きいほど食いつく。0 以下なら誘導しない(直進)。
	// ・searchRange は「その距離までなら追う」上限。離れすぎている間は曲げずに直進し、
	//   また範囲に入れば追い直す(ターゲットは捨てないので再追尾できる)。
	//   0 以下なら距離制限なし。
	// ・ターゲットが消滅した場合、シグネチャが空になって HasComponent が false になる。
	//   その時は targetEntity を無効値へ戻し、以降は直進させる。
	// ・自分の位置は LocalTransform を見る。弾は親を持たない(ワールド=ローカル)し、
	//   WorldMatrix は前フレームの PostUpdate の値なので、生成直後は入っていない。
	// ・追う先は相手の AimPointComponent の点(無ければ原点)。原点が足元の相手で
	//   足元へ刺さらないように、狙われる側が胴体の中心あたりを持っている。
	//==============================================================================
	void HomingSystem::Init(App::ECS::APPWorld& a_world)
	{
		// 自分のチャンクの値だけを書く(ほかのエンティティは RefData で読むだけ)ので、ワーカーで回す
		a_world.ActiveJobTask<Component::HomingComponent, Component::DesiredVelocityComponent, const Component::LocalTransformComponent>(
			Engine::ECS::ESystemType::PreUpdate,
			"HomingSystem",
			[](
				Engine::ECS::Chunk*      /*a_pChunk*/,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				Component::HomingComponent*                  a_homingArray,
				Component::DesiredVelocityComponent*                a_velArray,
				const Component::LocalTransformComponent*    a_trsArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::HomingComponent&               _homing = a_homingArray[_i];
					Component::DesiredVelocityComponent&             _vel    = a_velArray[_i];
					const Component::LocalTransformComponent& _trs    = a_trsArray[_i];

					// 曲がれないなら何もしない(直進)
					if (_homing.turnSpeed <= 0.0f) continue;

					//------------------------------------------------------
					// ターゲットの解決
					//   消滅していれば無効値へ戻して以降は直進させる
					//------------------------------------------------------
					if (_homing.targetEntity == Engine::ECS::Limits::INVALID_ENTITY) continue;

					if (!a_ctx.pWorld->HasComponent<Component::WorldMatrixComponent>(_homing.targetEntity))
					{
						_homing.targetEntity = Engine::ECS::Limits::INVALID_ENTITY;
						continue;
					}

					const auto* _pTargetWorld =
						a_ctx.pWorld->RefData<Component::WorldMatrixComponent>(_homing.targetEntity);
					if (!_pTargetWorld)
					{
						_homing.targetEntity = Engine::ECS::Limits::INVALID_ENTITY;
						continue;
					}

					const Math::Vector3 _targetPos = Component::GetAimPointPos(
						*a_ctx.pWorld, _homing.targetEntity, Math::Matrix(_pTargetWorld->worldMat));

					//------------------------------------------------------
					// 現在の進行方向(速度が無いと向きが決まらない)
					//------------------------------------------------------
					Math::Vector3 _velValue(_vel.value);
					float _speedSq = _velValue.LengthSquared();
					if (!(_speedSq > 1e-8f)) continue;

					float         _speed   = std::sqrt(_speedSq);
					Math::Vector3 _curDir  = _velValue / _speed;

					//------------------------------------------------------
					// ターゲットへの向き
					//------------------------------------------------------
					Math::Vector3 _toTarget = _targetPos - Math::Vector3(_trs.pos);
					float _distSq = _toTarget.LengthSquared();
					if (!(_distSq > 1e-6f)) continue;	// ほぼ重なっている

					float _dist = std::sqrt(_distSq);

					// 追える距離を超えている間は曲げない(範囲に戻れば再び追う)
					if (_homing.searchRange > 0.0f && _dist > _homing.searchRange) continue;

					_toTarget /= _dist;

					//------------------------------------------------------
					// 今フレームで曲がれる角度まで進行方向を回す
					//------------------------------------------------------
					float _maxStep = _homing.turnSpeed * a_ctx.dt;	// ラジアン
					float _cos     = std::clamp(_curDir.Dot(_toTarget), -1.0f, 1.0f);
					float _angle   = std::acos(_cos);

					Math::Vector3 _newDir = _toTarget;
					if (_angle > _maxStep)
					{
						// 回転軸 = 現在の向き × 目標の向き
						Math::Vector3 _axis      = _curDir.Cross(_toTarget);
						float         _axisLenSq = _axis.LengthSquared();

						if (_axisLenSq > 1e-8f)
						{
							_axis /= std::sqrt(_axisLenSq);
						}
						else
						{
							// ちょうど真後ろ。軸が作れないので直交する適当な軸で回し始める
							Math::Vector3 _ref = (std::fabs(_curDir.y) > 0.99f)
								? Math::Vector3(1.0f, 0.0f, 0.0f)
								: Math::Vector3(0.0f, 1.0f, 0.0f);
							_axis = _curDir.Cross(_ref);
							_axis.Normalize();
						}

						Math::Quaternion _rot = Math::Quaternion::CreateFromAxisAngle(_axis, _maxStep);
						_newDir = Math::Vector3::Transform(_curDir, _rot);
						_newDir.Normalize();
					}

					// 弾速は変えず、向きだけ差し替える
					_vel.value = _newDir * _speed;
				}
			}
		)
		// 絞り込みに使わない読み : 追う相手の位置と狙う点を RefData で読む
		.Reads<Component::WorldMatrixComponent, Component::AimPointComponent>();
	}
}
