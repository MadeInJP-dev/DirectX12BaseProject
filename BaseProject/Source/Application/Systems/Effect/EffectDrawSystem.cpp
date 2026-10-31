#include "EffectDrawSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Particle/ParticleBufferManager.h"
#include "Engine/Graphics/LightManager/LightManager.h"
#include "Engine/Effect/EffectPlayer.h"
#include "Engine/Effect/Trail/TrailNodePool.h"
#include "Engine/Graphics/Particle/GPU/EmitterSlotPool/EmitterSlotPool.h"

#include "Application/Components/Effect/EffectRuntimeComponent.h"
#include "Application/Components/Effect/EffectOverrideComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"

namespace App::System
{
	//==========================================================================================
	// EffectDrawSystem
	//
	// EffectUpdateSystem が決めたぶんを、実際に出す。
	//
	//   パーティクル : パーツごとの pendingEmit 個を GPU へ emit 要求する
	//   メッシュ     : パーツごとの行列と色を組んで描画命令に積む
	//   トレイル     : パーツごとの今の位置を、借りている点の履歴(TrailNode)へ渡す。
	//                  帯を組んで描くのは GraphicsEngine と TransparentPass
	//
	// どちらも発生源はエフェクトが付いているエンティティのワールド行列。
	// パーツ側は「そこからどうずらすか」しか持たないので、
	// 同じエフェクトを別の場所・別の相手に付け回せる。
	//==========================================================================================
	void EffectDrawSystem::Init(App::ECS::APPWorld& a_world)
	{
		// 発生源の席を取って持つので、EffectRuntimeComponent は書き込み
		a_world.ActiveTask<Component::EffectRuntimeComponent, const Component::EffectOverrideComponent, const Component::WorldMatrixComponent>(
			Engine::ECS::ESystemType::Draw,
			"EffectDrawSystem",
			[]
			(
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				Component::EffectRuntimeComponent* a_runtimeArray,
				const Component::EffectOverrideComponent* a_overrideArray,
				const Component::WorldMatrixComponent* a_worldMatArray
				)
			{
				auto* _pResourceManager = a_ctx.pServices->pResourceManager;
				auto* _pMainEngine = a_ctx.pServices->pMainEngine;
				if (!_pResourceManager || !_pMainEngine) return;

				auto* _pGE = _pMainEngine->RefGraphicsEngine();
				auto* _pParticleManager = _pGE->RefParticleManager();
				auto* _pSlotPool = _pParticleManager ? _pParticleManager->RefEmitterSlotPool() : nullptr;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::EffectRuntimeComponent& _runtime = a_runtimeArray[_i];
					const Component::EffectOverrideComponent& _override = a_overrideArray[_i];

					auto* _pEffect = _pResourceManager->Ref(_runtime.effectHandle);
					if (!_pEffect) continue;

					const Math::Matrix _ownerWorld(a_worldMatArray[_i].worldMat);
					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					//----------------------------------------------------------
					// 発生源の席(ローカル空間のパーティクル用)
					//
					// エフェクト1つにつき1席。Local のパーツが1つでもあれば取る。
					// 行列は再生中かどうか・今フレーム出すかどうかに関係なく毎フレーム書く。
					// 出したフレームだけ書くと、
					//   ・発生レートが fps より低いと、出さないフレームに行列が古いまま描かれてガタつく
					//   ・止めた後の残り粒が、最後に出した位置に置き去りになる
					// 返すのはエンティティが消えるとき(EffectRuntimeComponent の Release)
					//----------------------------------------------------------
					if (_pSlotPool)
					{
						bool _hasLocalPart = false;
						for (const auto& _part : _pEffect->GetParticleParts())
						{
							if (!_part.IsValid()) continue;

							// パーツの上書き(Local / World)が先、無ければパーティクルアセットの設定
							if (_part.IsLocalSimulation(_pResourceManager->Get(_part.particleHandle)))
							{
								_hasLocalPart = true;
								break;
							}
						}

						if (_hasLocalPart && !_pSlotPool->IsValid(_runtime.emitterSlot))
						{
							_runtime.emitterSlot = _pSlotPool->Acquire();
						}

						_pSlotPool->SetTransform(_runtime.emitterSlot, _ownerWorld);
					}

					// 再生中か、止めている最中(OnStop の火花などを出している)なら出す。
					// 止まりきったものが借りたままのライトは、ここで返す(点きっぱなしにしない)
					if (!_runtime.instance.IsActive())
					{
						if (_pGE && _pGE->RefLightManager())
						{
							_runtime.instance.ReleaseLights(*_pGE->RefLightManager());
						}

						// トレイルも手放す。残りの点は寿命で消えるまで描かれる
						if (_pGE && _pGE->RefTrailNodePool())
						{
							_runtime.instance.ReleaseTrails(*_pGE->RefTrailNodePool());
						}
						continue;
					}

					//----------------------------------------------------------
					// 出す側からの上書きを、行列1つにまとめておく(置き場)
					//
					// アセットは共有なので、取り付け位置・向き・大きさの個体差は
					// コンポーネント側(EffectOverrideComponent)から受け取る。
					//   v * Scale * PlaceRot * Translate * ownerWorld
					// の順で掛けると「オーナーのローカル空間で、指定位置に指定の向きで置き、そこを中心に拡縮」になる。
					// パーツの位置と向きは、この置き場から見た相対になる。
					// パーティクルの発生位置もメッシュパーツもこの1つで済む
					//----------------------------------------------------------
					const float _effectScale = (_override.effectScale > 0.0f) ? _override.effectScale : 1.0f;

					// 束の長さの倍率。粒の初速へ掛けるので、寿命が同じなら
					// 飛ぶ距離＝噴射の長さがそのまま倍率ぶん伸びる。
					// 太さ(_effectScale)とは別物なので、掛ける先も分けてある
					const float _lengthScale = (_override.effectLengthScale > 0.0f) ? _override.effectLengthScale : 1.0f;

					//----------------------------------------------------------
					// 置き場の回転 : 上書きの向き(持ち主の座標系)を +Z にした回転
					//
					// 以前は上書きの向きでパーツの向きを丸ごと差し替えていたので、
					// 向きの違うパーツを並べたエフェクトに上書きを掛けると、全部が同じ向きになっていた。
					// 今は置き場ごと回すので、パーツ同士の向きの関係は崩れない
					// (パーツの向きが +Z なら、上書きの向きにそのまま一致する)。
					// 上書きが無ければ回さない
					//----------------------------------------------------------
					Math::Matrix _placeRot = {};
					if (_override.isOverrideTransform)
					{
						_placeRot = Engine::Graphics::Particle::MakeEmitMatrix(
							Math::Vector3(0.0f, 0.0f, 0.0f),
							Math::Vector3(_override.overrideEmitDir),
							Math::Vector3(0.0f, 1.0f, 0.0f));
					}

					Math::Matrix _effectWorld = _ownerWorld;
					if (_override.isOverrideTransform || _effectScale != 1.0f)
					{
						_effectWorld =
							Math::Matrix::CreateScale(_effectScale) *
							_placeRot *
							Math::Matrix::CreateTranslation(_override.overridePosOffset) *
							_ownerWorld;
					}

					//----------------------------------------------------------
					// パーティクル用 : 置き場(持ち主の座標系の中) と、持ち主から拡縮を落とした行列
					//
					// パーティクルの発生位置・向きには持ち主の拡縮を掛けない。
					// 粒の大きさ・速さ・散らばりにも掛けておらず、ローカルで回す粒の席も拡縮を落としてあるので、
					// それに揃えて「出す瞬間」は Local / World のどちらでも同じ位置・向きになるようにしてある。
					// (メッシュとライトのパーツは物なので、上の _effectWorld で持ち主の拡縮ごと置く)
					//----------------------------------------------------------
					const Math::Matrix _placeMat =
						Math::Matrix::CreateScale(_effectScale) *
						_placeRot *
						Math::Matrix::CreateTranslation(_override.overridePosOffset);
					const Math::Matrix _ownerRT = Engine::Graphics::Particle::EmitterSlotPool::StripScale(_ownerWorld);

					//----------------------------------------------------------
					// パーティクル
					//----------------------------------------------------------
					if (_pParticleManager)
					{
						const auto& _particleParts = _pEffect->GetParticleParts();
						const size_t _partCount =
							std::min<size_t>(_particleParts.size(), Engine::Resource::EFFECT_PARTICLE_MAX);

						for (size_t _p = 0; _p < _partCount; ++_p)
						{
							const auto& _part = _particleParts[_p];

							// このフレームの発生数に、個体ごとのパラメータの倍率を掛ける(結び付けが無ければ 1)
							const float* _pParams = _override.params;
							const float _countScale = _pEffect->EvaluateParamScale(
								Engine::Resource::EEffectParamTarget::ParticleEmitCount, _p, _pParams);
							const int _emitCount = static_cast<int>(
								std::lround(static_cast<float>(_runtime.instance.pendingEmit[_p]) * (std::max)(_countScale, 0.0f)));
							if (_emitCount <= 0) continue;

							// パーティクルアセットが引けなければ出しようがない
							auto* _pParticle = _pResourceManager->Get(_part.particleHandle);
							if (!_pParticle) continue;

							//--------------------------------------------------
							// 発生源(位置・噴き出す向き・上の手がかり)を決める
							//
							// パーツの位置と向きは置き場から見た値。それを「粒を保存する空間」へ移す。
							//   Local : 席(持ち主から拡縮を落とした行列)の座標系。ワールドへ戻すのは描画時(ParticleVS)
							//   World : ワールド。席と同じ「拡縮を落とした持ち主」を掛ける
							// 掛ける行列が違うだけで式は同じなので、出す瞬間の位置と向きは一致する。
							// 違うのは、出したあとに発生源へ追従するかどうかだけ。
							//
							// 上の手がかりは、噴き出す向きを軸にした回転(ロール)を決める。
							// 板を発生源に合わせる向き(EmitterAxis / EmitterFacing)で効く
							//--------------------------------------------------
							const bool _isLocal = _part.IsLocalSimulation(_pParticle);

							// 席が取れていなければ 0(単位行列)になり、ワールド空間として出る
							UINT _emitterIndex = 0;
							if (_isLocal)
							{
								_emitterIndex = Engine::Graphics::Particle::EmitterSlotPool::ToGPUIndex(_runtime.emitterSlot);
							}

							// 持ち主の座標系 → 粒を保存する空間(Local は席の座標系のままなので単位行列)
							Math::Matrix _ownerToSim = Math::Matrix::Identity();
							if (_emitterIndex == 0)
							{
								_ownerToSim = _ownerRT;
							}

							const Math::Matrix _placeToSim    = _placeMat * _ownerToSim;	// 位置用(エフェクトの拡縮込み)
							const Math::Matrix _placeRotToSim = _placeRot * _ownerToSim;	// 向き用(長さは MakeEmitMatrix が揃える)

							Math::Vector3 _pos;
							Math::Vector3 _dir;
							const Math::Vector3 _up = Math::Vector3::TransformNormal(Math::Vector3(0.0f, 1.0f, 0.0f), _placeRotToSim);

							switch (_part.space)
							{
							case Engine::Resource::EEffectSpace::WorldMatrix:
								// 置き場の原点と前方向(+Z)
								_pos = _placeToSim.Translation();
								_dir = Math::Vector3::TransformNormal(Math::Vector3(0.0f, 0.0f, 1.0f), _placeRotToSim);
								break;

							case Engine::Resource::EEffectSpace::ReverseVelocity:
							{
								// 進行方向の逆へ吹く(噴射・排気)。
								// 弾やミサイルは見た目の姿勢が進行方向と一致しないので、
								// 行列の軸ではなく実際の速度から向きを取る(置き場の回転も使わない)。
								// DesiredVelocityComponent はこのクエリに含めない
								// (持たないエンティティのエフェクトまで止まってしまうため)
								_pos = Math::Vector3::Transform(Math::Vector3(_part.posOffset), _placeToSim);

								// 速度はワールドの向き
								Math::Vector3 _worldDir;

								// RefData は持っていないコンポーネントなら nullptr を返す
								if (a_ctx.pWorld->HasComponent<Component::DesiredVelocityComponent>(_self))
								{
									if (const auto* _pVel = a_ctx.pWorld->RefData<Component::DesiredVelocityComponent>(_self))
									{
										_worldDir = -Math::Vector3(_pVel->value);
									}
								}

								// 止まっている(または速度を持たない)ときは後ろ向き＝持ち主の +Z の逆
								if (_worldDir.LengthSquared() <= 1e-8f)
								{
									_worldDir = -Math::Vector3(_ownerRT._31, _ownerRT._32, _ownerRT._33);
								}

								// ローカルで回す粒なら、席の座標系へ戻す。
								// 席は拡縮を落とした行列なので、回転の逆は転置で済む
								_dir = (_emitterIndex != 0)
									? Math::Vector3::TransformNormal(_worldDir, _ownerRT.Transpose())
									: _worldDir;
								break;
							}

							case Engine::Resource::EEffectSpace::LocalOffset:
							default:
								// 置き場を基準に、パーツのオフセット位置・向きを合成
								_pos = Math::Vector3::Transform(Math::Vector3(_part.posOffset), _placeToSim);
								_dir = Math::Vector3::TransformNormal(Math::Vector3(_part.emitDir), _placeRotToSim);
								break;
							}

							//--------------------------------------------------
							// エミットデータ構築
							// 散らばり方はエフェクト側、速度と寿命と板の回転はパーティクルアセット側
							//--------------------------------------------------
							Engine::Graphics::Particle::EmitterData _emitData = {};

							// 発生行列と、その回転(粒の板の向きに使う)を一緒に入れる。
							// 形状はシェーダーがローカル(+Z が噴き出す向き)で作って、この行列を掛ける。
							// 方向の正規化と 0 ベクトルの安全策もここが持つ
							Engine::Graphics::Particle::SetEmitTransform(_emitData, _pos, _dir, _up);
							_emitData.emitCount     = static_cast<UINT>(_emitCount);

							// 大きさとばらつき半径も一緒に拡縮する。
							// 粒だけ大きくして散らばりが元のままだと、束が太らずに粒が重なるだけになる
							_emitData.baseScale      = _part.baseScale * _effectScale *
								_pEffect->EvaluateParamScale(Engine::Resource::EEffectParamTarget::ParticleSize, _p, _pParams);
							_emitData.positionRadius = _part.positionRadius * _effectScale;
							_emitData.positionExtents = _part.positionExtents * _effectScale;
							_emitData.directionAngle = DirectX::XMConvertToRadians(_part.directionAngle);
							_emitData.emitShape      = static_cast<UINT>(_part.emitShape);
							_emitData.emitterIndex   = _emitterIndex;
							_emitData.minScale       = _part.minScale;
							_emitData.maxScale       = _part.maxScale;

							// 初速だけ長さの倍率で伸ばす。寿命は触らないので、
							// 束は同じ濃さのまま前へ伸びる(寿命側を伸ばすと尾を引いて残る)
							// 個体ごとのパラメータの倍率(ParticleSpeed)も初速に掛ける
							const float _speedScale = _lengthScale *
								_pEffect->EvaluateParamScale(Engine::Resource::EEffectParamTarget::ParticleSpeed, _p, _pParams);
							_emitData.minSpeed    = _pParticle->GetInitalSpeedMin() * _speedScale;
							_emitData.maxSpeed    = _pParticle->GetInitalSpeedMax() * _speedScale;
							_emitData.minLifeTime = _pParticle->GetLifeTimeMin();
							_emitData.maxLifeTime = _pParticle->GetLifeTimeMax();

							// 板の回転(アセットは度で持っている)
							_emitData.minRotation        = DirectX::XMConvertToRadians(_pParticle->GetRotationMin());
							_emitData.maxRotation        = DirectX::XMConvertToRadians(_pParticle->GetRotationMax());
							_emitData.minAngularVelocity = DirectX::XMConvertToRadians(_pParticle->GetAngularVelocityMin());
							_emitData.maxAngularVelocity = DirectX::XMConvertToRadians(_pParticle->GetAngularVelocityMax());

							_pParticleManager->ReserveEmit(_part.particleHandle, _emitData);
						}
					}

					//----------------------------------------------------------
					// メッシュ
					//----------------------------------------------------------
					if (_pGE)
					{
						const auto& _meshParts = _pEffect->GetMeshParts();

						for (size_t _m = 0; _m < _meshParts.size(); ++_m)
						{
							auto* _pModel = _pResourceManager->Get(_meshParts[_m].modelHandle);
							if (!_pModel) continue;

							// 今出している時間帯かどうかも含めて、アセット側が組んでくれる
							Math::Matrix  _meshWorld;
							Math::Color   _colorScale;
							Math::Vector3 _emissiveAdd;
							if (!Engine::Effect::EffectPlayer::BuildMeshDraw(
								*_pEffect, _m, _runtime.instance, _effectWorld,
								_meshWorld, _colorScale, _emissiveAdd))
							{
								continue;
							}

							// 個体ごとのパラメータの倍率(結び付けが無ければ 1)。
							// 大きさはメッシュ自身の原点を中心に掛ける
							const float _meshScale = _pEffect->EvaluateParamScale(
								Engine::Resource::EEffectParamTarget::MeshScale, _m, _override.params);
							if (_meshScale != 1.0f)
							{
								_meshWorld = Math::Matrix::CreateScale(_meshScale) * _meshWorld;
							}
							_emissiveAdd *= _pEffect->EvaluateParamScale(
								Engine::Resource::EEffectParamTarget::MeshEmissive, _m, _override.params);

							// エミッシブテクスチャの倍率(emissiveScale)は素通し
							Engine::Graphics::ModelSubmitDesc _desc = {};
							_desc.pWorld = a_ctx.pWorld;
							_desc.modelHandle = _meshParts[_m].modelHandle;
							_desc.worldMat = _meshWorld;
							_desc.colorScale = _colorScale;
							_desc.bloomAdd = _emissiveAdd;
							_pGE->RefDrawSubmitter()->SubmitModel(_desc);
						}
					}

					//----------------------------------------------------------
					// ライト
					//
					// 出している間だけポイントライトを借りて、毎フレーム値を書く。
					// 出す時間帯から外れたら返す(点きっぱなしにしない)。
					// 返し損ねたものはエンティティが消えるとき(EffectRuntimeComponent の Release)に返る
					//----------------------------------------------------------
					if (_pGE && _pGE->RefLightManager())
					{
						auto* _pLightManager = _pGE->RefLightManager();

						for (size_t _l = 0; _l < Engine::Resource::EFFECT_POINTLIGHT_MAX; ++_l)
						{
							auto& _lightHandle = _runtime.instance.lightHandles[_l];

							Engine::Graphics::PointLight _light = {};
							const bool _isShow = Engine::Effect::EffectPlayer::BuildLightDraw(
								*_pEffect, _l, _runtime.instance, _effectWorld, _effectScale, _light);

							if (!_isShow)
							{
								if (_lightHandle.IsValid())
								{
									_pLightManager->RemoveLight(_lightHandle);
									_lightHandle = {};
								}
								continue;
							}

							if (!_lightHandle.IsValid())
							{
								_lightHandle = _pLightManager->AllocatePL();
								if (!_lightHandle.IsValid()) continue;	// 上限まで点いている
							}

							if (auto* _pLight = _pLightManager->RefLight(_lightHandle))
							{
								*_pLight = _light;
							}
						}
					}

					//----------------------------------------------------------
					// トレイル
					//
					// 引いている間だけ点の履歴(TrailNode)を借りて、毎フレーム今の位置を渡す。
					// 引く時間帯から外れたら手放す : 点はプールに残って尾から消えていき、
					// 次に引き始めたときは新しい帯になる(前の帯と繋がらない)
					//----------------------------------------------------------
					if (_pGE && _pGE->RefTrailNodePool())
					{
						auto* _pTrailPool = _pGE->RefTrailNodePool();

						for (size_t _t = 0; _t < Engine::Resource::EFFECT_TRAIL_MAX; ++_t)
						{
							auto& _trailHandle = _runtime.instance.trailHandles[_t];

							Math::Vector3 _trailPos;
							Engine::Effect::TrailStyle _style = {};
							const bool _isShow = Engine::Effect::EffectPlayer::BuildTrailPoint(
								*_pEffect, _t, _runtime.instance, _effectWorld, _effectScale, _trailPos, _style);

							if (!_isShow)
							{
								if (_trailHandle.IsValid()) _pTrailPool->Detach(_trailHandle);
								continue;
							}

							if (!_trailHandle.IsValid())
							{
								_trailHandle = _pTrailPool->Acquire();
							}

							if (auto* _pNode = _pTrailPool->Ref(_trailHandle))
							{
								_pNode->SetStyle(_style);
								_pNode->AddPoint(_trailPos);
							}
						}
					}
				}
			}
		);
	}
}
