#pragma once

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"
#include "Engine/Effect/EffectInstance.h"
#include "Engine/EditorField/EditorField.h"
#include "Engine/Graphics/Particle/GPU/EmitterSlotPool/EmitterSlotPool.h"

#include "Application/Utility/EffectSpawnHelper.h"

namespace App::Component
{
	//==========================================================================================
	// EffectRuntimeComponent
	//
	// EffectAssetComponent の実行中の値 : GUID から解決したアセットのハンドルと、
	// このエンティティ専用の進行状態。
	//
	// ・解決は EffectFixupSystem、進行は EffectUpdateSystem、発生・描画は EffectDrawSystem。
	// ・アセットは全員で共有する設計図なので、進行状態は instance が持つ。
	// ・EffectAssetComponent の必須コンポーネントなので、プレハブに書かなくても付く。保存しない。
	// ・ローカル空間で回すパーティクルを出すなら、発生源の席(emitterSlot)も持つ。
	//   取るのと毎フレームの行列の更新は EffectDrawSystem、返す予約は下の Release。
	//==========================================================================================
	struct EffectRuntimeComponent
	{
		// GUID から解決したアセットのハンドル
		Engine::Handle<Engine::Resource::EffectAsset> effectHandle = {};

		// このエンティティ専用の進行状態
		Engine::Effect::EffectInstance instance = {};

		//------------------------------------------------------------------
		// 発生源の席(ローカル空間のパーティクル用)
		//
		// エフェクト1つにつき1席。Local のパーツが1つでもあるときだけ取る。
		// 止めても返さない : 出し終わった粒も、持ち主が生きている間は持ち主について動く。
		// 返すのはエンティティごと消えるとき(Release)で、粒が消えきるまで待ってから空きへ戻る。
		//
		// ※ プレハブから実体化すると値がそのまま写ってくるので、EffectFixupSystem で必ず空にする
		//    (空にしないと、他人の席の行列を書き換えてしまう)
		//------------------------------------------------------------------
		Engine::Handle<Engine::Graphics::Particle::EmitterTransform> emitterSlot = {};
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::EffectRuntimeComponent>
{
	//----------------------------------------------------------------------------------
	// 借りているリソースを返す
	//
	// コンポーネントはデストラクタが走らないので、参照を返すのはここの仕事。
	// ECS がエンティティを消すとき・コンポーネントを外すとき・
	// PostDeserialize へ入り直すとき(fixup が取り直す)に必ず呼ぶ。
	//----------------------------------------------------------------------------------
	static void Release(void* a_pData, const Engine::ECS::EngineServices& a_services)
	{
		App::Component::EffectRuntimeComponent& _comp = Engine::EditorField::RefValue<App::Component::EffectRuntimeComponent>(a_pData);

		// 席の返却予約が先。猶予(粒の最大寿命)をエフェクトアセットから引くので、
		// アセットのハンドルを返した後では間に合わない
		App::Utility::ReserveReturnEffectEmitterSlot(a_services, _comp.effectHandle, _comp.emitterSlot);

		// 出している途中のライトパーツが借りているポイントライトも返す
		App::Utility::ReleaseEffectLights(a_services, _comp.instance);

		// 引いている途中のトレイルも手放す(残りの点は消えきるまで描かれる)
		App::Utility::ReleaseEffectTrails(a_services, _comp.instance);

		a_services.pResourceManager->ReleaseHandle(_comp.effectHandle);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::EffectRuntimeComponent& _comp = Engine::EditorField::RefValue<App::Component::EffectRuntimeComponent>(a_context.pData);

		// 中身の確認用。細かい編集はアセット側のインスペクターで行う
		auto* _pEffect = a_context.pWorld->RefEngineServices()->pResourceManager->Ref(_comp.effectHandle);
		if (!_pEffect)
		{
			Engine::EditorField::HelpText("(未解決 / 読み込み中)");
			return;
		}

		Engine::EditorField::Value("Particle Parts", "%d", static_cast<int>(_pEffect->GetParticleParts().size()));
		Engine::EditorField::Value("Mesh Parts", "%d", static_cast<int>(_pEffect->GetMeshParts().size()));
		Engine::EditorField::Value("Trail Parts", "%d", static_cast<int>(_pEffect->GetTrailParts().size()));
		Engine::EditorField::Value("Elapsed", "%.2f", _comp.instance.elapsed);
	}
};
