#pragma once

#include "Engine/EditorField/EditorField.h"
#include "Engine/ECS/World/World.h"

namespace App::Component
{
	//==========================================================================================
	// GroundDustEmitterComponent
	//
	// 機体が地面の近くを速く動いているときに、真下の地表へ砂埃を炊くための設定と状態。
	// ワームの体(ボイド)が炊く砂埃(WarmGroundEffectComponent / BoidGroundEffectSystem)と同じ作り。
	//
	//   真下へ maxHeight だけレイを打ち、地面に当たれば当たった所へ単発のエフェクトを出す。
	//   ・高さ : 地表すれすれで nearScale、maxHeight ぎりぎりで farScale
	//   ・速さ : minSpeed で炊き始め、fullSpeed で一番濃くなる。
	//            速いほど間隔を詰め(slowInterval → fastInterval)、大きく(slowScale → 1)する
	//   ・向き : 地面の法線を、進む向きの逆へ kickBack だけ倒す(後ろへ巻き上げる)
	//
	// ・速さは実速度(ActualVelocityComponent)の水平成分。落ちているだけでは炊かない。
	// ・レイを打つのは残り時間(timer)が切れたときだけ。
	// ・地面として見るのは StaticObject だけ(BoidGroundEffectSystem と同じ)。
	// ・エフェクトは遅延生成で、出し切ったら自分から消える単発のもの。
	// ・レイを打ってエフェクトを出すのは GroundDustEmitterSystem。
	//==========================================================================================
	struct GroundDustEmitterComponent
	{
		//------------------------------------------------------------------
		// 設定(保存する)
		//------------------------------------------------------------------
		Core::GUID effectGUID = Core::DEFAULT_GUID;	// 炊くエフェクト(出し切って消える単発のもの)

		float maxHeight    = 12.0f;		// 地面からこの高さまでなら炊く(m)
		float rayStartUp   = 1.0f;		// レイの始点を足元からこれだけ上げる(m)。
										// 足元から打つと、立っているときは始点が地表と重なって当たらない
		float nearScale    = 1.0f;		// 地表すれすれでの大きさ
		float farScale     = 0.3f;		// maxHeight ぎりぎりでの大きさ

		float minSpeed     = 15.0f;		// この速さ(m/秒)から炊き始める
		float fullSpeed    = 80.0f;		// この速さ(m/秒)で一番濃くなる
		float slowInterval = 0.25f;		// minSpeed での炊く間隔(秒。実際は ±25% ばらす)
		float fastInterval = 0.04f;		// fullSpeed での炊く間隔(秒)
		float slowScale    = 0.5f;		// minSpeed での大きさの倍率(fullSpeed で 1)

		float kickBack     = 0.6f;		// 巻き上げる向きを、進む向きの逆へどれだけ倒すか(0 で真上)

		//------------------------------------------------------------------
		// 実行中の状態(保存しない)
		//------------------------------------------------------------------
		float timer = 0.0f;				// 次に炊くまでの残り時間(秒)
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::GroundDustEmitterComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::GroundDustEmitterComponent& _comp = Engine::EditorField::RefValue<App::Component::GroundDustEmitterComponent>(a_pData);

		// 残り時間は実行中の値なので保存しない
		a_ar.Field("effectGUID", _comp.effectGUID);
		a_ar.Field("maxHeight", _comp.maxHeight);
		a_ar.Field("nearScale", _comp.nearScale);
		a_ar.Field("farScale", _comp.farScale);
		a_ar.Field("minSpeed", _comp.minSpeed);
		a_ar.Field("fullSpeed", _comp.fullSpeed);
		a_ar.Field("slowInterval", _comp.slowInterval);
		a_ar.Field("fastInterval", _comp.fastInterval);
		a_ar.Field("slowScale", _comp.slowScale);
		a_ar.Field("kickBack", _comp.kickBack);
		a_ar.Field("rayStartUp", _comp.rayStartUp);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::GroundDustEmitterComponent& _comp = Engine::EditorField::RefValue<App::Component::GroundDustEmitterComponent>(a_context.pData);

		Engine::EditorField::AssetField(*a_context.pWorld->RefEngineServices(), "Effect", "EffectAsset", _comp.effectGUID);
		if (_comp.effectGUID == Core::DEFAULT_GUID)
		{
			Engine::EditorField::HelpText("(未設定 : 砂埃は出ない)");
		}

		Engine::EditorField::Header("Height");
		Engine::EditorField::Field("MaxHeight (m)", _comp.maxHeight, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("地面からこの高さまでなら炊く");
		Engine::EditorField::Field("RayStartUp (m)", _comp.rayStartUp, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("レイの始点を足元からこれだけ上げる(立っているときに地表と重ならないように)");
		Engine::EditorField::Field("NearScale", _comp.nearScale, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("地表すれすれでの大きさ");
		Engine::EditorField::Field("FarScale", _comp.farScale, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("MaxHeight ぎりぎりでの大きさ");

		Engine::EditorField::Header("Speed");
		Engine::EditorField::Field("MinSpeed (m/s)", _comp.minSpeed, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("この速さから炊き始める(水平の実速度)");
		Engine::EditorField::Field("FullSpeed (m/s)", _comp.fullSpeed, 0.1f, 0.0f);
		Engine::EditorField::Tooltip("この速さで一番濃くなる");
		Engine::EditorField::Field("SlowInterval (s)", _comp.slowInterval, 0.005f, 0.01f);
		Engine::EditorField::Field("FastInterval (s)", _comp.fastInterval, 0.005f, 0.01f);
		Engine::EditorField::Field("SlowScale", _comp.slowScale, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("MinSpeed での大きさの倍率(FullSpeed で 1)");

		Engine::EditorField::Header("Direction");
		Engine::EditorField::Field("KickBack", _comp.kickBack, 0.01f, 0.0f, 4.0f);
		Engine::EditorField::Tooltip("巻き上げる向きを進む向きの逆へ倒す量(0 で地面の法線そのまま)");
	}
};
