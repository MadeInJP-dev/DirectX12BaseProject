#pragma once

#include "Engine/GameObject/BaseObject/BaseObject.h"

#include "Application/Components/Attachment/WeaponMountComponent.h"

namespace App::Game { struct PlayerLoadout; }
namespace App::Component { struct AttachmentSlot; }

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
	//
	// 見せるだけの出し方(ガレージ)
	// ・IsControllable を切ると、出したあと操作の印(PlayerControllTag)を外す。入力では動かない。
	// ・取り付け口の武器はあとから付け替えられる(ReplaceWeapon)。付け替えるのは出した機体の武器だけで、
	//   出撃構成(GlobalGameContext::loadout)は呼んだ側が書き換える。
	//======================================================================================
	class PlayerSpawner final : public Engine::GameObject::BaseObject
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

		//=======================================================================
		// 出した機体(ガレージなど、外から見せたり付け替えたりする用)
		//=======================================================================

		// 出したプレイヤーのルート。出していなければ INVALID
		Engine::ECS::Entity GetPlayerEntity() const { return m_playerEntity; }

		// 取り付け口のエンティティ。その口が無い機体・出していなければ INVALID
		Engine::ECS::Entity GetMountEntity(Component::EWeaponMount a_mount) const;

		// 取り付け口に付いている武器。付いていなければ INVALID
		Engine::ECS::Entity GetWeaponEntity(Component::EWeaponMount a_mount) const;

		/// <summary>
		/// 取り付け口の武器を付け替える
		/// </summary>
		/// <param name="a_prefabGUID">付ける武器のプレハブ。空なら外すだけ</param>
		/// <returns>付けられた(空なら外せた)ら true</returns>
		/// <remarks>
		/// 今の武器は解放予約して、新しい武器をその場で子として出す(GameObject の更新から呼ぶこと)。
		/// 出撃構成は書き換えない
		/// </remarks>
		bool ReplaceWeapon(
			Engine::GameObject::ObjectContext& a_context,
			Component::EWeaponMount a_mount,
			const Core::GUID& a_prefabGUID);

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

		// 武器を1つ取り付け口へ付け、スロットへつなぐ。付けた武器を返す(付けられなければ INVALID)
		Engine::ECS::Entity AttachWeapon(
			Engine::GameObject::ObjectContext& a_context,
			Component::EWeaponMount a_mount,
			const Core::GUID& a_prefabGUID);

		// 取り付け口に対応する本体のスロット。本体が居なければ nullptr
		Component::AttachmentSlot* RefMountSlot(Engine::ECS::World& a_world, Component::EWeaponMount a_mount) const;

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

		// 入力で動かすか。切ると出したあと PlayerControllTag を外す(ガレージで見せるだけの機体)
		bool          m_isControllable = true;

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		Engine::ResourceRef<Engine::Resource::Prefab> m_prefabRef = {};

		// 付けた武器のプレハブ・取り付け口・付けた武器(取り付け口の種類ごと。EWeaponMount の順)
		static constexpr size_t WEAPON_MOUNT_COUNT = 4;

		// 0 は有効なエンティティ番号なので、未設定は INVALID で埋めておく
		static std::array<Engine::ECS::Entity, WEAPON_MOUNT_COUNT> MakeInvalidEntityArray()
		{
			std::array<Engine::ECS::Entity, WEAPON_MOUNT_COUNT> _array = {};
			_array.fill(Engine::ECS::Limits::INVALID_ENTITY);
			return _array;
		}
		std::array<Engine::ResourceRef<Engine::Resource::Prefab>, WEAPON_MOUNT_COUNT> m_weaponPrefabRefArray = {};
		std::array<Engine::ECS::Entity, WEAPON_MOUNT_COUNT> m_mountEntityArray = MakeInvalidEntityArray();
		std::array<Engine::ECS::Entity, WEAPON_MOUNT_COUNT> m_weaponEntityArray = MakeInvalidEntityArray();

		// 出したプレイヤーのルート。出していなければ INVALID
		Engine::ECS::Entity m_playerEntity = Engine::ECS::Limits::INVALID_ENTITY;

		// 出そうとしたか。失敗しても毎フレーム出し直さない(警告が流れ続けるため)
		bool m_isSpawnTried = false;
	};
}
