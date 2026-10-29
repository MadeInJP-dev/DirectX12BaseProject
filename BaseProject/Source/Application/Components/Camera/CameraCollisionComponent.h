#pragma once

namespace App::Component
{
	//==========================================================================================
	// CameraCollisionComponent
	//
	// TPSカメラが地形にめり込まないための設定。カメラ本体に付ける。
	// 付いていないカメラも既定値で判定する(TPSSystem が既定値を使う)ので、
	// 調整したいときだけ付ければよい。
	//
	// ・障害物 : 自機(ピボットの高さ)からカメラへレイを撃ち、間に地形があれば
	//            当たった点の手前までカメラを寄せる。寄せるのは速く、戻すのはゆっくり補間する。
	// ・地面   : カメラの少し上から真下へレイを撃ち、地表 + groundClearance より下へは行かせない。
	// ・フェード : 障害物のレイに掛かった地形と自機を、カメラからの距離で透かす
	//            (ModelComponent::fade。寄せるのは ModelFadeSystem、画素を捨てるのは GBuffer / ZPre)。
	//
	// 当たりに行くのは静的なもの(StaticObject)だけ。敵や弾では押し出さない。
	// カメラの向きは触らない(位置だけ動かす)ので、照準はブレない。
	//==========================================================================================
	struct CameraCollisionComponent
	{
		// ---- 障害物(自機 → カメラ) ----
		bool isObstacleEnabled = true;

		// 当たった面からどれだけ手前で止めるか(m)。近面で壁を切らないための余白
		float obstacleMargin = 0.3f;

		// 自機からこれより近くへは寄せない(m)。寄りすぎて機体の中に入るのを防ぐ
		float minDistance = 0.5f;

		// 寄せる速さ / 戻す速さ(1秒あたりの指数減衰レート。0 以下なら補間せずにすぐ合わせる)。
		// 寄せるのが遅いと壁の向こうが見えるので速く、戻すのは遅くして構図が跳ねないようにする
		float pullInRate	= 25.0f;
		float releaseRate	= 4.0f;

		// ---- 地面(カメラ → 下) ----
		bool isGroundEnabled = true;

		// 地表からカメラまでの最低の高さ(m)
		float groundClearance = 0.5f;

		// レイの始点をカメラからどれだけ上げるか(m)。
		// カメラが既に地面の下へ入っていても、地表を上から拾えるようにするため
		float groundProbeUp = 2.0f;

		// 押し上げる速さ / 戻す速さ(1秒あたりの指数減衰レート。0 以下なら補間せずにすぐ合わせる)
		float groundPushRate	= 30.0f;
		float groundReleaseRate	= 6.0f;

		// ---- フェード(ModelComponent::fade。GBuffer でアルファディザー) ----
		// どちらも「カメラからの距離」で決める。Near 以下で 0(見えない)、Far 以上で 1(見える)
		bool isFadeEnabled = true;

		// 障害物 : 自機 → カメラのレイに掛かった地形(当たった点とカメラの距離)
		float obstacleFadeNear	= 0.5f;
		float obstacleFadeFar	= 3.0f;

		// これより大きいモデル(ワールドでの境界球の半径・m)は消さない。
		// 地面や岩山のような大きな地形まで消えて、足元が抜けるのを防ぐ。0 以下なら大きさを見ない
		float obstacleFadeMaxRadius = 30.0f;

		// 自機 : 自機のピボットとカメラの距離。子(武器・外装)も一緒に消える
		float playerFadeNear	= 1.0f;
		float playerFadeFar		= 2.5f;

		// 消える速さ / 戻る速さ(1秒あたりの指数減衰レート。0 以下なら補間せずにすぐ合わせる)
		float fadeOutRate	= 15.0f;
		float fadeInRate	= 5.0f;
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::CameraCollisionComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::CameraCollisionComponent& _comp = Engine::EditorField::RefValue<App::Component::CameraCollisionComponent>(a_pData);
		a_ar.Field("isObstacleEnabled", _comp.isObstacleEnabled);
		a_ar.Field("obstacleMargin", _comp.obstacleMargin);
		a_ar.Field("minDistance", _comp.minDistance);
		a_ar.Field("pullInRate", _comp.pullInRate);
		a_ar.Field("releaseRate", _comp.releaseRate);
		a_ar.Field("isGroundEnabled", _comp.isGroundEnabled);
		a_ar.Field("groundClearance", _comp.groundClearance);
		a_ar.Field("groundProbeUp", _comp.groundProbeUp);
		a_ar.Field("groundPushRate", _comp.groundPushRate);
		a_ar.Field("groundReleaseRate", _comp.groundReleaseRate);
		a_ar.Field("isFadeEnabled", _comp.isFadeEnabled);
		a_ar.Field("obstacleFadeNear", _comp.obstacleFadeNear);
		a_ar.Field("obstacleFadeFar", _comp.obstacleFadeFar);
		a_ar.Field("obstacleFadeMaxRadius", _comp.obstacleFadeMaxRadius);
		a_ar.Field("playerFadeNear", _comp.playerFadeNear);
		a_ar.Field("playerFadeFar", _comp.playerFadeFar);
		a_ar.Field("fadeOutRate", _comp.fadeOutRate);
		a_ar.Field("fadeInRate", _comp.fadeInRate);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::CameraCollisionComponent& _comp = Engine::EditorField::RefValue<App::Component::CameraCollisionComponent>(a_context.pData);

		Engine::EditorField::Header("Obstacle");
		Engine::EditorField::Field("IsObstacleEnabled", _comp.isObstacleEnabled);
		Engine::EditorField::Field("Margin (m)", _comp.obstacleMargin, 0.01f, 0.0f, 5.0f);
		Engine::EditorField::Tooltip("当たった面からどれだけ手前で止めるか");
		Engine::EditorField::Field("MinDistance (m)", _comp.minDistance, 0.01f, 0.0f, 10.0f);
		Engine::EditorField::Field("PullInRate", _comp.pullInRate, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Tooltip("寄せる速さ。0 ですぐ寄せる");
		Engine::EditorField::Field("ReleaseRate", _comp.releaseRate, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Tooltip("戻す速さ。0 ですぐ戻す");

		Engine::EditorField::Header("Ground");
		Engine::EditorField::Field("IsGroundEnabled", _comp.isGroundEnabled);
		Engine::EditorField::Field("Clearance (m)", _comp.groundClearance, 0.01f, 0.0f, 10.0f);
		Engine::EditorField::Tooltip("地表からカメラまでの最低の高さ");
		Engine::EditorField::Field("ProbeUp (m)", _comp.groundProbeUp, 0.05f, 0.0f, 20.0f);
		Engine::EditorField::Tooltip("レイの始点をカメラからどれだけ上げるか");
		Engine::EditorField::Field("PushRate", _comp.groundPushRate, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Tooltip("押し上げる速さ。0 ですぐ押し上げる");
		Engine::EditorField::Field("GroundReleaseRate", _comp.groundReleaseRate, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Tooltip("戻す速さ。0 ですぐ戻す");

		Engine::EditorField::Header("Fade");
		Engine::EditorField::Field("IsFadeEnabled", _comp.isFadeEnabled);
		Engine::EditorField::Field("ObstacleFadeNear (m)", _comp.obstacleFadeNear, 0.05f, 0.0f, 50.0f);
		Engine::EditorField::Field("ObstacleFadeFar (m)", _comp.obstacleFadeFar, 0.05f, 0.0f, 50.0f);
		Engine::EditorField::Tooltip("カメラから Near 以下で見えなくなり、Far 以上で見える");
		Engine::EditorField::Field("ObstacleFadeMaxRadius (m)", _comp.obstacleFadeMaxRadius, 0.5f, 0.0f, 1000.0f);
		Engine::EditorField::Tooltip("これより大きい地形は消さない。0 で大きさを見ない");
		Engine::EditorField::Field("PlayerFadeNear (m)", _comp.playerFadeNear, 0.05f, 0.0f, 50.0f);
		Engine::EditorField::Field("PlayerFadeFar (m)", _comp.playerFadeFar, 0.05f, 0.0f, 50.0f);
		Engine::EditorField::Tooltip("自機とカメラの距離。Near 以下で見えなくなり、Far 以上で見える");
		Engine::EditorField::Field("FadeOutRate", _comp.fadeOutRate, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Field("FadeInRate", _comp.fadeInRate, 0.1f, 0.0f, 120.0f);
		Engine::EditorField::Tooltip("消える / 戻る速さ。0 ですぐ合わせる");
	}
};
