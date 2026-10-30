#include "WeaponMuzzleSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Model/Model.h"

#include "Application/Components/Weapon/WeaponFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/MuzzleComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Combat/AimResultComponent.h"
#include "Application/Components/Render/ModelComponent.h"
#include "Application/Components/Combat/TargetEntityComponent.h"
#include "Application/Components/Combat/AimPointComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"

namespace App::System
{
	namespace WeaponMuzzle
	{
		Math::Vector3 GetMuzzleLocalPos(
			const Engine::Resource::ResourceManager& a_resourceManager,
			const Component::MuzzleComponent& a_muzzle,
			const Component::ModelComponent* a_pModel)
		{
			Math::Vector3 _localPos = { 0.0f, 0.0f, 0.0f };

			// ノードの番号は MuzzleStartSystem がハッシュから引いてある
			if (a_muzzle.isNodeValid && a_pModel)
			{
				if (const auto* _pModel = a_resourceManager.Get(a_pModel->handle))
				{
					const auto& _nodeVec = _pModel->GetOriginalNodeVec();
					if (a_muzzle.nodeIndex < _nodeVec.size())
					{
						const Math::Matrix& _nodeMat = _nodeVec[a_muzzle.nodeIndex].worldTransform;
						_localPos = { _nodeMat._41, _nodeMat._42, _nodeMat._43 };
					}
				}
			}

			return _localPos + a_muzzle.offset;
		}
	}

	namespace
	{
		//======================================================================================
		// 索敵で捉えている相手の狙ってほしい点を引く(敵が撃つとき)
		//
		// 敵は狙点(AimResultComponent)を持たないので、銃口の向きのまま撃つと
		// 機体の向き(左右だけ回る)で水平に飛び、上下にいる相手へ向かない。
		// 自分 → 親 と辿って TargetEntityComponent を探し、捉えている相手の
		// AimPointComponent の点(無ければ原点)を返す。見失っている間は使わない。
		//======================================================================================
		bool FindTargetAimPos(Engine::ECS::World& a_world, Engine::ECS::Entity a_self, Math::Vector3& a_outPos)
		{
			// 親を辿る深さの上限。親子が循環していても止まるように付けておく
			constexpr int MAX_DEPTH = 8;

			Engine::ECS::Entity _entity = a_self;
			for (int _d = 0; _d < MAX_DEPTH; ++_d)
			{
				if (_entity == Engine::ECS::Limits::INVALID_ENTITY) break;

				if (const auto* _pTarget = a_world.RefData<Component::TargetEntityComponent>(_entity))
				{
					const Engine::ECS::Entity _targetEntity = _pTarget->targetEntity;
					if (!_pTarget->isFind || _targetEntity == Engine::ECS::Limits::INVALID_ENTITY) return false;
					if (!a_world.HasComponent<Component::WorldMatrixComponent>(_targetEntity)) return false;

					const auto* _pTargetWorld = a_world.RefData<Component::WorldMatrixComponent>(_targetEntity);
					if (!_pTargetWorld) return false;

					a_outPos = Component::GetAimPointPos(a_world, _targetEntity, Math::Matrix(_pTargetWorld->worldMat));
					return true;
				}

				// 親へ
				const auto* _pHierarchy = a_world.RefData<Component::HierarchyComponent>(_entity);
				if (!_pHierarchy) break;
				_entity = _pHierarchy->parentID;
			}

			return false;
		}
	}

	void WeaponMuzzleSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveJobTask<Component::WeaponFireComponent, const Component::MuzzleComponent, const Component::WorldMatrixComponent>(
			Engine::ECS::ESystemType::Update,
			"WeaponMuzzleSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				Component::WeaponFireComponent* a_fireArray,
				const Component::MuzzleComponent* a_muzzleArray,
				const Component::WorldMatrixComponent* a_worldMatArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					Component::WeaponFireComponent& _fire = a_fireArray[_i];
					if (!_fire.isFired) continue;

					const Component::MuzzleComponent& _muzzle = a_muzzleArray[_i];
					const Math::Matrix _m = a_worldMatArray[_i].worldMat;
					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					//======================================================================
					// 基準の向き
					//----------------------------------------------------------------------
					// 狙点があるときは「狙いの向き(カメラ前方)」を基準にする。
					// 武器はアニメーションノードに追従していて、武器の軸が銃身の向きとは限らないため。
					// 無いときは銃口の向き(武器ローカルの dir)を使う。
					//
					// AimResultComponent は AimTargetSystem が計算し、
					// AttachmentDispatchSystem が親から配信してくる
					//======================================================================
					const Component::AimResultComponent* _pAim = a_ctx.pWorld->RefData<Component::AimResultComponent>(_self);

					// まだ一度も計算されていない(=原点が入っている)なら使わない
					if (_pAim && !_pAim->isValid) _pAim = nullptr;

					Math::Vector3 _baseDir = Math::Vector3::TransformNormal(_muzzle.dir, _m);
					const float _baseLenSq = _baseDir.LengthSquared();
					_baseDir = (_baseLenSq > 1e-8f) ? (_baseDir / std::sqrt(_baseLenSq)) : Math::Vector3(_m._31, _m._32, _m._33);

					if (_pAim)
					{
						const Math::Vector3 _aimDir = Math::Vector3(_pAim->dir);
						const float _aimDirLenSq = _aimDir.LengthSquared();
						if (_aimDirLenSq > 1e-8f) _baseDir = _aimDir / std::sqrt(_aimDirLenSq);
					}

					//======================================================================
					// 発射位置 : 銃口(ノード + offset)を武器のワールド行列でワールドへ
					//======================================================================
					const Math::Vector3 _muzzleLocalPos = WeaponMuzzle::GetMuzzleLocalPos(
						*a_ctx.pServices->pResourceManager, _muzzle, a_ctx.pWorld->RefData<Component::ModelComponent>(_self));
					const Math::Vector3 _spawnPos = Math::Vector3::Transform(_muzzleLocalPos, _m);

					//======================================================================
					// 射出方向 : 銃口から狙点へ向ける
					//======================================================================
					Math::Vector3 _shootDir = _baseDir;
					if (_pAim)
					{
						Math::Vector3 _toTarget = Math::Vector3(_pAim->pos) - _spawnPos;

						// 狙点が銃口とほぼ同じ位置だと向きが定まらないので、その時は基準のまま
						if (_toTarget.LengthSquared() > 1e-6f)
						{
							_toTarget.Normalize();

							// 狙点が真後ろにある場合は採用しない。
							// (自機の手前の物を拾ってしまった時に、弾がカメラへ向かって
							//  飛んでいくのを防ぐための保険。基準は必ず狙いの向き)
							if (_toTarget.Dot(_baseDir) > 0.0f)
							{
								_shootDir = _toTarget;
							}
						}
					}
					else
					{
						// 狙点の無い撃ち手(敵)は、索敵で捉えている相手へ上下も含めて向ける
						Math::Vector3 _targetPos;
						if (FindTargetAimPos(*a_ctx.pWorld, _self, _targetPos))
						{
							Math::Vector3 _toTarget = _targetPos - _spawnPos;
							if (_toTarget.LengthSquared() > 1e-6f)
							{
								_toTarget.Normalize();
								_shootDir = _toTarget;
							}
						}
					}

					_fire.spawnPos       = _spawnPos;
					_fire.shootDir       = _shootDir;
					_fire.muzzleLocalPos = _muzzleLocalPos;
				}
			}
		)
		// 順序 : 撃つと決まった武器(撃ち方のシステムが書く)だけを埋める
		.After({ "AutoFireSystem", "BurstFireSystem", "SemiAutoFireSystem" })
		// 絞り込みに使わない読み : 自分の狙点(AttachmentDispatchSystem が配る)とモデル
		.Reads<Component::AimResultComponent, Component::ModelComponent>()
		// 狙点の無い撃ち手 : 親を辿った索敵結果と、捉えている相手の位置・狙われる点
		.Reads<Component::TargetEntityComponent, Component::HierarchyComponent, Component::AimPointComponent>();
	}
}
