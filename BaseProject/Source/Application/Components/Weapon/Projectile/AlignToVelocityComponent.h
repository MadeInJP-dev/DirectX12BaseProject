#pragma once

namespace App::Component
{
	//==========================================================================================
	// AlignToVelocityComponent
	//
	// 進む向き(DesiredVelocityComponent)へ見た目の向きを合わせる。
	// 細長い弾やミサイルのモデルに付ける(球の弾なら要らない)。
	//
	// ・向けるのはモデルのローカル +Z。モデルは先端を +Z(glTF では -Z)に作ること。
	// ・回すのは AlignToVelocitySystem。誘導(HomingSystem)で曲がった向きにも毎フレーム合わせる。
	//==========================================================================================
	struct AlignToVelocityComponent
	{
		float minSpeed = 0.1f;		// これより遅いときは向きを変えない(止まりかけで向きが暴れないように)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::AlignToVelocityComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::AlignToVelocityComponent& _comp = Engine::EditorField::RefValue<App::Component::AlignToVelocityComponent>(a_pData);
		a_ar.Field("minSpeed", _comp.minSpeed);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::AlignToVelocityComponent& _comp = Engine::EditorField::RefValue<App::Component::AlignToVelocityComponent>(a_context.pData);
		Engine::EditorField::Field("MinSpeed", _comp.minSpeed, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("これより遅いときは向きを変えない");
	}
};
