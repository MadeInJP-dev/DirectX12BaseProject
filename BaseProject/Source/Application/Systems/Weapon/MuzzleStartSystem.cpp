#include "MuzzleStartSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Model/Model.h"

#include "Application/Components/Weapon/WeaponConfig/MuzzleComponent.h"
#include "Application/Components/Render/ModelComponent.h"
#include "Application/Components/Effect/EffectAssetComponent.h"

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
					// マズルフラッシュの再生枠を武器自身へ持たせる
					//--------------------------------------------------------------
					// 撃つたびにエフェクト用のエンティティを生やすと、そのエンティティは
					// エフェクトが終わるまでワールドに残る。移動しながら撃つと
					// 撃った位置に取り残されて尾を引いて見える。
					//
					// 武器自身に枠を1つ持たせ、撃つたびに頭から再生し直す形にする
					// (MuzzleFlashSystem が EffectPlayer::Play を直接呼ぶ)。
					// 枠は EffectAssetComponent。進行も発生・描画もこれを見ているので、付けるだけで動く。
					//==============================================================
					if (_muzzle.muzzleEffect.guid != Core::DEFAULT_GUID)
					{
						if (a_ctx.pWorld->HasComponent<Component::EffectAssetComponent>(_self))
						{
							// すでに別の用途で付いている武器からは奪わない。
							// 奪うと元の演出が止まるので、気付けるように知らせる
							ENGINE_WARNING(
								"[Muzzle] EffectAssetComponent を既に持っているため"
								"マズルフラッシュを設定できません");
						}
						else
						{
							// 渡すのは設定だけ。ハンドル(自分のぶんの参照)は EffectFixupSystem が GUID から取る。
							// 大きさは撃つたびに MuzzleFlashSystem が入れる
							Component::EffectAssetComponent _effectComp = {};
							_effectComp.effectGUID = _muzzle.muzzleEffect.guid;
							_effectComp.playOnStart = false;		// 撃つまでは出さない
							_effectComp.destroyOnFinish = false;	// 枠は武器と一緒に消えるまで残す

							const auto _typeID = a_ctx.pWorld->GetCompTypeID<Component::EffectAssetComponent>();
							if (_typeID != Engine::ECS::Limits::INVALID_COMPONENTTYPEID)
							{
								a_ctx.pWorld->ReserveAddComponent(
									_typeID, _self, reinterpret_cast<uint8_t*>(&_effectComp));
							}
						}
					}

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
		// 絞り込みに使わない読み : 自分のモデルと、マズルフラッシュの枠をもう持っているか
		.Reads<Component::ModelComponent, Component::EffectAssetComponent>();
	}
}
