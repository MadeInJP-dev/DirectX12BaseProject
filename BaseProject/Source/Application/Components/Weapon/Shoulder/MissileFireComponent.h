#pragma once

#include "Application/Components/Weapon/WeaponConfig/MuzzleComponent.h"

namespace App::Component
{
	//================================================================================
	// 肩の武器(ミサイル)の一斉射
	//
	// 持ち主から「この相手たちへ撃て」という要求を受け、burstNum 発を撃ち出す。
	// 実際に撃つのは MissileFireSystem(この武器の側)。
	//
	//   要求を出す側 : プレイヤー … MissileSalvoSystem(レティクルで溜めて、離した瞬間)
	//                  ボス       … BossMissileSalvoSystem(間隔で立つ命令)
	//   撃つ側       : MissileFireSystem … 要求を受けたら burstTime 間隔で1発ずつ(isBurst が false なら全弾同時)
	//                                       撃ち切ったら waitTime たつまで次の要求を受けない
	//
	// ・何発撃てるか・どこから撃つかは武器の性能なので、持ち主ではなくこちらが持つ。
	//   持ち主側(MissileLockComponent)は「誰をロックするか」だけを持つ。
	// ・発射位置 : 銃口(MuzzleComponent)を基準に、firstPos から endPos まで等間隔に並べる(武器ローカル)。
	//   columnNum で段を付けられる。段数で発射数を割り、1段ぶんを firstPos → endPos の横並び
	//   (X / Z)に、段を firstPos.y → endPos.y の縦に並べる。1段なら Y も横並びと一緒に動く。
	//   どちらも 0 なら全弾が銃口から出る。
	// ・固定長配列なのは ECS コンポーネントに std::vector を持たせないため
	//   (アーキタイプ移動やプレハブ経路で memcpy される)。
	//================================================================================
	struct MissileFireComponent
	{
		// 一度に扱える発射数の上限。burstNum はこれ以下に丸められる
		static constexpr int MISSILE_MAX = 16;

		uint32_t burstNum = 6;				// 1回の一斉射で撃つ数

		// 発射位置(銃口からのずらし。武器ローカル)
		Math::Vector3 firstPos = { 0.0f, 0.0f, 0.0f };	// 1発目
		Math::Vector3 endPos = { 0.0f, 0.0f, 0.0f };	// 最後
		uint32_t columnNum = 1;							// 段数

		float burstTime = 0.06f;			// 1発ごとの間隔(秒)
		float waitTime = 1.0f;				// 撃ち切ってから次の要求を受けられるまで(秒)
		bool isBurst = true;				// true : burstTime 間隔で1発ずつ / false : 全弾同時

		float spreadAngle = 25.0f;			// 射出時の散らし角(度)。狙う向きを軸にしたコーンの半頂角

		// 実際に撃つ数(上限で丸めたもの)
		int GetActiveBurstNum() const
		{
			return std::clamp(static_cast<int>(burstNum), 0, MISSILE_MAX);
		}

		//------------------------------------------------------------------------
		// 要求(ランタイム。持ち主が書き、MissileFireSystem が受け取って下ろす)
		//------------------------------------------------------------------------
		struct Request
		{
			bool isRequested = false;
			Engine::ECS::Entity targets[MISSILE_MAX] = {};	// 各弾が追う相手。INVALID なら直進
			int targetCount = 0;							// targets の有効数(撃つ数ではない。足りなければ使い回す)
			Math::Vector3 aimDir = { 0.0f, 0.0f, 1.0f };	// 相手の居ない弾が飛ぶ向き(ワールド)
			float targetOffsetY = 0.0f;						// 相手の原点から上へずらして狙う量(m)
		};
		Request request = {};

		//------------------------------------------------------------------------
		// 発射中(ランタイム。MissileFireSystem だけが書く)
		//------------------------------------------------------------------------
		struct Runtime
		{
			Engine::ECS::Entity fireTargets[MISSILE_MAX] = {};	// 各弾が追う相手
			Math::Vector3 aimDir = { 0.0f, 0.0f, 1.0f };
			float targetOffsetY = 0.0f;

			int fireTotal = 0;			// この一斉射の総数(散らしと発射位置の計算用)
			int fireCount = 0;			// 撃った数
			float burstTimer = 0.0f;	// 次の1発までの残り時間(秒)
			float waitTimer = 0.0f;		// 次の要求を受けられるまでの残り時間(秒)
		};
		Runtime runtimeData = {};

		bool IsFiring() const { return runtimeData.fireCount < runtimeData.fireTotal; }
		bool IsWaiting() const { return runtimeData.waitTimer > 0.0f; }

		// 新しい要求を受けられるか(持ち主がロックを溜め始めてよいかの判断にも使う)
		bool IsReady() const { return !IsFiring() && !IsWaiting() && !request.isRequested; }
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::MissileFireComponent>
{
	// 発射位置の基準は銃口なので必ず持たせる
	using Requires = Engine::ECS::RequireComponents<App::Component::MuzzleComponent>;

	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::MissileFireComponent& _comp = Engine::EditorField::RefValue<App::Component::MissileFireComponent>(a_pData);
		a_ar.Field("burstNum", _comp.burstNum);
		a_ar.Field("firstPos", _comp.firstPos);
		a_ar.Field("endPos", _comp.endPos);
		a_ar.Field("columnNum", _comp.columnNum);
		a_ar.Field("burstTime", _comp.burstTime);
		a_ar.Field("waitTime", _comp.waitTime);
		a_ar.Field("isBurst", _comp.isBurst);
		a_ar.Field("spreadAngle", _comp.spreadAngle);
	}

	static void Edit(CompEditContext& a_context)
	{
		using App::Component::MissileFireComponent;
		MissileFireComponent& _comp = Engine::EditorField::RefValue<MissileFireComponent>(a_context.pData);

		Engine::EditorField::Header("Salvo");
		int _burstNum = static_cast<int>(_comp.burstNum);
		if (Engine::EditorField::Field("Burst Num", _burstNum, 1.0f, 1, MissileFireComponent::MISSILE_MAX))
		{
			_comp.burstNum = static_cast<uint32_t>(std::clamp(_burstNum, 1, MissileFireComponent::MISSILE_MAX));
		}
		Engine::EditorField::Field("Is Burst", _comp.isBurst);
		Engine::EditorField::Tooltip("on : Burst Time 間隔で1発ずつ / off : 全弾同時");
		{
			Engine::EditorField::DisabledScope _disabled(!_comp.isBurst);
			Engine::EditorField::Field("Burst Time", _comp.burstTime, 0.005f, 0.0f, 5.0f, "%.3f s");
		}
		Engine::EditorField::Field("Wait Time", _comp.waitTime, 0.05f, 0.0f, 60.0f, "%.2f s");
		Engine::EditorField::Tooltip("撃ち切ってから次に撃てるまで");
		Engine::EditorField::Field("Spread Angle", _comp.spreadAngle, 0.5f, 0.0f, 89.0f, "%.1f deg");

		Engine::EditorField::Header("Launch Position");
		Engine::EditorField::Field("First Pos", _comp.firstPos, 0.01f);
		Engine::EditorField::Field("End Pos", _comp.endPos, 0.01f);
		int _columnNum = static_cast<int>(_comp.columnNum);
		if (Engine::EditorField::Field("Column Num", _columnNum, 1.0f, 1, MissileFireComponent::MISSILE_MAX))
		{
			_comp.columnNum = static_cast<uint32_t>(std::clamp(_columnNum, 1, MissileFireComponent::MISSILE_MAX));
		}
		Engine::EditorField::Tooltip("段数。1段ぶんを First → End の横(X / Z)に、段を First.y → End.y の縦に並べる");

		// 結果は毎フレーム上書きされるので表示のみ
		Engine::EditorField::Line();
		Engine::EditorField::Value("Fire", "%d / %d", _comp.runtimeData.fireCount, _comp.runtimeData.fireTotal);
		Engine::EditorField::Value("Wait", "%.2f s", _comp.runtimeData.waitTimer);
	}
};
