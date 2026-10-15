#include "BossCombatIntentSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Boss/BossParamsComponent.h"
#include "Application/Components/Combat/TargetEntityComponent.h"
#include "Application/Components/Movement/LookAngleComponent.h"
#include "Application/Components/Combat/AimResultComponent.h"
#include "Application/Components/Movement/BoostParamsComponent.h"
#include "Application/Components/Movement/BoostStateComponent.h"
#include "Application/Components/Attachment/AttachmentSlotsComponent.h"
#include "Application/Components/Weapon/Hand/WeaponStateComponent.h"
#include "Application/Components/Movement/MoveIntentComponent.h"
#include "Application/Components/Combat/ActionIntentComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Movement/ActualVelocityComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"
#include "Application/Components/Physics/GroundStateComponent.h"


namespace App::System
{
	//==========================================================================================
	// BossCombatIntentSystem
	//
	// 人型ボスの行動を、プレイヤーの入力とまったく同じ形(視点角 + 移動入力 + ブースト入力 +
	// 発射入力 + 狙点)へ落とす。プレイヤー側の InputMoveSystem / InputActionSystem に相当し、
	// 「人の代わりにスティックとボタンを操作するもの」だと思えばよい。
	//
	// そのため動かす側は既存のシステムがそのまま使える。
	//   LookAngle    → RotationSystem(機体の向き) / AdditivePoseSystem(上体の狙い)
	//                  / RobotBoostSystem(ブーストの向き) / CharacterMovementSystem(移動の基準軸)
	//   MoveIntent   → CharacterMovementSystem(視点基準 → 目標速度)
	//   BoostIntentComponent → RobotBoostSystem(推力)
	//   ActionIntent → AttachmentDispatchSystem 経由で銃(子エンティティ)へ → 撃ち方のシステム(WeaponFireModeSystem)
	//   AimTargetPos → 同上。銃はこの点へ向けて撃つ
	// ボス用に増やしたのはこのシステムと BossMissileSalvoSystem の2つだけ。
	//
	// ・戦闘は距離ではなく命令で始まる
	//     BossParamsComponent::isCombatStarted が立つまで、その場で待機する(入力を全部 0 にする)。
	//     命令を出すのは SceneSequence の BossOrder。単体で動きを見たいときは
	//     BossParamsComponent::startOnSpawn を立てておけば命令なしで始まる。
	//
	// ・機動(アーマードコア/オメガフェニックス風)
	//     間合いは keepDistance ± keepMargin の幅で保つ。遠ければ詰め、近すぎれば下がり、
	//     幅の内では前後に動かない。その間ずっと横へ流し続け(strafe)、向きは相手に固定する。
	//     高さも keepHeight ± heightMargin で保つので、地面を歩かず空中に居座る。
	//     さらに dashInterval ごとにブーストの単押し(クイックブースト)を入れて、
	//     速度が一定にならないようにしている。
	//
	// ・行動パターン(EBossPattern)
	//     patternDuration ごとに重み付き抽選で選び直す「今回はどう戦うか」。
	//     懐へ飛び込む(Rush)・上を取る(HighGround)・下から来る(LowGround)・
	//     回り込む(Orbit)・離れてミサイル(Retreat)。
	//
	//     パターンが差し替えるのは “どこに居たいか” と癖(横の強さ・切り返し/ダッシュの
	//     間隔・射撃の傾向)だけで、そこへ行く手順は上の共通処理のまま。位置取りの目標を
	//     すげ替えるだけで別の戦い方に見えるので、パターンごとに移動処理を書かなくて済む。
	//     同じパターンが連続しないように抽選するので、待ち構えても読みが外れる。
	//
	// ・偏差撃ち
	//     狙点は「相手の今の位置」ではなく「弾が届く頃の位置」。相手の実速度(ActualVelocityComponent)と
	//     銃の弾速から先を読む。撃ち合いとして成立させるための最低限で、aimLeadScale = 0 に
	//     すれば置き撃ちなしに戻せる。
	//
	// ・PreUpdate に置く理由
	//     索敵結果(SearchPlayerSystem が書く TargetEntityComponent)を読むので同じ帯に居る。
	//     ActionIntentComponent を書く側なので、それを読む AttachmentDispatchSystem より
	//     自動的に前へ回る(RAW 依存)。狙点も同じタイミングで書けば同じフレームで子へ届く。
	//==========================================================================================
	namespace
	{
		// 角度(度)を -180..180 へ畳む。視点角は加算し続けるので大きな値になり得る
		float WrapDeg180(float a_deg)
		{
			a_deg = std::fmod(a_deg + 180.0f, 360.0f);
			if (a_deg < 0.0f) a_deg += 360.0f;
			return a_deg - 180.0f;
		}

		// 現在角から目標角へ、1フレームぶんの上限つきで寄せる(度)
		float MoveTowardDeg(float a_current, float a_target, float a_maxDelta)
		{
			const float _diff = WrapDeg180(a_target - a_current);
			if (a_maxDelta <= 0.0f) return a_current + _diff;
			return a_current + std::clamp(_diff, -a_maxDelta, a_maxDelta);
		}

		// 間隔 ± 揺らぎ。同じ周期で動くと読まれてしまうので散らす
		// (抽選そのものは Math::Random に寄せてある)
		float NextInterval(float a_base, float a_random)
		{
			return std::max(Math::Random::Deviation(a_base, a_random), 0.05f);
		}

		//======================================================================================
		// 行動パターンの中身
		//--------------------------------------------------------------------------------------
		// パターンが差し替えるのは「どこに居たいか」と癖だけ。移動そのものの手順は
		// パターンによらず共通なので、ここでは目標と倍率を返すだけにしてある。
		//======================================================================================
		struct PatternProfile
		{
			float distance;				// 保ちたい間合い(m)
			float marginScale;			// 間合いの許容幅の倍率(小さいほどきっちり詰める)
			float height;				// 相手からの高さ(m。負で下)
			float strafeScale;			// 横移動の強さ倍率
			float strafeIntervalScale;	// 切り返し間隔の倍率(大きいほど同じ向きへ流し続ける)
			float dashIntervalScale;	// クイックブースト間隔の倍率(小さいほど頻繁)
			float gunRangeScale;		// 銃を撃ち始める距離の倍率
			float missileIntervalScale;	// 一斉射の間隔の倍率(小さいほど多く撒く)
			bool  canHold;				// 横の切り返しで足を止めてよいか
		};

		PatternProfile MakeProfile(const Component::BossParamsComponent& a_boss, Component::EBossPattern a_pattern)
		{
			switch (a_pattern)
			{
			case Component::EBossPattern::Rush:
				// 懐へ飛び込む。横へ逃げずに真っ直ぐ詰めたいので切り返しは長め、
				// 代わりにクイックブーストを頻繁に入れて直線的に見せない
				// 止まらない : 飛び込むこと自体が狙いなので、途中で足を止めると意味が無くなる
				return { a_boss.rushDistance, 0.5f, a_boss.rushHeight, 0.6f, 1.5f, 0.45f, 1.0f, 1.0f, false };

			case Component::EBossPattern::HighGround:
				// 上を取って撃ち下ろす。距離は少し詰めて、ミサイルを多めに撒く
				return { a_boss.keepDistance * 0.8f, 1.0f, a_boss.highGroundHeight,
						 1.0f, 1.0f, 0.8f, 1.0f, 0.5f, true };

			case Component::EBossPattern::LowGround:
				// 低く潜り込んで撃ち上げる。横の動きを強めて的を絞らせない
				return { a_boss.keepDistance * 0.7f, 1.0f, a_boss.lowGroundHeight,
						 1.3f, 0.8f, 0.7f, 1.0f, 1.0f, true };

			case Component::EBossPattern::Orbit:
				// 間合いを保ったまま同じ向きへ回り込む。切り返しをほとんど挟まない
				return { a_boss.keepDistance, 1.0f, a_boss.keepHeight,
						 a_boss.orbitStrafeScale, 3.0f, 0.6f, 1.0f, 1.0f, true };

			case Component::EBossPattern::Retreat:
				// 大きく離れてミサイル主体。銃は届かない距離なので撃たせない
				// 止まらない : 距離を取るための動きなので、途中で止まると離れきれない
				return { a_boss.retreatDistance, 1.5f, a_boss.keepHeight + 8.0f,
						 0.8f, 1.2f, 1.2f, 0.5f, 0.5f, false };

			case Component::EBossPattern::Standoff:
			default:
				return { a_boss.keepDistance, 1.0f, a_boss.keepHeight, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, true };
			}
		}

		//======================================================================================
		// 行動パターンの抽選
		//--------------------------------------------------------------------------------------
		// 重みつきのくじ引き。まず「今と違うパターン」だけで引き、そこで引けなければ
		// (重みが1つしか立っていない等)全体から引き直す。
		// 同じパターンが連続すると、詰めてくると分かった状態がそのまま続いてしまうため。
		//======================================================================================
		Component::EBossPattern PickPattern(const Component::BossParamsComponent& a_boss, Component::EBossPattern a_current)
		{
			constexpr int COUNT = static_cast<int>(Component::EBossPattern::Max);

			const float _weights[COUNT] =
			{
				a_boss.weightStandoff,
				a_boss.weightRush,
				a_boss.weightHighGround,
				a_boss.weightLowGround,
				a_boss.weightOrbit,
				a_boss.weightRetreat,
			};

			for (int _pass = 0; _pass < 2; ++_pass)
			{
				// _pass == 0 : 今のパターンを除いて引く / _pass == 1 : 全部から引く
				const bool _isExcludeCurrent = (_pass == 0);

				float _total = 0.0f;
				for (int _i = 0; _i < COUNT; ++_i)
				{
					if (_weights[_i] <= 0.0f) continue;
					if (_isExcludeCurrent && _i == static_cast<int>(a_current)) continue;

					_total += _weights[_i];
				}
				if (_total <= 0.0f) continue;

				float _roll = Math::Random::Float(0.0f, _total);

				for (int _i = 0; _i < COUNT; ++_i)
				{
					if (_weights[_i] <= 0.0f) continue;
					if (_isExcludeCurrent && _i == static_cast<int>(a_current)) continue;

					_roll -= _weights[_i];
					if (_roll <= 0.0f) return static_cast<Component::EBossPattern>(_i);
				}
			}

			// 重みが全部 0。動きが止まらないよう既定へ倒す
			return Component::EBossPattern::Standoff;
		}
	}

	void BossCombatIntentSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<
			const Component::TargetEntityComponent,
			const Component::LocalTransformComponent,
			const Component::AttachmentSlotsComponent,
			const Component::BossParamsComponent,
			Component::BossBrainStateComponent,
			Component::BossCommandComponent,
			Component::LookAngleComponent,
			Component::MoveIntentComponent,
			Component::ActionIntentComponent,
			Component::BoostIntentComponent,
			const Component::BoostParamsComponent,
			const Component::BoostStateComponent,
			Component::AimResultComponent>(
			Engine::ECS::ESystemType::PreUpdate,
			"BossCombatIntentSystem",
			[](
				Engine::ECS::Chunk*      a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				const Component::TargetEntityComponent*      a_targetArray,
				const Component::LocalTransformComponent*    a_trsArray,
				const Component::AttachmentSlotsComponent*   a_slotsArray,
				const Component::BossParamsComponent*        a_bossArray,
				Component::BossBrainStateComponent*          a_brainArray,
				Component::BossCommandComponent*             a_commandArray,
				Component::LookAngleComponent*               a_lookArray,
				Component::MoveIntentComponent*              a_moveIntentArray,
				Component::ActionIntentComponent*            a_actionIntentArray,
				Component::BoostIntentComponent*             a_boostArray,
				const Component::BoostParamsComponent*       a_boostParamsArray,
				const Component::BoostStateComponent*        a_boostStateArray,
				Component::AimResultComponent*            a_aimArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					const Component::TargetEntityComponent&    _target = a_targetArray[_i];
					const Component::LocalTransformComponent&  _trs    = a_trsArray[_i];
					const Component::AttachmentSlotsComponent& _slots  = a_slotsArray[_i];
					const Component::BossParamsComponent&      _boss   = a_bossArray[_i];
					Component::BossBrainStateComponent&        _brain  = a_brainArray[_i];
					Component::BossCommandComponent&           _command = a_commandArray[_i];
					Component::LookAngleComponent&             _look   = a_lookArray[_i];
					Component::MoveIntentComponent&            _intent = a_moveIntentArray[_i];
					Component::ActionIntentComponent&          _action = a_actionIntentArray[_i];
					Component::BoostIntentComponent&           _boost  = a_boostArray[_i];
					const Component::BoostParamsComponent&     _boostParams = a_boostParamsArray[_i];
					const Component::BoostStateComponent&      _boostState  = a_boostStateArray[_i];
					Component::AimResultComponent&          _aim    = a_aimArray[_i];

					// 命令を待たない設定なら、そのまま戦闘状態にしておく
					if (_boss.startOnSpawn) _command.isCombatStarted = true;

					//==========================================================
					// 相手の位置と実速度を引く
					//==========================================================
					const Engine::ECS::Entity _targetEntity = _target.targetEntity;

					bool          _hasTarget = false;
					Math::Vector3 _targetPos = {};
					Math::Vector3 _targetVel = {};

					if (_targetEntity != Engine::ECS::Limits::INVALID_ENTITY &&
						a_ctx.pWorld->HasComponent<Component::WorldMatrixComponent>(_targetEntity))
					{
						if (const auto* _pTargetWorld =
							a_ctx.pWorld->RefData<Component::WorldMatrixComponent>(_targetEntity))
						{
							_targetPos = Math::Matrix(_pTargetWorld->worldMat).Translation();
							_hasTarget = true;
						}
					}

					if (_hasTarget)
					{
						// 偏差撃ち用。加減速を通した実速度(ActualVelocityComponent)を優先し、
						// 持っていなければ目標速度で代用する
						if (a_ctx.pWorld->HasComponent<Component::ActualVelocityComponent>(_targetEntity))
						{
							if (const auto* _pMove = a_ctx.pWorld->RefData<Component::ActualVelocityComponent>(_targetEntity))
							{
								_targetVel = _pMove->value;
							}
						}
						else if (a_ctx.pWorld->HasComponent<Component::DesiredVelocityComponent>(_targetEntity))
						{
							if (const auto* _pVel = a_ctx.pWorld->RefData<Component::DesiredVelocityComponent>(_targetEntity))
							{
								_targetVel = _pVel->value;
							}
						}
					}

					//==========================================================
					// 戦闘前 / 相手なし : その場で待つ
					//----------------------------------------------------------
					// 入力を全部 0 にするだけでよい。移動も発射もこの入力を見ている
					// システムが担当しているので、ここで止めれば何も起きない。
					// 重力は生きているので、浮いていれば降りて着地する。
					//==========================================================
					if (!_command.isCombatStarted || !_hasTarget)
					{
						_brain.maneuver         = Component::EBossManeuver::Wait;
						_command.isMissileRequest = false;
						_brain.distance         = _hasTarget ? _target.distance : 0.0f;

						// 戦闘に入った最初のフレームでパターンを引き直させる
						_brain.patternTimer    = 0.0f;
						_brain.strafeHoldTimer = 0.0f;

						_intent.value = {};

						_action.isLeftWeaponShoot  = false;
						_action.isRightWeaponShoot = false;
						_action.isMissileHold      = false;
						_action.isLeftMissileHold = false;

						_boost.isBoostIntent = false;
						_boost.isBoostTriger = false;
						continue;
					}

					const Math::Vector3 _selfPos(_trs.pos);

					//==========================================================
					// 行動パターンの抽選
					//----------------------------------------------------------
					// 一定時間ごとに「今回はどう戦うか」を引き直す。
					// 切り替えた瞬間は横の向きも選び直して、前のパターンの流れを引きずらせない。
					//==========================================================
					_brain.patternTimer -= a_ctx.dt;
					if (_brain.patternTimer <= 0.0f)
					{
						_brain.pattern      = PickPattern(_boss, _brain.pattern);
						_brain.patternTimer =
							NextInterval(_boss.patternDuration, _boss.patternDurationRand);

						_brain.strafeSign      = Math::Random::Sign();
						_brain.strafeTimer     = 0.0f;
						_brain.strafeHoldTimer = 0.0f;	// 前のパターンの静止は引きずらない
					}

					const PatternProfile _profile = MakeProfile(_boss, _brain.pattern);

					//==========================================================
					// 狙点(偏差撃ち)
					//----------------------------------------------------------
					// 弾が届くまでの時間だけ相手の未来位置を読む。
					// 弾速は左手の武器(無ければ右手)の WeaponStateComponent から引く(武器ごとに変わるため)。
					//==========================================================
					Math::Vector3 _aimPos = _targetPos;
					_aimPos.y += _boss.aimOffsetY;

					const float _distance = (_aimPos - _selfPos).Length();
					_brain.distance = _distance;

					float _bulletSpeed = 0.0f;
					{
						auto _readSpeed = [&](Engine::ECS::Entity a_weapon)
						{
							if (_bulletSpeed > 0.0f) return;
							if (a_weapon == Engine::ECS::Limits::INVALID_ENTITY) return;
							if (!a_ctx.pWorld->HasComponent<Component::WeaponStateComponent>(a_weapon)) return;
							if (const auto* _pWeapon = a_ctx.pWorld->RefData<Component::WeaponStateComponent>(a_weapon))
							{
								_bulletSpeed = _pWeapon->bulletSpeed;
							}
						};
						_readSpeed(_slots.leftWeapon.id);
						_readSpeed(_slots.rightWeapon.id);
					}

					if (_bulletSpeed > 0.0f && _boss.aimLeadScale > 0.0f)
					{
						const float _leadTime = (_distance / _bulletSpeed) * _boss.aimLeadScale;
						_aimPos += _targetVel * _leadTime;
					}

					// 狙いの向き。潰れたら今向いている方向のままにする
					Math::Vector3 _aimDir = _aimPos - _selfPos;
					const float   _aimLenSq = _aimDir.LengthSquared();
					if (_aimLenSq > 1e-6f) _aimDir /= std::sqrt(_aimLenSq);
					else                   _aimDir = { 0.0f, 0.0f, 1.0f };

					// AimTargetSystem(カメラのレイ)の代わりに自分で書く。
					// これで AttachmentDispatchSystem が銃へ配り、撃ち方のシステムが狙点へ撃つ
					_aim.pos       = _aimPos;
					_aim.dir       = _aimDir;
					_aim.hitEntity = Engine::ECS::Limits::INVALID_ENTITY;
					_aim.isHit     = false;
					_aim.isValid   = true;

					//==========================================================
					// 視点角を狙点へ寄せる
					//----------------------------------------------------------
					// 左手系 +Z 前方なので Yaw = atan2(x, z)。Pitch は上向きが正
					// (AdditivePoseSystem が符号を反転して使う側の規約に合わせている)。
					//==========================================================
					const float _desiredYaw   = DirectX::XMConvertToDegrees(std::atan2(_aimDir.x, _aimDir.z));
					const float _pitchLimit   = std::min(_boss.maxPitchDeg, _look.maxPitch);
					const float _desiredPitch = std::clamp(
						DirectX::XMConvertToDegrees(std::asin(std::clamp(_aimDir.y, -1.0f, 1.0f))),
						-_pitchLimit, _pitchLimit);

					_look.Yaw   = MoveTowardDeg(_look.Yaw, _desiredYaw, _boss.turnSpeedDeg * a_ctx.dt);
					_look.Pitch = MoveTowardDeg(_look.Pitch, _desiredPitch, _boss.pitchSpeedDeg * a_ctx.dt);
					_look.Pitch = std::clamp(_look.Pitch, -_look.maxPitch, _look.maxPitch);

					//==========================================================
					// 前後 : 間合いを幅で保つ
					//----------------------------------------------------------
					// 高さは別に見るので、前後の判断は水平距離で行う。
					// 3D距離で見ると、真上に居るだけで「近すぎる」と判断してしまう。
					//==========================================================
					Math::Vector3 _flat = _targetPos - _selfPos;
					_flat.y = 0.0f;
					const float _horizontalDist = _flat.Length();

					const float _margin = _boss.keepMargin * _profile.marginScale;
					const float _far    = _profile.distance + _margin;
					const float _near   = std::max(_profile.distance - _margin, 0.0f);

					float _forward = 0.0f;
					if (_horizontalDist > _far)
					{
						_forward       = _boss.approachThrottle;
						_brain.maneuver = Component::EBossManeuver::Approach;
					}
					else if (_horizontalDist < _near)
					{
						_forward       = -_boss.backThrottle;
						_brain.maneuver = Component::EBossManeuver::Back;
					}
					else
					{
						_brain.maneuver = Component::EBossManeuver::Keep;
					}

					//==========================================================
					// 横 : 一定時間ごとに向きを切り替えて流し続ける
					//----------------------------------------------------------
					// 切り返しの間隔はパターン任せ。回り込み(Orbit)は長く、
					// 詰め(Rush)は真っ直ぐ行きたいので実質切り返さない。
					//==========================================================
					// 足を止めている残り時間を進める
					_brain.strafeHoldTimer = std::max(_brain.strafeHoldTimer - a_ctx.dt, 0.0f);

					_brain.strafeTimer -= a_ctx.dt;
					if (_brain.strafeTimer <= 0.0f)
					{
						// たまに同じ向きへ続けて、切り返しの周期を読ませない
						if (Math::Random::Bool()) _brain.strafeSign = -_brain.strafeSign;
						if (_brain.strafeSign == 0.0f) _brain.strafeSign = 1.0f;

						_brain.strafeTimer = NextInterval(
							_boss.strafeInterval * _profile.strafeIntervalScale,
							_boss.strafeIntervalRand);

						//------------------------------------------------------
						// 折り返しの瞬間だけ足を止める
						//------------------------------------------------------
						// 横へ振り続けているだけだと狙いを置く先が定まらず、当てるのが
						// ほぼ運になってしまう。折り返しは元々速度が乗っていない場所なので、
						// ここで止めれば動きとして不自然にならず、狙える隙になる。
						// 毎回止まると読まれるので、止まるかどうかも時間も抽選する。
						//------------------------------------------------------
						if (_profile.canHold && Math::Random::Chance(_boss.strafeHoldChance))
						{
							const float _minTime = std::max(_boss.strafeHoldTimeMin, 0.0f);
							const float _maxTime = std::max(_boss.strafeHoldTimeMax, _minTime);

							_brain.strafeHoldTimer = Math::Random::Float(_minTime, _maxTime);

							// 止まっている間に次の切り返しが来ないよう、待ち時間を積んでおく
							_brain.strafeTimer += _brain.strafeHoldTimer;
						}
					}

					// 止まっている間は横にも前後にも動かない(高度だけは保つ)
					const bool _isHolding = (_brain.strafeHoldTimer > 0.0f);
					if (_isHolding) _brain.maneuver = Component::EBossManeuver::Hold;

					const float _side = _isHolding
						? 0.0f
						: _brain.strafeSign * _boss.strafeThrottle * _profile.strafeScale;

					//==========================================================
					// 上下 : パターンが決めた高さに居座る
					//----------------------------------------------------------
					// ずれの大きさに比例させる。段階的に ±1 を入れると、境界で
					// 上昇と降下を往復して細かく震えてしまう。
					//==========================================================
					const float _heightError = (_targetPos.y + _profile.height) - _selfPos.y;

					float _up = 0.0f;
					if (std::fabs(_heightError) > _boss.heightMargin)
					{
						const float _range = std::max(_boss.heightMargin, 0.01f);
						_up = std::clamp(_heightError / (_range * 2.0f), -1.0f, 1.0f) * _boss.verticalThrottle;
					}

					//----------------------------------------------------------
					// 地面に着いている間は下へ押し込まない
					//----------------------------------------------------------
					// ブーストの向きは正規化されるので、下向き成分を入れたぶんだけ水平の
					// 推力が削られる。相手が地上に居るときの低空パターン(LowGround)は
					// 地面より下を目標にするので、そのままだと地面に押し付けたまま
					// 横へ動けなくなってしまう。
					//
					// 接地は GroundStateComponent(RayCollisionSystem が Physics 帯で書く)で見る。
					// 足元のレイを持たないボスもあり得るので、クエリには足さずに持っているときだけ引く
					// (クエリに足すとアーキタイプが狭まり、持たないボスが丸ごと動かなくなる)。
					// 以前は StateMachineComponent に同居していて、PlayerIntentSystem がそれを
					// 書き込み扱いにしていたため、読みを宣言すると依存が一周していた。
					//----------------------------------------------------------
					if (_up < 0.0f)
					{
						const Engine::ECS::Entity _self = a_pChunk->entityData[_i];
						if (a_ctx.pWorld->HasComponent<Component::GroundStateComponent>(_self))
						{
							if (const auto* _pGround = a_ctx.pWorld->RefData<Component::GroundStateComponent>(_self))
							{
								if (_pGround->isGround) _up = 0.0f;
							}
						}
					}

					// 視点基準の移動入力(プレイヤーのスティック入力と同じ意味)
					_intent.value.x = _side;
					_intent.value.y = _up;
					_intent.value.z = _isHolding ? 0.0f : _forward;

					//==========================================================
					// ブースト
					//----------------------------------------------------------
					// 余力があるうちは吹かしっぱなしで飛び回り、間隔ごとに単押しを
					// 混ぜてクイックブーストをかける(初動だけ tapBoostScale が乗る)。
					// 残量が予備を切ったら休んで回復させる。
					//==========================================================
					// 静止中は吹かさない。
					// ブーストは進みたい向きへ速度を差し替えるので、吹かしたままだと止まれない。
					// (水平入力が0のときは視線方向へ飛んでしまうので、切っておく必要がある)
					const bool _canBoost =
						!_isHolding &&
						_boostState.currentFuel > (_boostParams.boostFuel + _boss.boostFuelReserve);

					_boost.isBoostIntent = _canBoost;
					_boost.isBoostTriger = false;

					_brain.dashTimer -= a_ctx.dt;
					if (_brain.dashTimer <= 0.0f)
					{
						if (_canBoost) _boost.isBoostTriger = true;

						// 頻度もパターン任せ。詰めや回り込みでは短くして踏み込みを増やす
						_brain.dashTimer = NextInterval(
							_boss.dashInterval * _profile.dashIntervalScale,
							_boss.dashIntervalRand);
					}

					//==========================================================
					// 銃 : 撃つ/休むを交互に。正面に入っていない間は撃たない
					//==========================================================
					_brain.gunTimer -= a_ctx.dt;
					if (_brain.gunTimer <= 0.0f)
					{
						_brain.isGunActive = !_brain.isGunActive;
						_brain.gunTimer    = std::max(
							_brain.isGunActive ? _boss.gunBurstTime : _boss.gunRestTime, 0.05f);
					}

					// 旋回が追いついていない間に撃つと明後日へ飛ぶので、正面に入るまで待つ
					const bool _isInCone =
						std::fabs(WrapDeg180(_desiredYaw - _look.Yaw)) <= _boss.gunConeDeg;

					// 撃ち始める距離もパターン任せ。離脱中(Retreat)は届かないので撃たない
					const float _gunRange = _boss.gunRange * _profile.gunRangeScale;

					// 左右の区別は付けない。ボスは両手ぶんの武器を同じリズムで撃つ
					const bool _shoot = _brain.isGunActive && _isInCone && (_distance <= _gunRange);
					_action.isLeftWeaponShoot  = _shoot;
					_action.isRightWeaponShoot = _shoot;
					// ミサイルは溜め撃ちではなく間隔で撃つので、この入力は使わない
					_action.isMissileHold      = false;
					_action.isLeftMissileHold = false;

					//==========================================================
					// ミサイル : 間隔ごとに一斉射を要求する
					//----------------------------------------------------------
					// 実際に撃つのは BossMissileSalvoSystem(PostUpdate)。発射位置に
					// ポッドの今フレームのワールド行列が要るため、あちらに任せている。
					// 射程外の間はタイマーを 0 で止めておき、入った瞬間に撃たせる。
					//==========================================================
					if (_brain.missileTimer > 0.0f)
					{
						_brain.missileTimer = std::max(_brain.missileTimer - a_ctx.dt, 0.0f);
					}

					if (_brain.missileTimer <= 0.0f &&
						!_command.isMissileRequest &&
						_distance <= _boss.missileRange)
					{
						_command.isMissileRequest = true;

						// 上を取ったときや離脱中はミサイル主体にしたいので、間隔もパターン任せ
						_brain.missileTimer = NextInterval(
							_boss.missileInterval * _profile.missileIntervalScale,
							_boss.missileIntervalRand);
					}
				}
			}
		)
		// 順序 : 移動入力・発射入力の書き手同士(対象はボスだけで重ならない)。敵の行動決定の後に置く
		.After({ "EnemyShootIntentSystem", "EnemyMoveIntentSystem", "CloseCombatIntentSystem" })
		// 絞り込みに使わない読み : 相手の位置と速度、武器の弾速、自分の接地を RefData で読む
		.Reads<Component::WorldMatrixComponent, Component::ActualVelocityComponent, Component::DesiredVelocityComponent, Component::WeaponStateComponent, Component::GroundStateComponent>();
	}
}
