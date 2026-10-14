#pragma once

namespace App::Component
{
	//==========================================================================================
	// WeaponTriggerComponent
	//
	// 武器エンティティが外から受け取る「命令」だけを持つコンポーネント。
	//
	// 持ち主(プレイヤー・敵)は「左を撃て / 右を撃て」としか言わない。
	// 撃てるかどうか(連射間隔・バースト・熱・弾倉)や、何をどう撃つか(弾・弾速・銃口)は
	// 武器側のコンポーネント(WeaponConfig / Hand 以下)が持ち、武器のシステムが面倒を見る。
	//
	// 引き金を引くのは
	//   ・持ち主が武器を子として持つ場合 : AttachmentDispatchSystem
	//   ・持ち主自身が武器を兼ねる場合   : SelfWeaponTriggerSystem
	// のどちらか。武器側から持ち主を見に行くことはしない。
	//
	// 肩の武器(ミサイル)は引き金ではなく一斉射の要求(MissileFireComponent)で撃つので、これを持たない。
	//
	// 保存するものは無い(毎フレーム上書きされる命令なので)。
	//==========================================================================================
	struct WeaponTriggerComponent
	{
		// 引き金を引かれているか。押されている間ずっと true
		bool isPulled = false;
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::WeaponTriggerComponent>
{
	static void Edit(CompEditContext& a_context)
	{
		App::Component::WeaponTriggerComponent& _comp = Engine::EditorField::RefValue<App::Component::WeaponTriggerComponent>(a_context.pData);

		// 配信された結果を見るだけ。ここから触っても次のフレームで上書きされる
		bool _pulled = _comp.isPulled;
		Engine::EditorField::DisabledScope _disabled(true);
		Engine::EditorField::Field("isPulled", _pulled);
	}
};
