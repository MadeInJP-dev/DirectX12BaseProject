#pragma once

namespace App::Component
{
	// TPSカメラの実行時状態(TPSSystem が毎フレーム更新する)
	struct TPSCameraStateComponent
	{
		// 現在の注視点。ワールドの絶対座標ではなく「カメラ空間(オービット基準)の相対座標」で持つ。
		// 左手系なので +Z が視線の奥、+X が画面右、+Y が画面上。
		// 機体の姿勢で解決すると、胴体が視線と別方向を向いた瞬間に構図が振られる。
		Math::Vector3 currentLookAt = { 0.0f, 0.0f, 0.0f };
		Math::Quaternion currentOrbit  = { 0.0f, 0.0f, 0.0f, 1.0f }; // 現在のオービット回転(Slerp補間用)

		// currentLookAt をワールドへ解決した結果。TPSSystem が毎フレーム書く。
		// 他システム(AimTargetSystem)がオービットの式を再現しなくて済むように置いている。
		// 実行時の派生値なので保存しない。
		Math::Vector3 lookAtWorld = { 0.0f, 0.0f, 0.0f };

		Math::Vector3 currentPivot  = { 0.0f, 0.0f, 0.0f }; // 現在のピボット。ターゲットへ遅れて追従する
		float currentPullBack = 0.0f;							// 現在の引き量(m)。速度に応じて伸びる

		// 速度レスポンスの実行時値(保存しない)
		float currentSpeed01  = 0.0f;	// 速度の正規化値(0..1)。引き/追従/画角の効きはすべてこれ基準
		float currentFovAdd   = 0.0f;	// 現在の画角の上乗せ(度)。CameraParamComponent.fovBoost へ入る

		// 地形との判定の実行時値(保存しない。CameraCollisionComponent の設定で動く)
		float currentCollisionPull		= -1.0f;	// 障害物のぶん自機へ寄せている量(m)。負 = まだ無い(次の判定で合わせる)
		float currentGroundLift			= 0.0f;		// 地面から押し上げている量(m)
		bool  isObstacleHit				= false;	// 自機とカメラの間に地形があるか
		bool  isGroundHit				= false;	// カメラの下に地面が見つかったか

		// フェードの依頼(実行時の値。保存しない)。TPSSystem が毎フレーム書き、ModelFadeSystem が
		// 該当するモデル(そのエンティティと子孫)の ModelComponent::fade をここへ寄せる。
		//   fadeTarget         : 追従先(自機)。距離で決めた playerFade を当てる
		//   fadeObstacles[k]   : 自機 → カメラのレイに掛かった地形。距離で決めた fadeObstacleValues[k] を当てる
		static constexpr uint32_t FADE_OBSTACLE_MAX = 4;
		Engine::ECS::Entity fadeTarget = Engine::ECS::Limits::INVALID_ENTITY;
		float playerFade = 1.0f;
		std::array<Engine::ECS::Entity, FADE_OBSTACLE_MAX> fadeObstacles = {};
		std::array<float, FADE_OBSTACLE_MAX> fadeObstacleValues = {};
		uint32_t fadeObstacleCount = 0;

		// 追従状態が一度でも作られたか。
		// 保存しないので、シーンをロードした直後の1フレーム目は必ずターゲットへスナップする
		// (原点から飛んでくるのを防ぐため)。
		bool isInitialized = false;
	};
}

template<>
struct Engine::ECS::ComponentTraits<App::Component::TPSCameraStateComponent>
{
	static void Archive(Engine::Persistence::Archive& a_ar, void* a_pData)
	{
		App::Component::TPSCameraStateComponent& _comp = Engine::EditorField::RefValue<App::Component::TPSCameraStateComponent>(a_pData);
		a_ar.Field("currentLookAt", _comp.currentLookAt);
		a_ar.Field("currentOrbit", _comp.currentOrbit);
	}

	static void Edit(CompEditContext& a_context)
	{
		App::Component::TPSCameraStateComponent& _comp = Engine::EditorField::RefValue<App::Component::TPSCameraStateComponent>(a_context.pData);

		// システムが毎フレーム上書きするので表示のみ
		Engine::EditorField::Value("LookAtCamera", "%.2f, %.2f, %.2f", _comp.currentLookAt.x, _comp.currentLookAt.y, _comp.currentLookAt.z);
		Engine::EditorField::Value("LookAtWorld", "%.2f, %.2f, %.2f", _comp.lookAtWorld.x, _comp.lookAtWorld.y, _comp.lookAtWorld.z);
		Engine::EditorField::Value("CurrentPivot", "%.2f, %.2f, %.2f", _comp.currentPivot.x, _comp.currentPivot.y, _comp.currentPivot.z);
		Engine::EditorField::Value("PullBack", "%.2f", _comp.currentPullBack);
		Engine::EditorField::Value("Speed01", "%.2f", _comp.currentSpeed01);
		Engine::EditorField::Value("FovAdd", "%.2f", _comp.currentFovAdd);
		Engine::EditorField::Value("CollisionPull", "%.2f (%s)", _comp.currentCollisionPull, _comp.isObstacleHit ? "hit" : "-");
		Engine::EditorField::Value("GroundLift", "%.2f (%s)", _comp.currentGroundLift, _comp.isGroundHit ? "hit" : "-");
		Engine::EditorField::Value("PlayerFade", "%.2f", _comp.playerFade);
		for (uint32_t _k = 0; _k < _comp.fadeObstacleCount && _k < App::Component::TPSCameraStateComponent::FADE_OBSTACLE_MAX; ++_k)
		{
			Engine::EditorField::Value("ObstacleFade", "%llu : %.2f",
				static_cast<unsigned long long>(_comp.fadeObstacles[_k]), _comp.fadeObstacleValues[_k]);
		}
	}
};
