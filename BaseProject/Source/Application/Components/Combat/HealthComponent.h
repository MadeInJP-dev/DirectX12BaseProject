#pragma once

#include "Engine/ECS/World/World.h"

#include "Application/Components/Transform/HierarchyComponent.h"

namespace App::Component
{
	//==========================================================================================
	// HealthComponent
	//
	// 体力と「死んでからの後始末待ち」を持つ。敵・ボス・プレイヤーなど
	// 「殴られて減るもの」に付ける。
	//
	// ・maxHealth / releaseDelay だけが設定値(保存され、インスペクタで編集できる)。
	//   currentHealth と死亡状態は生成時に初期化されるランタイム値で、表示のみ。
	// ・減らすのは HealthSystem。0 になったらその場では消さず「死亡状態」へ入る。
	//   死亡状態のあいだ入力/AIは止まり、releaseDelay 秒たったら DeathStateSystem が
	//   解放予約する。
	//
	//   即座に消さないのは、消えたエンティティからは何も引けないため。
	//   死亡エフェクト(EffectEventsComponent の OnDeath)を出す EffectEventSystem は
	//   「死んだ本人のコンポーネント」を引くので、本人が生きているうちに
	//   出し切れるだけの猶予がいる。演出中に死体が残るのは意図した挙動でもある。
	//
	// ・これを持っているものは ExplodeOnHitSystem の即死対象から外れる
	//   (弾のように「当たった瞬間に消える」ものと住み分けるため)。
	//==========================================================================================
	struct HealthComponent
	{
		float maxHealth = 100.0f;		// 最大体力 : 設定値
		float currentHealth = 0.0f;		// 現在体力 : ランタイム。HealthFixupSystem が maxHealth で満たす

		// ---- 死亡状態 ----
		// 死んでから実際に消えるまでの猶予。演出(死亡エフェクト・やられモーション)の尺に合わせる。
		// 0 にすると次のフレームで消えるので、エフェクトを出す猶予が無くなる点に注意
		float releaseDelay = 2.0f;		// 死亡してから解放予約するまでの秒数 : 設定値

		bool  isDead = false;			// 死亡状態か : ランタイム
		float deathTimer = 0.0f;		// 死亡してからの経過秒 : ランタイム
	};

	//==========================================================================================
	// 死亡状態か。HealthComponent を持たないもの(=死なないもの)は false。
	//
	// クエリに HealthComponent を入れると、持たない側がシステムの対象から丸ごと外れてしまう。
	// 「持っていれば見る」で済ませたい所(旋回系など)で使う。
	// isDead を書くのは PostUpdate 帯(HealthSystem)だけなので、Update 帯から引いても競合しない
	//==========================================================================================
	inline bool IsDeadEntity(Engine::ECS::World& a_world, const Engine::ECS::Entity& a_entity)
	{
		if (!a_world.HasComponent<HealthComponent>(a_entity)) return false;

		const HealthComponent* _pHealth = a_world.RefData<HealthComponent>(a_entity);
		return _pHealth && _pHealth->isDead;
	}

	//==========================================================================================
	// 自分か、親をたどった先の持ち主が死亡状態か。
	//
	// 武器は持ち主の子として付く(体力を持つのは持ち主)ので、武器から見て
	// 「持ち主が死んでいるか」を知りたいときに使う。本体が武器を兼ねる敵は自分を見て終わる。
	// 使う側は HealthComponent と HierarchyComponent の読みを宣言すること
	//==========================================================================================
	inline bool IsDeadSelfOrOwner(Engine::ECS::World& a_world, Engine::ECS::Entity a_entity)
	{
		// 親子が輪になっていたときの保険。実際の階層はせいぜい数段
		constexpr int MAX_DEPTH = 8;

		for (int _depth = 0; _depth < MAX_DEPTH && a_entity != Engine::ECS::Limits::INVALID_ENTITY; ++_depth)
		{
			if (IsDeadEntity(a_world, a_entity)) return true;

			if (!a_world.HasComponent<HierarchyComponent>(a_entity)) return false;
			const HierarchyComponent* _pHierarchy = a_world.RefData<HierarchyComponent>(a_entity);
			if (!_pHierarchy) return false;

			a_entity = _pHierarchy->parentID;
		}
		return false;
	}
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::HealthComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::HealthComponent& _comp = Engine::EditorField::RefValue<App::Component::HealthComponent>(a_pData);

		// 現在体力と死亡状態は保存しない。読み込み直したら満タンの生存から始まる
		a_ar.Field("maxHealth", _comp.maxHealth);
		a_ar.Field("releaseDelay", _comp.releaseDelay);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::HealthComponent& _comp = Engine::EditorField::RefValue<App::Component::HealthComponent>(a_context.pData);

		if (Engine::EditorField::Field("MaxHealth", _comp.maxHealth, 1.0f, 0.0f))
		{
			if (_comp.maxHealth < 0.0f) _comp.maxHealth = 0.0f;

			// 上限を下げたときに現在体力がはみ出したままにならないようにする
			if (_comp.currentHealth > _comp.maxHealth) _comp.currentHealth = _comp.maxHealth;
		}

		// 現在体力は表示のみ
		float _ratio = (_comp.maxHealth > 0.0f)
			? std::clamp(_comp.currentHealth / _comp.maxHealth, 0.0f, 1.0f)
			: 0.0f;

		char _label[32] = {};
		std::snprintf(_label, sizeof(_label), "%.0f / %.0f", _comp.currentHealth, _comp.maxHealth);
		Engine::EditorField::ProgressBar("Current", _ratio, _label);

		Engine::EditorField::Line();

		if (Engine::EditorField::Field("ReleaseDelay", _comp.releaseDelay, 0.05f, 0.0f))
		{
			if (_comp.releaseDelay < 0.0f) _comp.releaseDelay = 0.0f;
		}
		Engine::EditorField::Tooltip("(死亡してから消えるまでの秒数)");

		// 死亡状態は表示のみ
		if (_comp.isDead)
		{
			Engine::EditorField::ErrorText("Dead : %.2f / %.2f", _comp.deathTimer, _comp.releaseDelay);
		}
		else
		{
			Engine::EditorField::HelpText("Alive");
		}
	}
};
