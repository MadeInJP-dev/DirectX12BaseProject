#include "BoosterEffectSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Effect/BoosterEffectComponent.h"
#include "Application/Components/Effect/EffectPlayRequestComponent.h"
#include "Application/Components/Effect/EffectOverrideComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Utility/EffectSpawnHelper.h"

namespace App::System
{
	//==========================================================================================
	// BoosterEffectSystem
	//
	// ブースターの「置き方」と「大きさの変化」を、噴射エフェクトへ渡す。
	// あわせて、ブーストダッシュを踏み込んだ瞬間のスパークを出す。
	//
	// ・エフェクトアセットは GUID 単位で共有されるので、取り付け位置と向きの個体差は
	//   アセットには書けない。BoosterEffectComponent が持っているものを、
	//   毎フレーム EffectAssetComponent の上書き欄へ写して渡す。
	//
	// ・大きさは2つを掛け合わせて決める。
	//     (1) 吹かした瞬間の膨らみ
	//         点火(isPlay の立ち上がり)とダッシュの踏み込みで burstScale まで跳ね上げ、
	//         burstTime かけて baseScale へ戻す。
	//     (2) ブーストダッシュ中の太らせ
	//         ダッシュしている間だけ boostScale 倍。こちらは持続する。
	//   (1) だけだと踏み込みの一瞬しか差が出ず、飛んでいる間は歩いている時と
	//   同じ絵になる。(2) だけだと切り替わりが平坦で踏み込みの手応えが出ない。
	//     (3) チャージダッシュ
	//         溜めている間は溜まり具合ぶん太らせ(chargeScale)、撃ち出している間は
	//         束の長さを伸ばす(dashLengthScale)。太さと長さで分けているのは、
	//         「溜まっている」と「もう出た」を一目で区別させるため。
	//   点火/消火とダッシュ中かを決めるのは ThrusterEffectSystem(PreUpdate)なので、
	//   ここは Update 帯に置いてそのフレームの値を見る。
	//   実際に出すのは EffectDrawSystem(Draw)なので、書いた値はそのフレームに間に合う。
	//
	// ・戻し方を「毎フレーム一定割合で減らす」ではなく残り時間の線形にしているのは、
	//   burstTime にそのまま「何秒で戻るか」が出ていた方が調整しやすいため。
	//   ダッシュ中かの行き来も同じ理由で boostBlendTime の線形にしている
	//   (入り切りをそのまま出すとジェットが1フレームで跳ねる)。
	//
	// ・スパークはジェットとは別のエフェクトなので、ジェットの大きさをいじるのではなく
	//   噴射口の位置へ単発のエフェクトエンティティを出す。
	//   ジェットは出っぱなしで太さしか変えられないため、「瞬間的に速度が上がった」ことを
	//   見せるには、絵の違うものが一度だけ弾ける方が伝わる。
	//   出したものは destroyOnFinish で自分から消えるので後片付けは要らない。
	//==========================================================================================
	void BoosterEffectSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<Component::BoosterEffectComponent, const Component::EffectPlayRequestComponent, Component::EffectOverrideComponent>(
			Engine::ECS::ESystemType::Update,
			"BoosterEffectSystem",
			[](
				Engine::ECS::Chunk*      a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				Component::BoosterEffectComponent*           a_boosterArray,
				const Component::EffectPlayRequestComponent* a_requestArray,
				Component::EffectOverrideComponent*          a_overrideArray
			)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::BoosterEffectComponent& _booster = a_boosterArray[_i];
					const Component::EffectPlayRequestComponent& _request  = a_requestArray[_i];
					Component::EffectOverrideComponent&          _override = a_overrideArray[_i];

					//--------------------------------------------------------------
					// 噴射する向き
					//
					// 手で入れた値は長さがまちまちなので、ここで揃えておかないと
					// 受け側(EffectDrawSystem)で行列を掛けたときに長さが効いてしまう。
					// 0 ベクトルのときは触らない(既定の向きのまま出す)。
					// スパークを出す向きにも使うので、先に求めておく
					//--------------------------------------------------------------
					Math::Vector3 _dir = _booster.emitDir;
					const bool _hasDir = (_dir.LengthSquared() > 1e-8f);
					if (_hasDir)
					{
						_dir.Normalize();
					}

					//--------------------------------------------------------------
					// 点火の立ち上がりで膨らませる
					//--------------------------------------------------------------
					const bool _isPlaying = _request.isPlay;

					if (_isPlaying && !_booster.wasPlaying)
					{
						_booster.burstTimer = _booster.burstTime;
					}
					_booster.wasPlaying = _isPlaying;

					//--------------------------------------------------------------
					// ブーストダッシュの踏み込み
					//
					// 立ち上がりで膨らみを入れ直し、スパークを1回だけ出す。
					// 移動しながらダッシュに入った場合は噴射がすでに出ているので、
					// 点火の側では拾えない。ここで入れ直すことで
					// 「歩き出し」と「踏み込み」のどちらでも同じ手応えが出る
					//--------------------------------------------------------------
					const bool _isBoosting = _booster.isBoosting;
					const bool _justBoosted = _isBoosting && !_booster.wasBoosting;
					_booster.wasBoosting = _isBoosting;

					if (_justBoosted)
					{
						_booster.burstTimer = _booster.burstTime;
					}

					// 時間を進める。消火中も戻しきってしまってよい
					// (次に点火したときはどうせ入れ直すため)
					if (_booster.burstTimer > 0.0f)
					{
						_booster.burstTimer = (std::max)(0.0f, _booster.burstTimer - a_ctx.dt);
					}

					//--------------------------------------------------------------
					// ダッシュ中かの行き来 : 0 = 通常 / 1 = ブースト中
					//--------------------------------------------------------------
					const float _boostTarget = _isBoosting ? 1.0f : 0.0f;
					if (_booster.boostBlendTime > 0.0f)
					{
						const float _step = a_ctx.dt / _booster.boostBlendTime;
						if (_booster.boostBlend < _boostTarget)
						{
							_booster.boostBlend = (std::min)(_boostTarget, _booster.boostBlend + _step);
						}
						else
						{
							_booster.boostBlend = (std::max)(_boostTarget, _booster.boostBlend - _step);
						}
					}
					else
					{
						_booster.boostBlend = _boostTarget;
					}

					//--------------------------------------------------------------
					// 大きさ : 膨らみ(burstScale -> baseScale)に、ダッシュ中の倍率を掛ける
					//--------------------------------------------------------------
					float _t = 0.0f;	// 1 = 吹かした瞬間 / 0 = 戻りきった
					if (_booster.burstTime > 0.0f)
					{
						_t = std::clamp(_booster.burstTimer / _booster.burstTime, 0.0f, 1.0f);
					}

					float _scale =
						_booster.baseScale + (_booster.burstScale - _booster.baseScale) * _t;

					// ダッシュ中の太らせは倍率で乗せる。
					// 足し算にすると baseScale を変えたときに効き具合が変わってしまう
					_scale *= 1.0f + (_booster.boostScale - 1.0f) * _booster.boostBlend;

					//--------------------------------------------------------------
					// チャージダッシュの溜め : 溜まり具合そのままに太らせる
					//
					// ここだけ時間で均していないのは、元の値(chargeRate)が
					// すでに 0 から 1 へ時間をかけて上がってくるものだから。
					// 均すと溜まりきってからも膨らみ続けて、撃ち出しと重なってしまう
					//--------------------------------------------------------------
					const float _chargeRate = std::clamp(_booster.chargeRate, 0.0f, 1.0f);
					_scale *= 1.0f + (_booster.chargeScale - 1.0f) * _chargeRate;

					_override.effectScale = _scale;

					//--------------------------------------------------------------
					// チャージダッシュの撃ち出し : 束の長さを伸ばす
					//
					// 太さ(effectScale)ではなく長さ(effectLengthScale)を動かすのは、
					// 溜めの側がすでに太さを使っているため。撃ち出しでさらに太らせると
					// 溜めの続きにしか見えず、前へ飛び出した感じが出ない
					//--------------------------------------------------------------
					const float _lengthTarget = _booster.isChargeDashing ? 1.0f : 0.0f;
					if (_booster.dashLengthBlendTime > 0.0f)
					{
						const float _lengthStep = a_ctx.dt / _booster.dashLengthBlendTime;
						if (_booster.dashLengthBlend < _lengthTarget)
						{
							_booster.dashLengthBlend =
								(std::min)(_lengthTarget, _booster.dashLengthBlend + _lengthStep);
						}
						else
						{
							_booster.dashLengthBlend =
								(std::max)(_lengthTarget, _booster.dashLengthBlend - _lengthStep);
						}
					}
					else
					{
						_booster.dashLengthBlend = _lengthTarget;
					}

					_override.effectLengthScale =
						1.0f + (_booster.dashLengthScale - 1.0f) * _booster.dashLengthBlend;

					//--------------------------------------------------------------
					// 置き方 : 噴射口の位置と向きを渡す
					//--------------------------------------------------------------
					_override.isOverrideTransform = true;
					_override.overridePosOffset   = _booster.posOffset;

					if (_hasDir)
					{
						_override.overrideEmitDir = _dir;
					}

					//--------------------------------------------------------------
					// 踏み込んだ瞬間のスパーク
					//
					// 噴射口のワールド位置へ、噴射と同じ向きで単発のエフェクトを出す。
					// WorldMatrixComponent はクエリに入れず RefData で引く。
					// クエリに足すと、行列を持たないブースターが丸ごと外れて
					// 上の置き方・大きさまで書かれなくなるため。
					//
					// RefData は持っていないコンポーネントなら nullptr を返す
					//--------------------------------------------------------------
					if (!_justBoosted) continue;

					const bool _hasSpark = (_booster.sparkEffectGUID != Core::DEFAULT_GUID);
					const bool _hasBlaze = (_booster.blazeEffectGUID != Core::DEFAULT_GUID);
					if (!_hasSpark && !_hasBlaze) continue;

					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];
					if (!a_ctx.pWorld->HasComponent<Component::WorldMatrixComponent>(_self)) continue;

					const auto* _pWorldMat = a_ctx.pWorld->RefData<Component::WorldMatrixComponent>(_self);
					if (!_pWorldMat) continue;

					const Math::Matrix _ownerWorld(_pWorldMat->worldMat);

					// 位置は噴射口(posOffset)をワールドへ、向きは噴射の向きをワールドへ。
					// 向きが決まっていないときは 0 ベクトルのまま渡して、
					// アセットのパーツが持っている向きに任せる
					const Math::Vector3 _sparkPos =
						Math::Vector3::Transform(_booster.posOffset, _ownerWorld);

					Math::Vector3 _sparkDir = {};
					if (_hasDir)
					{
						_sparkDir = Math::Vector3::TransformNormal(_dir, _ownerWorld);
					}

					// 反復中なので即時生成はされない。実体化は次の BeginFrame
					if (_hasSpark)
					{
						App::Utility::ReserveSpawnEffectAt(
							*a_ctx.pWorld,
							_booster.sparkEffectGUID,
							_sparkPos,
							true,
							_sparkDir,
							_booster.sparkScale);
					}

					//--------------------------------------------------------------
					// 炎の噴き上がりは、ブースターに付けたまま出す
					//
					// 踏み込んだ瞬間は機体が一気に加速するので、その場に置くと
					// 炎だけが後ろへ置き去りになり、機体を包んで見えない。
					// 付けた側は親の拡縮を継がないので、噴射口の位置には親のスケールを掛けておく
					//--------------------------------------------------------------
					if (_hasBlaze)
					{
						Math::Vector3 _ownerScale;
						Math::Quaternion _ownerRot;
						Math::Vector3 _ownerPos;
						Math::Matrix _ownerMat = _ownerWorld;
						if (!_ownerMat.Decompose(_ownerScale, _ownerRot, _ownerPos))
						{
							_ownerScale = Math::Vector3(1.0f, 1.0f, 1.0f);
						}

						App::Utility::ReserveSpawnAttachedEffect(
							*a_ctx.pWorld,
							_booster.blazeEffectGUID,
							_self,
							_booster.posOffset * _ownerScale,
							_hasDir ? _dir : Math::Vector3{},
							_booster.blazeScale);
					}
				}
			}
		)
		// 順序 : エフェクト(EffectAsset)の書き手を EffectUpdate → BoosterEffect → GunShoot → Ballistic の順に並べる
		.After("EffectUpdateSystem")
		// 絞り込みに使わない読み : スパークの位置
		.Reads<Component::WorldMatrixComponent>();
	}
}
