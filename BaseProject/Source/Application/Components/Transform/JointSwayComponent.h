#pragma once

namespace App::Component
{
	//==========================================================================================
	// JointSwayComponent
	//
	// 関節をゆっくり揺らす : 決めた向き(restQuat)を中心に、決めた軸のまわりを行ったり来たりさせる。
	// 作業用アームのように、部品ごとのモデルを親子(HierarchyComponent)でつないだものの関節に付ける。
	// 骨(スキニング)もアニメーションの資産も要らない。
	//
	//   角度 = amplitude * sin(2π * (time / period + phase))
	//   向き = 揺れ(軸まわりに角度) を当ててから restQuat
	//
	// 書くのは LocalTransformComponent の quat。向きの中心は restQuat が持つので、
	// LocalTransform の回転を手で変えても毎フレーム上書きされる(中心を変えるときは restQuat を変える)。
	// 揺れている途中の向きを保存しても、次に開いたときは restQuat から揺れ直すので崩れない。
	//
	// 書き換えるのは JointSwaySystem。
	//==========================================================================================
	struct JointSwayComponent
	{
		// ---- 設定(保存される) ----

		// 揺れの中心になる向き(親からの相対)
		Math::Quaternion restQuat = Math::Quaternion::Identity();

		// 回す軸(自分のローカル)。長さは問わない
		Math::Vector3 axis = { 1.0f, 0.0f, 0.0f };

		float amplitude = 10.0f;	// 振れ幅(度)。中心から片側
		float period = 6.0f;		// 1往復にかかる秒数
		float phase = 0.0f;			// 位相のずれ(0〜1 で1周)。同じ周期の関節をずらして機械らしくする

		// ---- 実行中(保存しない) ----
		float time = 0.0f;			// 経過時間(秒)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::JointSwayComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::JointSwayComponent& _comp = Engine::EditorField::RefValue<App::Component::JointSwayComponent>(a_pData);
		a_ar.Field("restQuat", _comp.restQuat);
		a_ar.Field("axis", _comp.axis);
		a_ar.Field("amplitude", _comp.amplitude);
		a_ar.Field("period", _comp.period);
		a_ar.Field("phase", _comp.phase);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::JointSwayComponent& _comp = Engine::EditorField::RefValue<App::Component::JointSwayComponent>(a_context.pData);

		Engine::EditorField::Field("RestRotation", _comp.restQuat);
		Engine::EditorField::Tooltip("揺れの中心になる向き(親からの相対)。LocalTransform の回転は毎フレーム上書きされる");

		Engine::EditorField::Field("Axis", _comp.axis, 0.01f, -1.0f, 1.0f);
		Engine::EditorField::Tooltip("回す軸(自分のローカル)");

		Engine::EditorField::Field("Amplitude", _comp.amplitude, 0.1f, 0.0f, 180.0f);
		Engine::EditorField::Tooltip("振れ幅(度)。中心から片側");

		Engine::EditorField::Field("Period", _comp.period, 0.05f, 0.1f, 120.0f);
		Engine::EditorField::Tooltip("1往復にかかる秒数");

		Engine::EditorField::Field("Phase", _comp.phase, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Tooltip("位相のずれ(0〜1 で1周)");
	}
};
