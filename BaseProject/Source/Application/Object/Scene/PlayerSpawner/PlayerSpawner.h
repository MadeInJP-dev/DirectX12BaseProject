#pragma once

#include "Engine/GameObject/BaseObject/BaseObject.h"

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

		// 出したプレイヤーのルート。出していなければ INVALID
		Engine::ECS::Entity m_playerEntity = Engine::ECS::Limits::INVALID_ENTITY;

		// 出そうとしたか。失敗しても毎フレーム出し直さない(警告が流れ続けるため)
		bool m_isSpawnTried = false;
	};
}
