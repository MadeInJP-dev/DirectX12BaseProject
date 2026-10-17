#include "MuzzleFlashSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Application/Components/Weapon/WeaponFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/MuzzleComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Effect/EffectAssetComponent.h"
#include "Application/Components/Effect/EffectRuntimeComponent.h"
#include "Engine/Effect/EffectPlayer.h"
#include "Application/Components/Effect/EffectPlayRequestComponent.h"
#include "Application/Components/Effect/EffectOverrideComponent.h"
#include "Engine/Graphics/Particle/GPU/EmitterSlotPool/EmitterSlotPool.h"

namespace App::System
{
	//==========================================================================================
	// MuzzleFlashSystem
	//
	// 撃った武器(WeaponFireComponent::isFired)の銃口へ、武器自身が持っている再生枠
	// (EffectAssetComponent と、その実行中の値)を頭から再生し直す。
	//
	// ・エフェクト用のエンティティは出さない。出すとそのエンティティは
	//   エフェクトが終わるまでワールドに残るので、移動しながら撃つと
	//   撃った位置に取り残されて尾を引いて見える。(枠は銃口の必須コンポーネント。中身を入れるのは MuzzleEffectFixupSystem)
	// ・置き方は毎発入れ直す。銃口ノードは変わらなくても、狙いの向きは撃つたびに変わるため。
	// ・弾が出なかったフレーム(プレハブ未設定など)では光らない。
	//   WeaponProjectileSpawnSystem が弾を出せなかったときに isFired を下ろすので、その後に読む。
	// ・エフェクトの Play を直接呼ぶのでメインスレッドで回す。
	//==========================================================================================
	void MuzzleFlashSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<const Component::MuzzleComponent, const Component::WeaponFireComponent, const Component::WorldMatrixComponent,
			const Component::EffectAssetComponent, Component::EffectRuntimeComponent, Component::EffectPlayRequestComponent, Component::EffectOverrideComponent>(
			Engine::ECS::ESystemType::Update,
			"MuzzleFlashSystem",
			[]
			(
				Engine::ECS::Chunk*,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*,
				const Component::MuzzleComponent* a_muzzleArray,
				const Component::WeaponFireComponent* a_fireArray,
				const Component::WorldMatrixComponent* a_worldMatArray,
				const Component::EffectAssetComponent*,
				Component::EffectRuntimeComponent* a_runtimeArray,
				Component::EffectPlayRequestComponent* a_requestArray,
				Component::EffectOverrideComponent* a_overrideArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					const Component::WeaponFireComponent& _fireComp = a_fireArray[_i];
					if (!_fireComp.isFired) continue;

					const Component::MuzzleComponent& _muzzle = a_muzzleArray[_i];
					if (_muzzle.muzzleEffect.guid == Core::DEFAULT_GUID) continue;

					Component::EffectRuntimeComponent& _runtime = a_runtimeArray[_i];
					Component::EffectOverrideComponent& _override = a_overrideArray[_i];
					auto* _pMuzzleEffect = a_ctx.pServices->pResourceManager->Ref(_runtime.effectHandle);
					if (!_pMuzzleEffect) continue;

					// 位置も向きも「武器の行列から拡縮を落とした座標系」へ直して渡す。
					//
					// EffectDrawSystem はパーティクルの置き場に持ち主の拡縮を掛けない
					// (拡縮を落とした行列 StripScale を掛ける)。武器ローカルの銃口位置をそのまま渡すと、
					// 機体ごと拡大している(3倍など)ときに銃口までの距離が縮み、持ち手の辺りから光っていた。
					// 銃口のワールド位置(WeaponMuzzleSystem が spawnPos に入れた値)から引き直せば、拡縮があっても一致する
					const Math::Matrix _weaponRTInvMat =
						Engine::Graphics::Particle::EmitterSlotPool::StripScale(Math::Matrix(a_worldMatArray[_i].worldMat)).Invert();

					_override.effectScale = _muzzle.muzzleEffect.scale;
					_override.isOverrideTransform = true;
					_override.overridePosOffset = Math::Vector3::Transform(_fireComp.spawnPos, _weaponRTInvMat);
					_override.overrideEmitDir = Math::Vector3::TransformNormal(_fireComp.shootDir, _weaponRTInvMat);

					// 頭から出し直す(Play は中で Reset を呼ぶ)。
					//
					// isPlay は一度立てたら下ろさない。EffectUpdateSystem は
					// isPlay の立ち下がりで Stop するので、下ろすと消えてしまう。
					// また立ち上がりの Play は「まだ再生していない」ときしか走らず、
					// 連射の上書きには使えないので、ここで直接 Play を呼ぶ。
					// 出し切った後はどのパーツも出す時間帯から外れるだけなので、
					// 再生中のまま置いておいても何も出ない
					a_requestArray[_i].isPlay = true;
					Engine::Effect::EffectPlayer::Play(*_pMuzzleEffect, _runtime.instance);
				}
			}
		)
		//------------------------------------------------------------------
		// 順序 : エフェクトの時間を進める(EffectUpdateSystem)より先に頭出しする
		//
		// 頭出し(Play)はそのフレームの発生数(pendingEmit)も 0 に戻す。
		// 進めた後に頭出しすると、決まった発生数を消してしまい、
		// 毎フレーム撃つとき(fps が連射より低い)は一度も出なかった。
		// 先に頭出しすれば、同じフレームの進行で最初のバーストが決まり、撃ったフレームに光る。
		// (以前は After("BoosterEffectSystem") で、進行の後ろに回っていた)
		//------------------------------------------------------------------
		.Before("EffectUpdateSystem");
	}
}
