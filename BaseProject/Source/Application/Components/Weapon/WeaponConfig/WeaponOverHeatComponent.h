#pragma once

#include "Engine/EditorField/EditorField.h"
#include "Engine/EditorField/EditorField.inl"
#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"

namespace App::Component
{
	//================================================================================
	// オーバーヒート : 撃つほど熱が溜まり、上限に届くと冷えるまで撃てなくなる
	//
	// リロード(WeaponReloadComponent)と対になる。持たせなければ熱は溜まらない。
	//
	// ・冷却は WeaponOverHeatSystem(毎フレーム。撃っていてもいなくても冷える)。
	//   熱を溜めて上限で止めるのは、撃った瞬間の撃ち方のシステム。
	// ・オーバーヒート中は overheatCoolScale を掛けて冷えを鈍らせられる
	//   (無理をした分だけ復帰を待たされる、というペナルティ)。
	//   熱が restartHeatRatio まで下がったらまた撃てる。
	// ・上限に届いた瞬間に heatEffect を武器の位置へ1回出す(WeaponOverHeatEffectSystem)。
	//================================================================================
	struct WeaponOverHeatComponent
	{
		float perShot = 3.0f;				// 1発あたりに溜まる熱
		float limit = 100.0f;				// ここまで溜まるとオーバーヒート

		float coolRate = 40.0f;				// 毎秒冷える量
		float overheatCoolScale = 0.75f;	// オーバーヒート中の冷却倍率(1未満にすると復帰が遅くなる)

		float restartHeatRatio = 0.2f;		// 復帰する熱の割合(limit 比)。0 なら完全に冷えるまで撃てない

		struct Effect
		{
			Core::GUID guid = Core::DEFAULT_GUID;							// 記録用(セーブされる)
			Engine::Handle<Engine::Resource::EffectAsset> handle = {};		// EffectFixupSystem が解決する(先読み用)
			float scale = 1.0f;												// 大きさ倍率
		};
		Effect heatEffect = {};				// オーバーヒートに達したときに出すエフェクト(単発)

		// ---- ランタイム(保存しない) ----
		struct Runtime
		{
			float heat = 0.0f;				// 今の熱
			bool isOverheat = false;		// オーバーヒート中か(復帰しきい値を下回るまで撃てない)
			bool isOverheatStarted = false;	// このフレームにオーバーヒートしたか(エフェクトの合図)
		};
		Runtime runtimeData = {};

		// 熱の溜まり具合(0〜1)。HUD やインスペクターの表示用
		float HeatRatio() const { return (limit > 0.0f) ? (runtimeData.heat / limit) : 0.0f; }
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::WeaponOverHeatComponent>
{
	static void Release(void* a_pData, const Engine::ECS::EngineServices& a_services)
	{
		App::Component::WeaponOverHeatComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponOverHeatComponent>(a_pData);
		a_services.pResourceManager->ReleaseHandle(_comp.heatEffect.handle);
	}

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::WeaponOverHeatComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponOverHeatComponent>(a_pData);
		a_ar.Field("perShot", _comp.perShot);
		a_ar.Field("limit", _comp.limit);
		a_ar.Field("coolRate", _comp.coolRate);
		a_ar.Field("overheatCoolScale", _comp.overheatCoolScale);
		a_ar.Field("restartHeatRatio", _comp.restartHeatRatio);
		a_ar.Field("heatEffectGUID", _comp.heatEffect.guid);
		a_ar.Field("heatEffectScale", _comp.heatEffect.scale);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::WeaponOverHeatComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponOverHeatComponent>(a_context.pData);

		Engine::EditorField::Field("Per Shot", _comp.perShot, 0.1f, 0.0f, 1000.0f);
		Engine::EditorField::Field("Limit", _comp.limit, 1.0f, 0.01f, 10000.0f);
		Engine::EditorField::Field("Cool Rate", _comp.coolRate, 0.5f, 0.0f, 10000.0f, "%.2f /s");
		Engine::EditorField::Field("Overheat Cool Scale", _comp.overheatCoolScale, 0.01f, 0.0f, 4.0f);
		Engine::EditorField::Field("Restart Heat Ratio", _comp.restartHeatRatio, 0.01f, 0.0f, 1.0f);

		// 上限が 0 以下だと割り算も判定も壊れるので下限で止める
		if (_comp.limit < 0.01f) _comp.limit = 0.01f;

		Engine::EditorField::Line();
		Engine::EditorField::AssetField<Engine::Resource::EffectAsset>(
			*a_context.pWorld->RefEngineServices(),
			"Heat Effect",
			"EffectAsset",
			_comp.heatEffect.guid,
			_comp.heatEffect.handle);
		Engine::EditorField::Field("Heat Effect Scale", _comp.heatEffect.scale, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("オーバーヒートした瞬間に武器の位置へ1回出す");

		char _overlay[64] = {};
		snprintf(_overlay, sizeof(_overlay), "%.1f / %.1f%s", _comp.runtimeData.heat, _comp.limit, _comp.runtimeData.isOverheat ? "  [OVERHEAT]" : "");
		Engine::EditorField::ProgressBar("Heat", _comp.HeatRatio(), _overlay);
	}
};
