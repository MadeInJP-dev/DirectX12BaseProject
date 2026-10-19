#include "ScenePhysics.h"

#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/EditorField/EditorField.h"

namespace Engine::Scene
{
	//======================================================================================
	// 流し込む先
	//======================================================================================
	void ScenePhysics::Register(Physics::PhysicsWorld* a_pPhysicsWorld)
	{
		m_pPhysicsWorld = a_pPhysicsWorld;

		// シーンファイルに "Physics" が無くても既定値で揃うよう、登録した時点で一度流し込む
		Apply();
	}

	void ScenePhysics::Unregister()
	{
		m_pPhysicsWorld = nullptr;
	}

	void ScenePhysics::Apply()
	{
		if (!m_pPhysicsWorld) return;

		m_pPhysicsWorld->SetGravity(m_gravity);
	}

	//======================================================================================
	// 保存・読み込み
	//======================================================================================
	void ScenePhysics::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("Gravity", m_gravity);

		if (a_ar.IsLoading()) Apply();
	}

	//======================================================================================
	// 編集UI
	//======================================================================================
	void ScenePhysics::DrawEdit()
	{
		Engine::EditorField::HelpText("このシーンの重力。落下を自前で解くもの(死亡モーション・破片など)もこれを使う");

		if (Engine::EditorField::Field("Gravity", m_gravity, 0.05f))
		{
			// 動かした瞬間に効かせる
			Apply();
		}
		Engine::EditorField::Tooltip("(m/秒^2。既定は 0, -9.81, 0)");
	}
}
