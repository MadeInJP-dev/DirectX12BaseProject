#pragma once

#include "MissileFireComponent.h"

namespace App::Component
{
	//==========================================================================================
	// MissileLockComponent
	//
	// 肩の武器の持ち主(プレイヤー・ボス)に付ける「誰をロックするか」の置き場。
	// プレイヤーの中身を作るのは MissileSalvoSystem。
	//
	//   ミサイルキーを押している間 … コンバットレティクル(CombatReticleHUD)の円に入った敵を
	//                                 locks[] へ溜める(1体につき1ロック。上限は肩の武器の発射数)
	//   離した瞬間                  … 溜めた相手を肩の武器(アタッチメントスロットの missile)の
	//                                 MissileFireComponent へ要求として渡す
	//
	// ・何発を・どの間隔で・どこから撃つか(と撃った後の待ち時間)は武器の性能なので、
	//   肩の武器の MissileFireComponent が持つ。ここには持たない。
	//   武器を付け替えれば発射数も変わる。
	// ・収集の円はここが唯一の正解(画面中央 × reticleRadius × reticleScale)。
	//   CombatReticleHUD は reticleRadius の円に絵を合わせて出す。
	// ・ボスは溜めを使わない(相手が最初から決まっている)。使うのは targetOffsetY だけ。
	// ・固定長配列なのは ECS コンポーネントに std::vector を持たせないため。
	//==========================================================================================
	struct MissileLockComponent
	{
		// 同時に扱えるロックの上限(肩の武器の発射数の上限と同じ)
		static constexpr int MISSILE_MAX = MissileFireComponent::MISSILE_MAX;

		// ---- 設定(保存される) ----
		float maxDistance    = 300.0f;	// ロックできる距離(m)。0 以下なら距離では切らない
		float targetOffsetY  = 0.0f;	// 敵の原点から上へずらす量(m)。原点が足元のモデル用
		float reticleRadius  = 200.0f;	// レティクルの見た目の半径(px)。CombatReticleHUD はこの大きさに絵を合わせる
		float reticleScale   = 1.0f;	// 見た目に対する判定の倍率(見た目より狭く/広く取る用)
		bool  requireLock    = false;	// true : 1つもロックできていなければ撃たない

		// ---- レティクル(ランタイム。MissileSalvoSystem が書く) ----
		// 判定に使った中心(px, 左上原点)。今は画面中央。HUD はここへレティクルを出す
		Math::Vector2 reticleCenter = { 0.0f, 0.0f };

		// 実際に使う判定の半径
		float GetActiveReticleRadius() const
		{
			return reticleRadius * reticleScale;
		}

		// ---- 収集結果(ランタイム。保存しない) ----
		//
		// 肩の左右で別々に溜めて撃つので、溜めの状態は肩ごとに持つ。
		// 判定の円・距離・狙う高さは左右で同じものを使う
		enum ESide : int
		{
			RightShoulder,	// 右肩(スロットの missile。プレイヤーは E)
			LeftShoulder,	// 左肩(スロットの leftMissile。プレイヤーは Q)
			SIDE_COUNT,
		};

		struct Side
		{
			Engine::ECS::Entity locks[MISSILE_MAX]         = {};	// ロックした敵(重複なし)
			Math::Vector2       lockScreenPos[MISSILE_MAX] = {};	// その敵のスクリーン座標(px)。HUD 用
			int                 lockCount                  = 0;
			bool  isCharging    = false;	// 収集中(キーを押している)か
			bool  wasHold       = false;	// 立ち下がり(離した瞬間)の検出用
		};
		Side sides[SIDE_COUNT] = {};

		// どちらかの肩で溜めているか
		bool IsAnyCharging() const { return sides[RightShoulder].isCharging || sides[LeftShoulder].isCharging; }
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::MissileLockComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::MissileLockComponent& _comp = Engine::EditorField::RefValue<App::Component::MissileLockComponent>(a_pData);
		a_ar.Field("maxDistance",    _comp.maxDistance);
		a_ar.Field("targetOffsetY",  _comp.targetOffsetY);
		a_ar.Field("reticleRadius",  _comp.reticleRadius);
		a_ar.Field("reticleScale",   _comp.reticleScale);
		a_ar.Field("requireLock",    _comp.requireLock);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::MissileLockComponent& _comp = Engine::EditorField::RefValue<App::Component::MissileLockComponent>(a_context.pData);

		Engine::EditorField::Header("Lock");
		Engine::EditorField::Field("MaxDistance", _comp.maxDistance, 1.0f, 0.0f);
		Engine::EditorField::Field("TargetOffsetY", _comp.targetOffsetY, 0.01f);
		Engine::EditorField::Field("ReticleRadius", _comp.reticleRadius, 1.0f, 0.0f, 4096.0f);
		Engine::EditorField::Tooltip("レティクルの見た目の半径(px)。CombatReticleHUD はこの大きさに絵を合わせる");
		Engine::EditorField::Field("ReticleScale", _comp.reticleScale, 0.01f, 0.0f, 4.0f);
		Engine::EditorField::Tooltip("見た目に対する判定の倍率。判定の半径 = ReticleRadius × ReticleScale");
		Engine::EditorField::Field("RequireLock", _comp.requireLock);

		// 結果は毎フレーム上書きされるので表示のみ
		Engine::EditorField::Line();
		Engine::EditorField::Value("ReticleCenter", "%.0f, %.0f", _comp.reticleCenter.x, _comp.reticleCenter.y);
		Engine::EditorField::Value("ActiveRadius", "%.0f px", _comp.GetActiveReticleRadius());
		Engine::EditorField::Text("R Charging : %s  Locks : %d",
			_comp.sides[App::Component::MissileLockComponent::RightShoulder].isCharging ? "yes" : "no",
			_comp.sides[App::Component::MissileLockComponent::RightShoulder].lockCount);
		Engine::EditorField::Text("L Charging : %s  Locks : %d",
			_comp.sides[App::Component::MissileLockComponent::LeftShoulder].isCharging ? "yes" : "no",
			_comp.sides[App::Component::MissileLockComponent::LeftShoulder].lockCount);

		Engine::EditorField::HelpText("発射数・間隔・散らし角 : 肩の武器の MissileFireComponent");
	}
};
