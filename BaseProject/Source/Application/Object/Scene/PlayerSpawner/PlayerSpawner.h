#pragma once

#include "Engine/GameObject/BaseObject/BaseObject.h"

namespace App::Game { struct PlayerLoadout; }

namespace App::Object
{
	//======================================================================================
	// プレイヤーのスポナー
	//
	// シーンにプレイヤーを直接置かず、ここからプレハブを出す。
	// 機体の中身(武器・ブースター・アニメーター)はプレハブ側だけで直せば
	// どのステージにも同じものが出るようにするため。
	//
	// ・出すのはシーンの始まりに1回だけ(Start)。
	//   Start の時点でプレハブが未設定なら、設定されたところで出す(エディターで置いた直後)。
	// ・プレイヤーは GUID を持たない。親子・アタッチメント・カメラの追従先は
	//   すべてエンティティIDで、出したときに付ける。
	// ・そのため即時生成で出す(遅延生成ではIDが返らない)。
	//   親子とアタッチメントは CreateInstanceNow が付け、カメラはここで付ける。
	//   Start / Update は ECS の反復の外なので、その場で CreateEntity してよい。
	// ・出したプレイヤーには TransientTag が付く(BuildSpawnInstanceData)ので、
	//   シーンを保存しても書き込まれない。シーンに残るのはこのスポナーだけ。
	// ・カメラはシーンの持ち物のまま(GUID で指定する)。
	//
	// 出撃構成(GlobalGameContext::loadout)
	// ・本体が設定されていれば、自分のプレハブではなくそちらを出す。
	//   ホームで選んだ機体と武器を、次に開いたゲームシーンへ持ち込むため。
	// ・本体を出したら、本体の子の取り付け口(WeaponMountComponent)へ武器のプレハブを子として出し、
	//   本体のアタッチメントのスロット(rightWeapon / leftWeapon / missile)へ ID で入れる。
	// ・構成の本体が未設定なら、自分のプレハブ(武器込みの一体物)をそのまま出す
	//   (出撃構成を持たない起動のしかたでも、ゲームシーンを単体で試せるように)。
	//======================================================================================
	class PlayerSpawner : public Engine::GameObject::BaseObject
	{
	public:

		// 開始処理 : プレイヤーを出してカメラへつなぐ
		void Start(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : 出し損ねていれば出す / 出現位置の目印を描く
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "PlayerSpawner"; }

		// インスペクター : プレハブ・出現位置・向き・カメラ
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

		// ギズモ : 出現位置を動かす
		bool DrawGizmo(
			const Engine::GameObject::ObjectGizmoContext& a_ctx,
			Engine::GameObject::ObjectContext& a_context) override;

	private:

		// プレハブからプレイヤーを出す。出せたら true
		bool SpawnPlayer(Engine::GameObject::ObjectContext& a_context);

		// 出撃構成を引く。本体が設定されていなければ nullptr(自分のプレハブを出す)
		const Game::PlayerLoadout* FindLoadout(Engine::GameObject::ObjectContext& a_context) const;

		// 出す本体のプレハブ(出撃構成の本体、無ければ自分のプレハブ)
		Core::GUID ResolveBodyPrefabGUID(Engine::GameObject::ObjectContext& a_context) const;

		// 出撃構成の武器を本体の取り付け口へ付け、スロットへつなぐ
		// a_bodyEntityVec : 出した本体のエンティティ(先頭がルート)
		void AttachLoadoutWeapons(
			Engine::GameObject::ObjectContext& a_context,
			const Game::PlayerLoadout& a_loadout,
			const std::vector<Engine::ECS::Entity>& a_bodyEntityVec);

		// カメラの追従先を出したプレイヤーへ付ける
		void LinkCamera(Engine::GameObject::ObjectContext& a_context);

		// 出現位置と向きの目印を描く
		void DrawSpawnMarker(Engine::GameObject::ObjectContext& a_context) const;

	private:

		//-------------------------------------------------------------------
		// 設定(保存される)
		//-------------------------------------------------------------------
		Core::GUID    m_prefabGUID = Core::DEFAULT_GUID;	// 出すプレイヤーのプレハブ
		Math::Vector3 m_pos        = {};						// 出現位置(ワールド)
		float         m_yaw        = 0.0f;					// 向き(度。0 = +Z 前方。LookAngleComponent と同じ規約)

		// 追従させるカメラ(エンティティの GUID)。未設定ならカメラは触らない
		Core::GUID    m_cameraGUID = Core::DEFAULT_GUID;

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		Engine::ResourceRef<Engine::Resource::Prefab> m_prefabRef = {};

		// 付けた武器のプレハブ(取り付け口の種類ごと。RightHand / LeftHand / Shoulder)
		static constexpr size_t WEAPON_MOUNT_COUNT = 4;
		std::array<Engine::ResourceRef<Engine::Resource::Prefab>, WEAPON_MOUNT_COUNT> m_weaponPrefabRefArray = {};

		// 出したプレイヤーのルート。出していなければ INVALID
		Engine::ECS::Entity m_playerEntity = Engine::ECS::Limits::INVALID_ENTITY;

		// 出そうとしたか。失敗しても毎フレーム出し直さない(警告が流れ続けるため)
		bool m_isSpawnTried = false;
	};
}
