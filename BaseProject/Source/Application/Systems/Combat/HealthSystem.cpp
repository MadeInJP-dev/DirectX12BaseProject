#include "HealthSystem.h"

#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Components/Combat/DefenseRatioComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Input/PlayerControllTag.h"
#include "Application/InstanceResource/HitEventResource.h"
#include "Application/InstanceResource/DeathEventResource.h"
#include "Application/InstanceResource/DebugCommandResource.h"
#include "Application/Game/DebugKeys/DebugCommands.h"

namespace App::System
{
	//==============================================================================
	// HealthSystem
	//
	// そのフレームに受けたヒットぶん体力を減らし、0 になったら自分を消す。
	//
	// ・ヒットは HitEventResource(ワールドに1つ)を見る。
	//   弾は当たった直後に自分が消えるので、当てた側から殴りに行くのではなく
	//   「そのフレームに起きたヒット全部」が残るこちらを受け手側が読む。
	//   1フレームに複数発当たった場合もすべて食らう(CollisionEvent は1件しか持てない)。
	// ・撃破時はエフェクトを出さず、死亡を DeathEventResource へ積むだけにする。
	//   何を出すかは EffectEventsComponent の OnDeath、出すのは EffectEventSystem の仕事で、
	//   弾が着弾で消えるときと同じ入口にそろえてある。
	// ・体力を持つものは ExplodeOnHitSystem の対象から外してある(Exclude<HealthComponent>)。
	//   即死させる役目とここが二重に効かないようにするためで、
	//   体力持ちの死亡はこのシステムだけが決める。
	// ・撃破しても ここでは消さない。HealthComponent を「死亡状態」にするだけで、
	//   実際に消す(解放予約する)のは releaseDelay 秒あとの DeathStateSystem。
	//
	//   以前はここで ReserveReleaseEntity まで済ませていたが、それだと死亡を読む側が
	//   1フレームでも遅れると本人がもう居らず、死亡エフェクトが出せなかった。
	//   死んだ本人のコンポーネントを引く処理(EffectEventSystem など)のために、
	//   死んでからしばらくは生かしておく。
	// ・爆発の位置に WorldMatrix ではなく LocalTransform を使っている。
	//   体力を持つのは敵やプレイヤーのような親を持たないエンティティなので、
	//   ローカル座標がそのままワールド座標になる。
	//   (以前は WorldMatrix を読むとソートが循環したための選択。フェーズのタグを
	//    依存に数えなくなった(IsQueryOnlyTag)ので、今はその理由は無い)
	// ・受けたダメージの合計には DefenseRatioComponent の比率を掛ける(持っていなければそのまま)。
	//   0 なら無敵。ワームボスが小隊長を整理している間などに使う。
	// ・デバッグキーの無敵(DebugCommands::isPlayerInvincible。Development まで)が立っていれば、
	//   プレイヤー(PlayerControllTag)はダメージを食らわない。当たった演出はそのまま出る。
	// ・PostUpdate 帯。ヒットを積むのは Physics 帯の HitDetectSystem、
	//   消すのは次フレーム PreUpdate の HitEventClearSystem なので、その間で読む。
	//==============================================================================
	void HealthSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<Component::HealthComponent, const Component::LocalTransformComponent>(
			Engine::ECS::ESystemType::PostUpdate,
			"HealthSystem",
			[](
				Engine::ECS::Chunk*      a_pChunk,
				uint32_t                          a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag*                        /*a_tags*/,
				Component::HealthComponent*                  a_healthArray,
				const Component::LocalTransformComponent*    a_trsArray
			)
			{
				if (!a_ctx.pWorld->HasResource<InstanceResource::HitEventResource>()) return;
				const InstanceResource::HitEventResource& _hitEvents = a_ctx.pWorld->GetResource<InstanceResource::HitEventResource>();

				// ヒットが1件も無いフレームは何もしない
				if (_hitEvents.events.empty()) return;

				// デバッグキーの無敵(入口が無ければ Shipping なので効かない)
				const Game::DebugCommands* _pDebug = InstanceResource::DebugCommandResource::FindCommands(a_ctx.pWorld);
				const bool _isPlayerInvincible = _pDebug && _pDebug->isPlayerInvincible;

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::HealthComponent&               _health = a_healthArray[_i];
					const Component::LocalTransformComponent& _trs    = a_trsArray[_i];

					// 既に力尽きているものは二重に処理しない
					if (_health.currentHealth <= 0.0f) continue;

					Engine::ECS::Entity _self = a_pChunk->entityData[_i];

					// ---- 自分が受け手になっているヒットを全部食らう ----
					float _damage = 0.0f;
					for (const InstanceResource::HitEvent& _event : _hitEvents.events)
					{
						if (_event.victim != _self) continue;
						_damage += _event.damage;
					}

					if (_damage <= 0.0f) continue;

					if (_isPlayerInvincible && a_ctx.pWorld->HasComponent<Component::PlayerControllTag>(_self)) continue;

					// 防御比率(1 : そのまま / 0 : 無敵)。持っていなければそのまま食らう
					if (a_ctx.pWorld->HasComponent<Component::DefenseRatioComponent>(_self))
					{
						_damage *= std::max(a_ctx.pWorld->RefData<Component::DefenseRatioComponent>(_self)->ratio, 0.0f);
						if (_damage <= 0.0f) continue;
					}

					_health.currentHealth -= _damage;
					if (_health.currentHealth > 0.0f) continue;

					// ---- 撃破 : 消さずに死亡状態へ入る ----
					_health.currentHealth = 0.0f;
					_health.isDead        = true;
					_health.deathTimer    = 0.0f;

					// 死亡を積む(エフェクトは EffectEventSystem が出す)
					if (a_ctx.pWorld->HasResource<InstanceResource::DeathEventResource>())
					{
						InstanceResource::DeathEvent _death = {};
						_death.entity = _self;
						_death.pos    = _trs.pos;

						a_ctx.pWorld->RefResource<InstanceResource::DeathEventResource>().Push(_death);
					}
				}
			}
		)
		// 順序 : 死亡(DeathEventResource)を積む側同士の並び
		.After("ExplodeOnHitSystem")
		// 絞り込みに使わない読み : 防御比率を RefData で読む
		.Reads<Component::DefenseRatioComponent>()
		.ReadsResource<InstanceResource::HitEventResource>()
		.ReadsResource<InstanceResource::DebugCommandResource>()
		.WritesResource<InstanceResource::DeathEventResource>();
	}
}
