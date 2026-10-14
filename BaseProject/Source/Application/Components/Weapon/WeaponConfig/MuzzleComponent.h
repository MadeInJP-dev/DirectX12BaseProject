#pragma once

#include "Engine/EditorField/EditorField.h"
#include "Engine/EditorField/EditorField.inl"
#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"

#include "Application/Editor/CompEditHelper/CompEditHelper.h"

namespace App::Component
{
	//================================================================================
	// 銃口 : 弾を出す位置と向き、撃った瞬間に光らせるもの
	//
	// 武器には必須(撃ち方・ミサイルの発射のコンポーネントが必須コンポーネントとして付ける)。
	//
	// ・武器のモデルに銃口のヌルノードを仕込み、その名前のハッシュを持つ。
	//   弾を出す位置もマズルフラッシュを出す位置もここ。未設定(0)なら武器の原点から出す。
	// ・dir は狙点(AimResultComponent)が無いときに撃つ向き(武器ローカル)。
	//   武器はアニメーションのノードに追従していて、ローカルの +Z が銃身の向きとは限らないため、
	//   向きはモデルに合わせてここで決める。狙点がある武器は狙点へ向けて撃つ。
	// ・ノードの番号は MuzzleStartSystem がハッシュから引く(保存しない)。
	//================================================================================
	struct MuzzleComponent
	{
		UINT muzzlePtrNodeHash = 0;							// 武器モデルの銃口ヌルノードの名前ハッシュ。0 なら武器の原点
		Math::Vector3 offset = { 0.0f, 0.0f, 0.0f };		// 銃口からのずらし(武器ローカル)
		Math::Vector3 dir = { 0.0f, 0.0f, 1.0f };			// 狙点が無いときに撃つ向き(武器ローカル)

		//------------------------------------------------------------------------
		// マズルフラッシュ
		//
		// 弾を1発撃つたびに、銃口へ単発のエフェクトを出す。
		// 武器自身に再生枠(EffectAssetComponent)を持たせて頭から出し直すので、
		// Duration を入れた(出し切りで終わる)アセットを指定すること。
		// 発砲音もこのエフェクトのサウンドパーツに入れておけば絵と音が揃う。
		//------------------------------------------------------------------------
		struct Effect
		{
			Core::GUID guid = Core::DEFAULT_GUID;							// 記録用(セーブされる)
			Engine::Handle<Engine::Resource::EffectAsset> handle = {};		// EffectFixupSystem が解決する
			float scale = 1.0f;												// 大きさ倍率(アセットは共有なので個体差はここで付ける)
		};
		Effect muzzleEffect = {};

		// ---- ランタイム(保存しない) ----
		UINT nodeIndex = 0;			// 銃口ノードの番号
		bool isNodeValid = false;	// ノードを引けたか(引けなければ武器の原点から出す)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::MuzzleComponent>
{
	static void Release(void* a_pData, const Engine::ECS::EngineServices& a_services)
	{
		App::Component::MuzzleComponent& _comp = Engine::EditorField::RefValue<App::Component::MuzzleComponent>(a_pData);
		a_services.pResourceManager->ReleaseHandle(_comp.muzzleEffect.handle);
	}

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::MuzzleComponent& _comp = Engine::EditorField::RefValue<App::Component::MuzzleComponent>(a_pData);
		a_ar.Field("muzzlePtrNodeHash", _comp.muzzlePtrNodeHash);
		a_ar.Field("offset", _comp.offset);
		a_ar.Field("dir", _comp.dir);
		a_ar.Field("muzzleEffectGUID", _comp.muzzleEffect.guid);
		a_ar.Field("muzzleEffectScale", _comp.muzzleEffect.scale);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::MuzzleComponent& _comp = Engine::EditorField::RefValue<App::Component::MuzzleComponent>(a_context.pData);

		App::Editor::CompEditHelper::SelectSelfModelNode(
			a_context,
			_comp.muzzlePtrNodeHash,
			_comp.nodeIndex
		);
		Engine::EditorField::Field("Offset", _comp.offset, 0.01f);
		if (Engine::EditorField::Field("Dir", _comp.dir, 0.01f))
		{
			if (_comp.dir.LengthSquared() <= 1e-8f) _comp.dir = { 0.0f, 0.0f, 1.0f };
		}
		Engine::EditorField::Tooltip("狙点が無いときに撃つ向き(武器ローカル)");

		// ---- マズルフラッシュ ----
		Engine::EditorField::Line();
		Engine::EditorField::HelpText("Muzzle Flash : 1発撃つごとに銃口へ出す");
		Engine::EditorField::AssetField<Engine::Resource::EffectAsset>(
			*a_context.pWorld->RefEngineServices(),
			"Muzzle Effect",
			"EffectAsset",
			_comp.muzzleEffect.guid,
			_comp.muzzleEffect.handle);
		Engine::EditorField::Field("Muzzle Effect Scale", _comp.muzzleEffect.scale, 0.01f, 0.0f);
		if (_comp.muzzleEffect.guid == Core::DEFAULT_GUID)
		{
			Engine::EditorField::HelpText("(未設定 : 撃っても何も出ない)");
		}
		else
		{
			Engine::EditorField::HelpText("出し切ったら自分で消えるので、Duration を入れたアセットを指定すること");
		}
	}
};
