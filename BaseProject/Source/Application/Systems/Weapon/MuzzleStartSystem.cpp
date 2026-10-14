#include "MuzzleStartSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Model/Model.h"

#include "Application/Components/Weapon/WeaponConfig/MuzzleComponent.h"
#include "Application/Components/Render/ModelComponent.h"

namespace App::System
{
	void MuzzleStartSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.StartTask<Component::MuzzleComponent>(
			Engine::ECS::ESystemType::Start,
			"MuzzleStartSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::StartTag* /*a_tag*/,
				Component::MuzzleComponent* a_muzzleArray
				)
			{
				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::MuzzleComponent& _muzzle = a_muzzleArray[_i];
					const Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					//==============================================================
					// 銃口ノードの番号を引く
					//--------------------------------------------------------------
					// 見つからなければ武器の原点から撃つ(isNodeValid が false のまま)
					//==============================================================
					_muzzle.isNodeValid = false;
					if (_muzzle.muzzlePtrNodeHash == 0) continue;

					const auto* _pModelComp = a_ctx.pWorld->RefData<Component::ModelComponent>(_self);
					if (!_pModelComp) continue;

					const auto* _pModel = a_ctx.pServices->pResourceManager->Get(_pModelComp->handle);
					if (!_pModel) continue;

					const auto& _nodeVec = _pModel->GetOriginalNodeVec();
					for (UINT _nodeIdx = 0; _nodeIdx < _nodeVec.size(); ++_nodeIdx)
					{
						if (_nodeVec[_nodeIdx].nodeNameHash != _muzzle.muzzlePtrNodeHash) continue;

						_muzzle.nodeIndex = _nodeIdx;
						_muzzle.isNodeValid = true;
						break;
					}

					if (!_muzzle.isNodeValid)
					{
						ENGINE_WARNING("[Muzzle] 銃口ノードがモデルに見つかりません(武器の原点から撃ちます)");
					}
				}
			}
		)
		// 絞り込みに使わない読み : 自分のモデル
		.Reads<Component::ModelComponent>();
	}
}
