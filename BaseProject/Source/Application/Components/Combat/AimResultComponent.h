#pragma once

namespace App::Component
{
	//==========================================================================================
	// AimResultComponent
	//
	// 狙点の結果(ワールド座標の狙点・狙いの向き・当たった相手)。
	// AimTargetSystem がカメラ正面へレイを飛ばし、その着弾点を書き込む。
	// 銃はこの座標へ向けて弾を発射する(WeaponMuzzleSystem)。
	//
	// ・銃側(子エンティティ)にも付けておくと、AttachmentDispatchSystem が親から狙点をコピーする。
	// ・ボスは BossCombatIntentSystem が偏差撃ちの狙点を書く。
	// ・以前は AimTargetPosComponent として、レイの設定(AimConfigComponent)と同じ型に入っていた。
	//   AimConfigComponent の必須コンポーネントなので、設定を持つものには自動で付く。保存しない。
	//==========================================================================================
	struct AimResultComponent
	{
		Math::Vector3 pos = { 0.0f,0.0f,0.0f };								// 狙点(ワールド座標)
		Math::Vector3 dir = { 0.0f,0.0f,1.0f };								// 狙いの向き(=カメラ前方。単位ベクトル)
		Engine::ECS::Entity hitEntity = Engine::ECS::Limits::INVALID_ENTITY;	// 当たった相手
		bool isHit = false;														// 何かに当たったか
		bool isValid = false;													// 一度でも計算されたか(未計算のpos=原点を撃たないため)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::AimResultComponent>
{
	static void Edit(CompEditContext& a_context)
	{
		App::Component::AimResultComponent& _comp = Engine::EditorField::RefValue<App::Component::AimResultComponent>(a_context.pData);

		// システムが毎フレーム上書きするので表示のみ
		Engine::EditorField::Value("AimPos", "%.2f, %.2f, %.2f", _comp.pos.x, _comp.pos.y, _comp.pos.z);
		Engine::EditorField::Value("AimDir", "%.2f, %.2f, %.2f", _comp.dir.x, _comp.dir.y, _comp.dir.z);
		Engine::EditorField::Value("IsHit", "%s", _comp.isHit ? "true" : "false");
		Engine::EditorField::Value("IsValid", "%s", _comp.isValid ? "true" : "false");
		Engine::EditorField::Tooltip("HitEntity : %llu", static_cast<unsigned long long>(_comp.hitEntity));
	}
};
