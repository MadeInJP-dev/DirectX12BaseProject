#pragma once

#include "Engine/ECS/World/World.h"

namespace App::Game
{
	struct DebugCommands;
}

namespace App::InstanceResource
{
	//==========================================================================================
	//
	// デバッグキー(Development まで)の命令への入口を置くワールドリソース。
	//
	// 命令の実体(DebugCommands)は GameManager の DebugKeys が持つ。ここに置くのは借りたポインタと、
	// このワールドだけで使う「倒す予約」。Shipping ではワールドの作り手がこれを置かない。
	//
	// 倒す予約を別に持っているのは、死亡を積む時刻の都合。
	// ウェーブを飛ばす SceneSequence はオブジェクトなのでシステムの後に更新されるが、
	// 死亡(DeathEventResource)は次のフレームの PreUpdate で捨てられるので、
	// その場で積んでもエフェクトもスコアも出ない。ここへ積んでおき、
	// 次のフレームの PostUpdate で DebugKillSystem が体力切れと同じ形で死亡させる。
	//
	//==========================================================================================
	struct DebugCommandResource
	{
		const Game::DebugCommands* pCommands = nullptr;

		// 倒す予約。DebugKillSystem が死亡状態へ入れて空にする
		std::vector<Engine::ECS::Entity> killReservedVec = {};

		/// <summary>
		/// ワールドから入口を引く。置かれていなければ nullptr
		/// </summary>
		/// <remarks>
		/// Shipping のワールドと、作り手を通らずに作られたワールド(エディターの確認用など)には無い
		/// </remarks>
		static DebugCommandResource* Find(Engine::ECS::World* a_pWorld)
		{
			if (!a_pWorld || !a_pWorld->HasResource<DebugCommandResource>()) return nullptr;
			return &a_pWorld->RefResource<DebugCommandResource>();
		}

		// 命令を引く。入口が無ければ nullptr
		static const Game::DebugCommands* FindCommands(Engine::ECS::World* a_pWorld)
		{
			const DebugCommandResource* _pResource = Find(a_pWorld);
			return _pResource ? _pResource->pCommands : nullptr;
		}
	};
}
