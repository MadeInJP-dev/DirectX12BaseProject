#pragma once

#include "Application/Components/Combat/LockOnTargetComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileLockComponent.h"

namespace App::InstanceResource
{
	//==========================================================================================
	//
	// HUD(画面に重ねるUI)が読む値を1か所にまとめたワールドリソース。
	//
	// 書くのは HUDGatherSystem(PostUpdate)だけで、HUD はここを読むだけにする。
	//
	// ・HUD がワールドを直接見ていたころは、TargetBoxHUD / UIGauge / HitEffectHUD …が
	//   それぞれ ForEach でプレイヤーを探し直し、
	//   「GameObjectManager::Update は ○○System の後なので」という実行順の前提を
	//   HUD ごとに抱えていた。集めるのを1つのシステムに寄せれば、
	//   探し方も実行順の前提もここだけで済む。
	//
	// ・中身は「出すための値」であって、ゲームの状態そのものではない。
	//   ゲーム側のシステムはここを読まないこと(HUD が無くてもゲームは同じに動く)。
	//
	// ・毎フレーム作り直す(hitSerial だけは積み上げる)。
	//   プレイヤーが居ないフレームは player が INVALID_ENTITY のまま、各 isValid は false のまま残る。
	//
	//==========================================================================================

	//--------------------------------------------------------------------------------------
	// ゲージで見る相手
	//--------------------------------------------------------------------------------------
	enum class EHUDSubject : uint32_t
	{
		Player,			// 操作しているプレイヤー
		LockedEnemy,	// プレイヤーがロックしている敵
		RightWeapon,	// プレイヤーの右手武器
		LeftWeapon,		// プレイヤーの左手武器

		Count,
	};

	//--------------------------------------------------------------------------------------
	// ゲージで見る値
	//--------------------------------------------------------------------------------------
	enum class EHUDGaugeKind : uint32_t
	{
		Health,		// HealthComponent                          : currentHealth / maxHealth
		BoostFuel,	// BoostStateComponent / BoostParamsComponent : currentFuel / maxFuel
		Overheat,	// WeaponOverHeatComponent                  : heat / limit
		ChargeDash,	// ChargeDashComponent                      : chargeTimer / chargeTime
		Magazine,	// WeaponReloadComponent                    : 残弾 / 容量(リロード中は進み具合)

		Count,
	};

	// 現在値と最大値
	struct HUDGaugeValue
	{
		float current = 0.0f;
		float max = 0.0f;
		bool isValid = false;	// 相手が居て、そのコンポーネントを持っていたか
	};

	// 判定の円(px, 左上原点)。描く側はこの位置と大きさに絵を合わせる
	struct HUDReticle
	{
		Math::Vector2 center = {};
		float radius = 0.0f;
		bool isValid = false;
	};

	// 画面に映っている敵1体ぶん
	struct HUDTargetMark
	{
		Math::Vector2 screenPos = {};	// スクリーン座標(px, 左上原点)
		bool isLocked = false;			// ロック中の相手か
	};

	struct PlayerHUDResource
	{
		static constexpr int TARGET_MAX = Component::LockOnTargetComponent::TARGET_MAX;
		static constexpr int MISSILE_LOCK_MAX = Component::MissileLockComponent::MISSILE_MAX;

		static constexpr size_t SUBJECT_COUNT = static_cast<size_t>(EHUDSubject::Count);
		static constexpr size_t GAUGE_KIND_COUNT = static_cast<size_t>(EHUDGaugeKind::Count);

		// 操作しているプレイヤー
		Engine::ECS::Entity player = Engine::ECS::Limits::INVALID_ENTITY;

		//----------------------------------------------------------------------------------
		// ゲージ : [見る相手][見る値]
		//----------------------------------------------------------------------------------
		HUDGaugeValue gauges[SUBJECT_COUNT][GAUGE_KIND_COUNT] = {};

		//----------------------------------------------------------------------------------
		// 銃のロックオン(LockOnTargetComponent)
		//----------------------------------------------------------------------------------
		HUDReticle aimReticle = {};						// ロックできる円
		HUDTargetMark targets[TARGET_MAX] = {};			// 画面に映っている敵
		int targetCount = 0;

		bool isLocked = false;							// ロック中の相手が居るか
		Math::Vector2 lockedScreenPos = {};				// その相手のスクリーン座標(px)

		//----------------------------------------------------------------------------------
		// ミサイル(MissileLockComponent)
		//----------------------------------------------------------------------------------
		// 見た目の円(reticleRadius)。実際に溜める判定は reticleScale を掛けたもの
		HUDReticle missileReticle = {};

		bool isMissileCharging = false;					// 溜めている最中か(キーを押している)
		Math::Vector2 missileLockScreenPos[MISSILE_LOCK_MAX] = {};	// 溜めた敵のスクリーン座標(px)
		int missileLockCount = 0;

		//----------------------------------------------------------------------------------
		// 手応え
		//
		// プレイヤーの弾がダメージの通る相手に当たったフレームに1つ進む。
		// HUD は前に見た値と比べて「当たった」を知る。
		// フラグにしないのは、読む側が1フレーム遅れても取りこぼさないようにするため
		//----------------------------------------------------------------------------------
		uint32_t hitSerial = 0;

		//==================================================================================

		bool HasPlayer() const { return player != Engine::ECS::Limits::INVALID_ENTITY; }

		const HUDGaugeValue& GetGauge(EHUDSubject a_subject, EHUDGaugeKind a_kind) const
		{
			return gauges[static_cast<size_t>(a_subject)][static_cast<size_t>(a_kind)];
		}
		HUDGaugeValue& RefGauge(EHUDSubject a_subject, EHUDGaugeKind a_kind)
		{
			return gauges[static_cast<size_t>(a_subject)][static_cast<size_t>(a_kind)];
		}

		// このフレームぶんを作り直す前に空へ戻す(積み上げる hitSerial は残す)
		void ClearFrame()
		{
			const uint32_t _hitSerial = hitSerial;
			*this = {};
			hitSerial = _hitSerial;
		}
	};
}
