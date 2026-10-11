#pragma once

#include "Engine/ECS/World/World.h"

namespace App::Game
{
	class UserData;
}

namespace App::Input
{
	class InputActionManager;
}

namespace App::InstanceResource
{
	//==========================================================================================
	//
	// ユーザーデータ(キー割り当て・感度・音量など、プレイヤーが設定画面で変えるもの)への
	// 入口を置くワールドリソース。
	//
	// 実体は GameManager が1つだけ持っている(シーンを切り替えても消えない)。
	// ここに置くのは借りたポインタだけで、GameDataResource と同じく
	// ワールドを作るとき(GameManager が差し込むワールドの作り手)に詰められる。
	//
	// 書く側 : 設定画面(OptionHUD のパネル)。値を変えた瞬間に書き込む
	// 保存   : 設定画面を閉じたときに UserData::Save
	//
	//==========================================================================================
	struct UserDataResource
	{
		Game::UserData* pUserData = nullptr;

		// キー割り当てを書き換えたら Apply を通す先(入力デバイスへ流し直す)
		Input::InputActionManager* pInputActionManager = nullptr;

		/// <summary>
		/// ワールドから入口を引く。置かれていなければ nullptr
		/// </summary>
		/// <remarks>
		/// エディターの確認用ワールドなど、作り手を通らずに作られたワールドには無い
		/// </remarks>
		static UserDataResource* Find(Engine::ECS::World* a_pWorld)
		{
			if (!a_pWorld || !a_pWorld->HasResource<UserDataResource>()) return nullptr;
			return &a_pWorld->RefResource<UserDataResource>();
		}
	};
}
