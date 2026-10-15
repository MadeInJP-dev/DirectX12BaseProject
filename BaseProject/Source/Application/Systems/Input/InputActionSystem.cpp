#include "InputActionSystem.h"
#include "Application/ECS/World/APPWorld.h"

#include "Application/Components/Combat/ActionIntentComponent.h"
#include "Application/Components/Input/PlayerControllTag.h"

#include "Application/Game/Core/InputSettings.h"


namespace App::System
{
	void InputActionSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveTask<const Component::PlayerControllTag, Component::ActionIntentComponent>(
			Engine::ECS::ESystemType::Input,
			"InputActionSystem",
			[]
			(
				Engine::ECS::Chunk* /*a_pChunk*/,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_ActiveTag*/,
				const Component::PlayerControllTag* /*a_tags*/,
				Component::ActionIntentComponent* a_actionIntentArray
				)
			{
				// 武器 : 左クリックが左手、右クリックが右手。
				// ここで作るのは「押されている」という命令だけで、
				// 撃てるかどうか(連射間隔・バースト・熱)は武器側が決める
				bool _isLeftShoot = a_ctx.pServices->pInputManager->IsHold(App::Game::EGameAction::LWeaponAttack);
				bool _isRightShoot = a_ctx.pServices->pInputManager->IsHold(App::Game::EGameAction::RWeaponAttack);

				// ミサイル : 押している間が溜め、離した瞬間が発射(判定は MissileSalvoSystem)。
				// 右肩と左肩は別々に溜めて撃つ
				bool _isMissile = a_ctx.pServices->pInputManager->IsHold(App::Game::EGameAction::RMissileLock);
				bool _isLeftMissile = a_ctx.pServices->pInputManager->IsHold(App::Game::EGameAction::LMissileLock);

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::ActionIntentComponent& _intent = a_actionIntentArray[_i];
					_intent.isLeftWeaponShoot = _isLeftShoot;
					_intent.isRightWeaponShoot = _isRightShoot;
					_intent.isMissileHold = _isMissile;
					_intent.isLeftMissileHold = _isLeftMissile;
				}
			}
		);
	}
}
