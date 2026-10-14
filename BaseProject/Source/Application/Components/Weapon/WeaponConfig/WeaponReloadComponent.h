#pragma once

namespace App::Component
{
	//================================================================================
	// リロード : 弾倉ぶん撃つとリロードに入り、reloadTime のあいだ撃てなくなる
	//
	// オーバーヒート(WeaponOverHeatComponent)と対になる。持たせなければ弾数の制限は無い。
	//
	// ・弾倉が空になったら自動でリロードに入る(手動のリロード入力はまだ無い)。
	// ・リロードの進行は WeaponReloadSystem、弾を減らすのは撃った瞬間の撃ち方のシステム。
	// ・ランタイムは「撃った数」で持つ。残弾で持つと、生成直後(0)が空と区別できないため。
	//================================================================================
	struct WeaponReloadComponent
	{
		uint32_t magazineMaxCapacity = 30;	// 弾倉の容量
		float reloadTime = 2.0f;			// リロードにかかる時間(秒)

		// ---- ランタイム(保存しない) ----
		struct Runtime
		{
			uint32_t usedCount = 0;			// 今の弾倉で撃った数
			bool isReload = false;			// リロード中か
			float reloadTimer = 0.0f;		// リロードを始めてからの経過時間(秒)
		};
		Runtime runtimeData = {};

		// 弾倉に残っている数
		uint32_t GetRemaining() const
		{
			return (runtimeData.usedCount < magazineMaxCapacity) ? (magazineMaxCapacity - runtimeData.usedCount) : 0;
		}
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::WeaponReloadComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::WeaponReloadComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponReloadComponent>(a_pData);
		a_ar.Field("magazineMaxCapacity", _comp.magazineMaxCapacity);
		a_ar.Field("reloadTime", _comp.reloadTime);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::WeaponReloadComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponReloadComponent>(a_context.pData);

		int _capacity = static_cast<int>(_comp.magazineMaxCapacity);
		if (Engine::EditorField::Field("Magazine Capacity", _capacity, 1.0f, 1, 10000))
		{
			_comp.magazineMaxCapacity = static_cast<uint32_t>((std::max)(_capacity, 1));
		}
		Engine::EditorField::Field("Reload Time", _comp.reloadTime, 0.05f, 0.0f, 60.0f, "%.2f s");

		char _overlay[64] = {};
		snprintf(_overlay, sizeof(_overlay), "%u / %u%s", _comp.GetRemaining(), _comp.magazineMaxCapacity, _comp.runtimeData.isReload ? "  [RELOAD]" : "");
		const float _ratio = (_comp.magazineMaxCapacity > 0)
			? static_cast<float>(_comp.GetRemaining()) / static_cast<float>(_comp.magazineMaxCapacity)
			: 0.0f;
		Engine::EditorField::ProgressBar("Magazine", _ratio, _overlay);
	}
};
