#include "WorldTypeRegister.h"

#include "APPWorld.h"

// コンポーネント関係
// システムフェーズタグ
#include "Application/Components/Core/PhaseTag/PostDeserializeTag.h"
#include "Application/Components/Core/PhaseTag/AwakeTag.h"
#include "Application/Components/Core/PhaseTag/StartTag.h"
#include "Application/Components/Core/PhaseTag/ActiveTag.h"

// コンポーネント
#include "Application/Components/Render/RayTag.h"
#include "Application/Components/Camera/CameraTag.h"
#include "Application/Components/Input/PlayerControllTag.h"
#include "Application/Components/Camera/CameraControllTag.h"
#include "Application/Components/Camera/CameraParamComponent.h"
#include "Application/Components/Camera/FocusParamComponent.h"
#include "Application/Components/Camera/RadialBlurComponent.h"
#include "Application/Components/Camera/SpeedLineComponent.h"
#include "Application/Components/Camera/FishEyeComponent.h"
#include "Application/Components/Camera/ProjMatComponent.h"
#include "Application/Components/Camera/FollowTargetComponent.h"
#include "Application/Components/Movement/GravityComponent.h"
#include "Application/Components/Movement/DesiredVelocityComponent.h"
#include "Application/Components/Movement/MovementParamsComponent.h"
#include "Application/Components/Movement/LookAngleComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Movement/MoveIntentComponent.h"
#include "Application/Components/Physics/Collider.h"
#include "Application/Components/Physics/RayCollider.h"
#include "Application/Components/Render/ModelComponent.h"
#include "Application/Components/Render/EmissiveOverrideComponent.h"
#include "Application/Components/Render/DynamicRaytracingComponent.h"
#include "Application/Components/Physics/GroundStateComponent.h"
#include "Application/Components/Boid/BoidWaveStateComponent.h"
#include "Application/Components/Animation/AnimatorComponent.h"
#include "Application/Components/Animation/UpperAnimatorComponent.h"
#include "Application/Components/Animation/LowerBodyTurnComponent.h"
#include "Application/Components/Animation/StancePoseComponent.h"
#include "Application/Components/Animation/SkeletonPoseComponent.h"
#include "Application/Components/Animation/NodePoseComponent.h"
#include "Application/Components/Render/UIComponent.h"
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Engine/ECS/Component/TransientTag.h"
#include "Application/Components/Core/NameComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Components/Transform/JointSwayComponent.h"
#include "Application/Components/Transform/FollowAnimationNodeComponent.h"
#include "Application/Components/Core/SpawnerComponent.h"
#include "Application/Components/Transform/PreviousWorldMatrixComponent.h"
#include "Application/Components/Movement/BoostParamsComponent.h"
#include "Application/Components/Effect/BoosterEffectComponent.h"
#include "Application/Components/Movement/ChargeDashComponent.h"
#include "Application/Components/Combat/ScoreTargetComponent.h"
#include "Application/Components/Render/PointLightComponent.h"
#include "Application/Components/Attachment/AttachmentSlotsComponent.h"
#include "Application/Components/Camera/TPSCameraStateComponent.h"
#include "Application/Components/Camera/TPSFollowComponent.h"
#include "Application/Components/Physics/SphereCollider.h"
#include "Application/Components/Enemy/EnemyTag.h"
#include "Application/Components/Physics/CapsuleCollider.h"
#include "Application/Components/Combat/ActionIntentComponent.h"
#include "Application/Components/Weapon/WeaponTriggerComponent.h"
#include "Application/Components/Attachment/WeaponMountComponent.h"
#include "Application/Components/Weapon/WeaponFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/MuzzleComponent.h"
#include "Application/Components/Weapon/WeaponConfig/AutoFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/BurstFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/SemiAutoFireComponent.h"
#include "Application/Components/Weapon/WeaponConfig/WeaponOverHeatComponent.h"
#include "Application/Components/Weapon/WeaponConfig/WeaponReloadComponent.h"
#include "Application/Components/Weapon/Hand/WeaponStateComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileStateComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileFireComponent.h"
#include "Engine/ECS/Component/CollisionEvent.h"
#include "Application/Components/Combat/ExplodeOnHitComponent.h"
#include "Application/Components/Camera/CameraFocusTargetComponent.h"
#include "Application/Components/Camera/CameraDeadZoneComponent.h"
#include "Application/Components/Animation/AdditivePoseComponent.h"
#include "Application/Components/Combat/AimConfigComponent.h"
#include "Application/Components/Combat/TargetEntityComponent.h"
#include "Application/Components/Combat/LockOnTargetComponent.h"
#include "Application/Components/Enemy/PatrolComponent.h"
#include "Application/Components/Enemy/CloseCombatComponent.h"
#include "Application/Components/Weapon/Projectile/HomingComponent.h"
#include "Application/Components/Weapon/Projectile/AlignToVelocityComponent.h"
#include "Application/Components/Weapon/Projectile/ProjectileComponent.h"
#include "Application/Components/Weapon/Shoulder/MissileLockComponent.h"
#include "Application/Components/Boss/BossParamsComponent.h"
#include "Application/Components/Boid/BoidSteeringParamsComponent.h"
#include "Application/Components/Boid/BoidLeaderComponent.h"
#include "Application/Components/Boid/PlatoonLeaderComponent.h"
#include "Application/Components/Boid/PlatoonSpinComponent.h"
#include "Application/Components/Boid/BoidSpownerComponent.h"
#include "Application/Components/Boid/SwarmBossBoidTag.h"
#include "Application/Components/Boid/SerchGroundComponent.h"
#include "Application/Components/Boid/WarmGroundEffectComponent.h"
#include "Application/Components/Effect/DebrisEmitterComponent.h"
#include "Application/Components/Effect/BallisticComponent.h"
#include "Application/Components/Boid/BoidContactDamageComponent.h"
#include "Application/Components/Boid/SwarmMissileComponent.h"
#include "Application/Components/Combat/DefenseRatioComponent.h"
#include "Application/Components/Boid/SwarmBurstComponent.h"
#include "../../Components/Effect/GroundEffectTag.h"
#include "Application/Components/Effect/GroundImpulseEmitterComponent.h"
#include "Application/Components/Effect/GroundDustEmitterComponent.h"

// システム関連
#include "Application/Systems/Render/ModelFixupSystem.h"
#include "Application/Systems/Core/GUIDFixupSystem.h"
#include "Application/Systems/Render/ModelReadyGateSystem.h"
#include "Application/Systems/Attachment/AttachmentReadyGateSystem.h"
#include "Application/Systems/Transform/HierarchyLinkSystem.h"
#include "Application/Systems/Camera/CameraStartSystem.h"
#include "Application/Systems/Animation/AnimationModelStartSystem.h"
#include "Application/Systems/Physics/RegisterPhysicsBodySystem.h"
#include "Application/Systems/Attachment/AttachmentNodeLinkSystem.h"
#include "Application/Systems/Input/InputMoveSystem.h"
#include "Application/Systems/Movement/RotationSystem.h"
#include "Application/Systems/Movement/LockOnRotationSystem.h"
#include "Application/Systems/Movement/GravitySystem.h"
#include "Application/Systems/Movement/CharacterMovementSystem.h"
#include "Application/Systems/Physics/RayCollisionSystem.h"
#include "Application/Systems/Physics/PositionIntegrationSystem.h"
#include "Application/Systems/Physics/MovementIntegrationSystem.h"
#include "Application/Systems/Camera/TPSSystem.h"
#include "Application/Systems/Camera/MainCameraSystem.h"
#include "Application/Systems/Camera/CameraProjUpdateSystem.h"
#include "Application/Systems/Camera/RadialBlurSpeedSystem.h"
#include "Application/Systems/Camera/RadialBlurShakeSystem.h"
#include "Application/Systems/Camera/SpeedLineSystem.h"
#include "Application/Systems/Camera/AimTargetSystem.h"
#include "Application/Systems/Transform/CalcMatrixSystem.h"
#include "Application/Systems/Transform/JointSwaySystem.h"
#include "Application/Systems/Combat/LockOnTargetSystem.h"
#include "Application/Systems/Weapon/MissileSalvoSystem.h"
#include "Application/Systems/Boss/BossMissileSalvoSystem.h"
#include "Application/Systems/Animation/AnimationSystem.h"
#include "Application/Systems/Animation/SkinningSystem.h"
#include "Application/Systems/Animation/CalcNodeSystem.h"
#include "Application/Systems/Transform/FollowAnimationNodeSystem.h"
#include "Application/Systems/Camera/CamSetShaderSystem.h"
#include "Application/Systems/Camera/CameraPipelineSubmitSystem.h"
#include "Application/Systems/Camera/CameraPipelineFixupSystem.h"
#include "Application/Systems/Render/PointLightSystem.h"
#include "Application/Systems/Render/ApplyEmissiveOverrideSystem.h"
#include "Application/Systems/Render/StaticObjectDrawSystem.h"
#include "Application/Systems/Render/DynamicObjectDrawSystem.h"
#include "Application/Systems/Render/AnimationOptionalDraw.h"
#include "Application/Systems/Render/RegisterRayWorldSystem.h"
#include "Application/Systems/Animation/AnimationMatrixFreeSystem.h"
#include "Application/Systems/Animation/AnimatorFreeSystem.h"
#include "Application/Systems/Render/RegisterPrevWorldMatSystem.h"
#include "Application/Systems/Animation/StateMachineFixupSystem.h"
#include "Application/Systems/Animation/StateMachineCommitSystem.h"
#include "Application/Systems/Animation/PlayerIntentSystem.h"
#include "Application/Systems/Movement/RobotBoostSystem.h"
#include "Application/Systems/Movement/ChargeDashSystem.h"
#include "Application/Systems/Effect/EffectFixupSystem.h"
#include "Application/Systems/Effect/EffectUpdateSystem.h"
#include "Application/Systems/Effect/BoosterEffectSystem.h"
#include "Application/Systems/Effect/EffectDrawSystem.h"
#include "Application/Systems/Transform/UpdateHierarchyDepthSystem.h"
#include "Application/Systems/Transform/CommitHierarchyWorldMatrixSystem.h"
#include "Application/Systems/Render/SkinningRegisterSystem.h"
#include "Application/Systems/Render/RegisterAnimatedRayWorldSystem.h"
#include "Application/Systems/Physics/CapsuleCollisionSystem.h"
#include "Application/Systems/Physics/SphereCollisionSystem.h"
#include "Application/Systems/Physics/BoundaryClampSystem.h"
#include "Application/Systems/Input/InputActionSystem.h"
#include "Application/Systems/Weapon/WeaponLimitSystem.h"
#include "Application/Systems/Weapon/WeaponFireModeSystem.h"
#include "Application/Systems/Weapon/WeaponMuzzleSystem.h"
#include "Application/Systems/Weapon/WeaponProjectileSpawnSystem.h"
#include "Application/Systems/Weapon/MuzzleFlashSystem.h"
#include "Application/Systems/Weapon/MissileFireSystem.h"
#include "Application/Systems/Physics/CollisionEventClearSystem.h"
#include "Application/Systems/Physics/HitDetectSystem.h"
#include "Application/Systems/Combat/ExplodeOnHitSystem.h"
#include "Application/Systems/Core/LifeTimeSystem.h"
#include "Application/Systems/Attachment/AttachmentDispatchSystem.h"
#include "Application/Systems/Weapon/SelfWeaponTriggerSystem.h"
#include "Application/Systems/Effect/ThrusterEffectSystem.h"
#include "Application/Systems/Attachment/AttachmentSlotLinkSystem.h"
#include "Application/Systems/Physics/SyncPhysicsBodySystem.h"
#include "Application/Systems/Animation/AdditivePoseLinkSystem.h"
#include "Application/Systems/Animation/AdditivePoseSystem.h"
#include "Application/Systems/Animation/LowerBodyTurnSystem.h"
#include "Application/Systems/Animation/StancePoseSystem.h"
#include "Application/Systems/Animation/AdditivePoseFreeSystem.h"
#include "Application/Systems/Enemy/SearchPlayerSystem.h"
#include "Application/Systems/Movement/FaceTargetSystem.h"
#include "Application/Systems/Enemy/EnemyMoveIntentSystem.h"
#include "Application/Systems/Movement/LookAroundSystem.h"
#include "Application/Systems/Enemy/EnemyMovementSystem.h"
#include "Application/Systems/Audio/BoostSoundSystem.h"
#include "Application/Systems/Audio/ChargeDashSoundSystem.h"
#include "Application/Systems/Audio/SoundFreeSystem.h"
#include "Application/Systems/Physics/PhysicsBodyFreeSystem.h"
#include "Application/Systems/Weapon/MuzzleStartSystem.h"
#include "Application/Systems/Combat/HitEventClearSystem.h"
#include "Application/Systems/Combat/DeathEventClearSystem.h"
#include "Application/Systems/Enemy/EnemyShootIntentSystem.h"
#include "Application/Systems/Enemy/CloseCombatIntentSystem.h"
#include "Application/Systems/Boss/BossCombatIntentSystem.h"
#include "Application/Systems/Weapon/HomingSystem.h"
#include "Application/Systems/Weapon/AlignToVelocitySystem.h"
#include "Application/Systems/Audio/AudioListenerSystem.h"
#include "Application/Components/Audio/AudioListenerComponent.h"
#include "Application/Components/Audio/ChargeDashSoundComponent.h"
#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Systems/Combat/HealthFixupSystem.h"
#include "Application/Systems/Combat/HealthSystem.h"
#include "Application/Systems/Combat/DebugKillSystem.h"
#include "Application/Systems/Combat/DeathStateSystem.h"
#include "Application/Components/Effect/EffectComponent.h"
#include "Application/Components/Effect/EffectAssetComponent.h"
#include "Application/Components/Core/LifeTimeComponent.h"
#include "../../InstanceResource/DeathEventResource.h"
#include "../../InstanceResource/WaveAnnounceResource.h"
#include "Application/Components/Effect/EffectEventsComponent.h"
#include "Application/Systems/Effect/EffectEventSystem.h"
#include "Application/Systems/Combat/ScoreSystem.h"
#include "Application/Systems/Boid/BoidSnapshotSystem.h"
#include "Application/Systems/Boid/BoidSteeringSystem.h"
#include "Application/Systems/Boid/PlatoonAxisSystem.h"
#include "Application/Systems/Boid/FollowLeaderSystem.h"
#include "Application/Systems/Boid/PlatoonFollowSystem.h"
#include "Application/Systems/Boid/SwarmLookSystem.h"
#include "Application/Systems/Boid/SwarmLeaderMoveSystem.h"
#include "Application/Systems/Boid/BoidWaveSystem.h"
#include "Application/Systems/Boid/SerchGroundSystem.h"
#include "Application/Systems/Boid/BoidGroundEffectSystem.h"
#include "Application/Systems/Effect/DebrisEmitterSystem.h"
#include "Application/Systems/Effect/BallisticSystem.h"
#include "Application/Systems/Boid/BoidContactDamageSystem.h"
#include "Application/Systems/Boid/SwarmMissileSystem.h"
#include "Application/Systems/Boid/SwarmBurstSystem.h"
#include "../../Systems/Render/GroundEffectSetSystem.h"
#include "Application/Systems/Effect/GroundImpulseEmitSystem.h"
#include "Application/Systems/Effect/GroundDustEmitterSystem.h"
#include "Application/Systems/UI/HUDGatherSystem.h"

// リソース関係
#include "Application/InstanceResource/HierarchyResource.h"
#include "Application/InstanceResource/SingletonEntityResource.h"
#include "Application/InstanceResource/ResourceWaitResource.h"
#include "../../InstanceResource/AdditiveBoneEntry.h"
#include "Application/InstanceResource/HitEventResource.h"
#include "Application/InstanceResource/WormWaveResource.h"
#include "Application/InstanceResource/BoidSnapshotResource.h"
#include "Application/InstanceResource/PlatoonAxisResource.h"
#include "Application/InstanceResource/WormGroundEffectResource.h"
#include "Application/InstanceResource/SwarmContactDamageResource.h"
#include "Application/InstanceResource/SwarmMissileResource.h"
#include "Application/InstanceResource/PlayerHUDResource.h"
#include "Application/InstanceResource/UICursorResource.h"
#include "Application/InstanceResource/BoundaryResource.h"
#include "Application/InstanceResource/RadialBlurShakeResource.h"
#include "Engine/Graphics/Raytracing/DynamicRaytracingData.h"

namespace App::ECS
{
	void RegisterGameTypes(APPWorld& a_world)
	{
		// ECSにコンポーネントを登録
		a_world.RegisterComponent<Component::PostDeserializeTag>("PostDeserializeTag");
		a_world.RegisterComponent<Component::AwakeTag>("AwakeTag");
		a_world.RegisterComponent<Component::StartTag>("StartTag");
		a_world.RegisterComponent<Component::ActiveTag>("ActiveTag");
		a_world.RegisterComponent<Component::ReleaseTag>("ReleaseTag");
		a_world.RegisterComponent<Component::EnemyTag>("EnemyTag");

		a_world.RegisterComponent<Component::RayTag>("RayTag");

		a_world.RegisterComponent<Component::CameraTag>("CameraTag");
		a_world.RegisterComponent<Component::CameraControllTag>("CameraControllTag");
		a_world.RegisterComponent<Component::PlayerControllTag>("PlayerControllTag");

		a_world.RegisterComponent<Component::CameraParamComponent>("CameraParamComponent");
		a_world.RegisterComponent<Component::ProjMatComponent>("ProjMatComponent");
		a_world.RegisterComponent<Component::FocusParamComponent>("FocusParamComponent");
		a_world.RegisterComponent<Component::RadialBlurComponent>("RadialBlurComponent");
		// 速いときにカメラのまわりを流れる風の筋。量を書くのは SpeedLineSystem
		a_world.RegisterComponent<Component::SpeedLineComponent>("SpeedLineComponent");
		a_world.RegisterComponent<Component::FishEyeComponent>("FishEyeComponent");
		a_world.RegisterComponent<Component::FollowTargetComponent>("FollowTargetComponent");
		a_world.RegisterComponent<Component::DesiredVelocityComponent>("DesiredVelocityComponent");
		a_world.RegisterComponent<Component::GravityComponent>("GravityComponent");
		// 移動の設定(保存)と、加減速を掛けた実速度(設定の必須コンポーネント)
		a_world.RegisterComponent<Component::MovementParamsComponent>("MovementParamsComponent");
		a_world.RegisterComponent<Component::ActualVelocityComponent>("ActualVelocityComponent");
		a_world.RegisterComponent<Component::LookAngleComponent>("LookAngleComponent");
		a_world.RegisterComponent<Component::ColliderComponent>("ColliderComponent");
		a_world.RegisterComponent<Component::RayColliderComponent>("RayColliderComponent");
		a_world.RegisterComponent<Component::LocalTransformComponent>("LocalTransformComponent");
		a_world.RegisterComponent<Component::WorldMatrixComponent>("WorldMatrixComponent");
		a_world.RegisterComponent<Component::ModelComponent>("ModelComponent");
		a_world.RegisterComponent<Component::AnimatorComponent>("AnimatorComponent");
		// 基本レイヤーの上に重ねるアニメーター(アニメーションレイヤリング)
		a_world.RegisterComponent<Component::UpperAnimatorComponent>("UpperAnimatorComponent");
		// 腰から下だけを進行方向へ向ける(戦車のような脚)
		a_world.RegisterComponent<Component::LowerBodyTurnComponent>("LowerBodyTurnComponent");
		// クリップで組んだポーズの上から決まった構えを足す(ガレージの仁王立ちなど)
		a_world.RegisterComponent<Component::StancePoseComponent>("StancePoseComponent");
		a_world.RegisterComponent<Component::SkeletonPoseComponent>("SkeletonPoseComponent");
		a_world.RegisterComponent<Component::NodePoseComponent>("NodePoseComponent");
		a_world.RegisterComponent<Component::UIComponent>("UIComponent");
		a_world.RegisterComponent<Component::NameComponent>("NameComponent");
		a_world.RegisterComponent<Engine::ECS::GUIDComponent>("GUIDComponent");
		// 実行中にプレハブから出したものの印。シーンの保存(BaseScene::Archive)はこれを飛ばす
		a_world.RegisterComponent<Engine::ECS::TransientTag>("TransientTag");
		a_world.RegisterComponent<Component::HierarchyComponent>("HierarchyComponent");
		a_world.RegisterComponent<Component::JointSwayComponent>("JointSwayComponent");
		// 出現させた側(SceneSequence)の印。ウェーブの全滅判定に使う
		a_world.RegisterComponent<Component::SpawnerComponent>("SpawnerComponent");
		a_world.RegisterComponent<Component::FollowAnimationNodeComponent>("FollowAnimationNodeComponent");
		a_world.RegisterComponent<Component::MoveIntentComponent>("MoveIntentComponent");
		a_world.RegisterComponent<Component::PreviousWorldMatrixComponent>("PreviousWorldMatrixComponent");
		// ブーストの設定(保存)と、入力・状態(設定の必須コンポーネント)
		a_world.RegisterComponent<Component::BoostParamsComponent>("BoostParamsComponent");
		a_world.RegisterComponent<Component::BoostIntentComponent>("BoostIntentComponent");
		a_world.RegisterComponent<Component::BoostStateComponent>("BoostStateComponent");
		a_world.RegisterComponent<Component::AttachmentSlotsComponent>("AttachmentSlotsComponent");
		a_world.RegisterComponent<Component::TPSCameraStateComponent>("TPSCameraStateComponent");
		a_world.RegisterComponent<Component::TPSFollowComponent>("TPSFollowComponent");
		a_world.RegisterComponent<Component::CapsuleColliderComponent>("CapsuleColliderComponent");
		a_world.RegisterComponent<Component::SphereColliderComponent>("SphereColliderComponent");
		a_world.RegisterComponent<Component::ActionIntentComponent>("ActionIntentComponent");
		//------------------------------------------------------------------
		// 武器(Components/Weapon)
		//------------------------------------------------------------------
		// 武器が外から受け取る引き金。持ち主の命令と武器の挙動を分ける受け口
		a_world.RegisterComponent<Component::WeaponTriggerComponent>("WeaponTriggerComponent");
		// このフレームの発射の結果(撃ち方の必須コンポーネント)
		a_world.RegisterComponent<Component::WeaponFireComponent>("WeaponFireComponent");
		// 部品(WeaponConfig) : 銃口・撃ち方・熱・弾倉。手持ちにも肩にも付けられる
		a_world.RegisterComponent<Component::MuzzleComponent>("MuzzleComponent");
		a_world.RegisterComponent<Component::AutoFireComponent>("AutoFireComponent");
		a_world.RegisterComponent<Component::BurstFireComponent>("BurstFireComponent");
		a_world.RegisterComponent<Component::SemiAutoFireComponent>("SemiAutoFireComponent");
		a_world.RegisterComponent<Component::WeaponOverHeatComponent>("WeaponOverHeatComponent");
		a_world.RegisterComponent<Component::WeaponReloadComponent>("WeaponReloadComponent");
		// 手に持つ武器(Hand) : 何を・どれくらいの速さと威力で撃つか
		a_world.RegisterComponent<Component::WeaponStateComponent>("WeaponStateComponent");
		// 肩の武器(Shoulder) : ミサイルのカタログと一斉射
		a_world.RegisterComponent<Component::MissileStateComponent>("MissileStateComponent");
		a_world.RegisterComponent<Component::MissileFireComponent>("MissileFireComponent");
		// 機体の武器の取り付け口。出撃構成の武器を PlayerSpawner がここへ付ける
		a_world.RegisterComponent<Component::WeaponMountComponent>("WeaponMountComponent");
		a_world.RegisterComponent<Engine::ECS::CollisionEvent>("CollisionEvent");
		a_world.RegisterComponent<Component::ExplodeOnHitComponent>("ExplodeOnHitComponent");
		a_world.RegisterComponent<Component::CameraFocusTargetComponent>("CameraFocusTargetComponent");
		// TPSカメラの追従範囲。枠から出たぶんだけカメラを平行移動させる
		a_world.RegisterComponent<Component::CameraDeadZoneComponent>("CameraDeadZoneComponent");
		a_world.RegisterComponent<Component::AdditivePoseComponent>("AdditivePoseComponent");
		// 狙点のレイの設定(保存)と、その結果(設定の必須コンポーネント)
		a_world.RegisterComponent<Component::AimConfigComponent>("AimConfigComponent");
		a_world.RegisterComponent<Component::AimResultComponent>("AimResultComponent");
		// 近距離型の敵の「足を止めて撃つ / 撃たずに動き直す」のリズム
		a_world.RegisterComponent<Component::CloseCombatComponent>("CloseCombatComponent");
		a_world.RegisterComponent<Component::PatrolComponent>("PatrolComponent");
		a_world.RegisterComponent<Component::TargetEntityComponent>("TargetEntityComponent");
		// プレイヤーのレティクル内の敵とロック対象。HUDと旋回が読む
		a_world.RegisterComponent<Component::LockOnTargetComponent>("LockOnTargetComponent");
		// ミサイルの溜め(持ち主側)。コンバットレティクル内の敵を溜めて、肩の武器へ一斉射を要求する
		a_world.RegisterComponent<Component::MissileLockComponent>("MissileLockComponent");
		// 人型ボスの戦闘設定と機動状態。シーケンスからの戦闘開始命令もここに立つ
		// ボスの設定(保存)と、思考の状態・命令(設定の必須コンポーネント)
		a_world.RegisterComponent<Component::BossParamsComponent>("BossParamsComponent");
		a_world.RegisterComponent<Component::BossBrainStateComponent>("BossBrainStateComponent");
		a_world.RegisterComponent<Component::BossCommandComponent>("BossCommandComponent");
		// 始動/継続/終了の音をまとめた AudioBehavior アセットを鳴らす
		a_world.RegisterComponent<Component::AudioListenerComponent>("AudioListenerComponent");
		a_world.RegisterComponent<Component::ChargeDashSoundComponent>("ChargeDashSoundComponent");
		a_world.RegisterComponent<Component::HealthComponent>("HealthComponent");
		a_world.RegisterComponent<Component::EffectComponent>("EffectComponent");
		// パーティクル+メッシュをまとめた EffectAsset を再生する
		a_world.RegisterComponent<Component::EffectAssetComponent>("EffectAssetComponent");
		// エフェクトの実行中の値(EffectAssetComponent の必須コンポーネント)
		a_world.RegisterComponent<Component::EffectRuntimeComponent>("EffectRuntimeComponent");
		a_world.RegisterComponent<Component::EffectPlayRequestComponent>("EffectPlayRequestComponent");
		a_world.RegisterComponent<Component::EffectOverrideComponent>("EffectOverrideComponent");
		a_world.RegisterComponent<Component::LifeTimeComponent>("LifeTimeComponent");
		a_world.RegisterComponent<Component::HomingComponent>("HomingComponent");
		a_world.RegisterComponent<Component::AlignToVelocityComponent>("AlignToVelocityComponent");
		a_world.RegisterComponent<Component::ProjectileComponent>("ProjectileComponent");
		// ※ 追加はここから下(末尾)へ。途中に挿すとコンポーネントのタイプIDがずれて
		//    保存済みのプレハブ・シーンが全部壊れる
		a_world.RegisterComponent<Component::BoosterEffectComponent>("BoosterEffectComponent");
		// ジャンプ長押しで溜めて直進するチャージダッシュ
		a_world.RegisterComponent<Component::ChargeDashComponent>("ChargeDashComponent");
		// 倒す相手であることの印と、倒したときに入るスコア
		a_world.RegisterComponent<Component::ScoreTargetComponent>("ScoreTargetComponent");
		// エンティティの位置を光源にする点光源。実体は LightManager のプールにある
		a_world.RegisterComponent<Component::PointLightComponent>("PointLightComponent");
		// 群れの操舵の設定(保存)と、実行中の所属・目標地点(操舵の設定の必須コンポーネント)
		a_world.RegisterComponent<Component::BoidSteeringParamsComponent>("BoidSteeringParamsComponent");
		a_world.RegisterComponent<Component::BoidMembershipComponent>("BoidMembershipComponent");
		a_world.RegisterComponent<Component::BoidTargetComponent>("BoidTargetComponent");
		// 群れのボスの先頭(SwarmBossController が指示を出す相手)の印
		a_world.RegisterComponent<Component::BoidLeaderComponent>("BoidLeaderComponent");
		// リーダーに連なる小隊長。一つ前の相手は SwarmBossController が生成時に書き込む
		a_world.RegisterComponent<Component::PlatoonLeaderComponent>("PlatoonLeaderComponent");
		a_world.RegisterComponent<Component::PlatoonSpinComponent>("PlatoonSpinComponent");
		// 自分の周りに出すボイドの設定(数は出す側が決める)
		a_world.RegisterComponent<Component::BoidSpownerComponent>("BoidSpownerComponent");
		// 群れのボスの体を作っているボイドの印。数がそのままボスの体力
		a_world.RegisterComponent<Component::SwarmBossBoidTag>("SwarmBossBoidTag");
		// 上下にレイを打って地面との関係を持つ(今はワームボスのリーダーが使う)
		a_world.RegisterComponent<Component::SerchGroundComponent>("SerchGroundComponent");
		// ワームの体(ボイド)が砂埃を炊く番を待つ時間。付けるのは SwarmBossController
		a_world.RegisterComponent<Component::WarmGroundEffectComponent>("WarmGroundEffectComponent");
		// エフェクトプレハブ : 破片を撒く / 撒かれた破片を放物線で飛ばして着地させる
		a_world.RegisterComponent<Component::DebrisEmitterComponent>("DebrisEmitterComponent");
		a_world.RegisterComponent<Component::BallisticComponent>("BallisticComponent");
		// ワームの体(ボイド)の体当たり。持つのは次に判定するまでの待ち時間だけ
		a_world.RegisterComponent<Component::BoidContactDamageComponent>("BoidContactDamageComponent");
		// 足元の接地判定。書くのは RayCollisionSystem(StateMachineComponent から分けた)
		a_world.RegisterComponent<Component::GroundStateComponent>("GroundStateComponent");
		// ボイドの発光ウェーブの計算途中の値(BoidComponent から分けた)
		a_world.RegisterComponent<Component::BoidWaveStateComponent>("BoidWaveStateComponent");
		// 実行中の発光の差し替え。ModelComponent へ写すのは ApplyEmissiveOverrideSystem
		a_world.RegisterComponent<Component::EmissiveOverrideComponent>("EmissiveOverrideComponent");
		// アニメーションするモデルのレイトレ用インスタンス(AnimatorComponent から分けた)
		a_world.RegisterComponent<Component::DynamicRaytracingComponent>("DynamicRaytracingComponent");
		// ワームの体から切り離された自爆ミサイル。付けるのは SwarmMissileSystem
		a_world.RegisterComponent<Component::SwarmMissileComponent>("SwarmMissileComponent");
		// 受けたダメージに掛ける比率(0 で無敵)。掛けるのは HealthSystem
		a_world.RegisterComponent<Component::DefenseRatioComponent>("DefenseRatioComponent");
		// ワームボスの死亡で爆散して飛び散っているボイド。付けるのは SwarmBossController
		a_world.RegisterComponent<Component::SwarmBurstComponent>("SwarmBurstComponent");
		// グラウンドエフェクト用タグ
		a_world.RegisterComponent<Component::GroundEffectTag>("GroundEffectTag");
		// グラウンドフィールドへ衝撃を出す(テスト用)。出すのは GroundImpulseEmitSystem
		a_world.RegisterComponent<Component::GroundImpulseEmitterComponent>("GroundImpulseEmitterComponent");
		// 地面の近くを速く動く機体が地表へ炊く砂埃。炊くのは GroundDustEmitterSystem
		a_world.RegisterComponent<Component::GroundDustEmitterComponent>("GroundDustEmitterComponent");
		// 出来事(生まれた・死んだ・攻撃を受けた)→ エフェクトの対応表。出すのは EffectEventSystem
		a_world.RegisterComponent<Component::EffectEventsComponent>("EffectEventsComponent");

		// システム登録
		a_world.RegisterSystem<System::ModelFixupSystem>();
		a_world.RegisterSystem<System::GUIDFixupSystem>();
		a_world.RegisterSystem<System::StateMachineFixupSystem>();
		a_world.RegisterSystem<System::EffectFixupSystem>();
		// 現在体力を最大体力で満たす
		a_world.RegisterSystem<System::HealthFixupSystem>();
		// リソースの到着待ちゲート。
		// AwakeTag -> StartTag の遷移より前に走らせる必要があるため、
		// Awake フェーズの先頭付近に置くこと
		a_world.RegisterSystem<System::ModelReadyGateSystem>();
		a_world.RegisterSystem<System::AttachmentSlotLinkSystem>();
		a_world.RegisterSystem<System::HierarchyLinkSystem>();
		// 親モデルの到着待ちゲート。
		// 親IDの解決(HierarchyLinkSystem)より後に走る必要があるため、
		// 必ずこの位置より下に置くこと
		a_world.RegisterSystem<System::AttachmentReadyGateSystem>();
		a_world.RegisterSystem<System::PlayerIntentSystem>();
		a_world.RegisterSystem<System::AttachmentDispatchSystem>();
		// 本体が武器を兼ねているキャラ(銃を子に持たない敵など)の引き金を渡す。
		// 武器が子の場合は上の AttachmentDispatchSystem が受け持つ
		a_world.RegisterSystem<System::SelfWeaponTriggerSystem>();
		a_world.RegisterSystem<System::ThrusterEffectSystem>();
		a_world.RegisterSystem<System::BoostSoundSystem>();
		// チャージダッシュの溜めの音(溜めを進める ChargeDashSystem より後ろの PostUpdate で回る)
		a_world.RegisterSystem<System::ChargeDashSoundSystem>();
		a_world.RegisterSystem<System::SearchPlayerSystem>();
		// 索敵結果(isFind)を敵の発射入力へ。銃が子なら AttachmentDispatchSystem が配信する
		a_world.RegisterSystem<System::EnemyShootIntentSystem>();
		// ボスの行動決定。プレイヤーの入力と同じ形(視点角/移動/ブースト/発射/狙点)を作る
		a_world.RegisterSystem<System::BossCombatIntentSystem>();
		// 誘導弾の進行方向決め。速度を書くだけなので Physics の積分より前に置く
		a_world.RegisterSystem<System::HomingSystem>();
		// 細長い弾の見た目を進む向きへ合わせる(誘導で曲がった後の速度を読む)
		a_world.RegisterSystem<System::AlignToVelocitySystem>();
		a_world.RegisterSystem<System::EnemyMoveIntentSystem>();
		// 近距離型の敵の撃つ/動くのリズム。
		// EnemyMoveIntentSystem が書いた移動入力を攻撃圏の中だけ上書きするので、
		// 必ずあちらの後ろに置くこと(PatrolComponent を読んで辺は張ってある)
		a_world.RegisterSystem<System::CloseCombatIntentSystem>();
		a_world.RegisterSystem<System::StateMachineCommitSystem>();
		// コライダーを物理空間(Jolt)へ登録する(静的も動くものも)
		a_world.RegisterSystem<System::RegisterPhysicsBodySystem>();
		a_world.RegisterSystem<System::CameraStartSystem>();
		a_world.RegisterSystem<System::AnimationModelStartSystem>();
		a_world.RegisterSystem<System::AttachmentNodeLinkSystem>();
		a_world.RegisterSystem<System::AdditivePoseLinkSystem>();
		a_world.RegisterSystem<System::CamSetShaderSystem>();
		// 描画構成を持つカメラを全部 GraphicsEngine へ送る(新レンダーグラフ)。
		// メインカメラ1台ぶんを送る CamSetShaderSystem とは別で、こちらは並走する経路
		a_world.RegisterSystem<System::CameraPipelineSubmitSystem>();
		a_world.RegisterSystem<System::CameraPipelineFixupSystem>();
		// 点光源の位置と設定値を LightManager へ送る。
		// GPUバッファへ詰め直されるのは描画フェーズの後なので、この帯で間に合う
		a_world.RegisterSystem<System::PointLightSystem>();
		// 演出側が置いた発光の差し替えを ModelComponent へ写す(描画の直前)
		a_world.RegisterSystem<System::ApplyEmissiveOverrideSystem>();
		a_world.RegisterSystem<System::InputMoveSystem>();
		a_world.RegisterSystem<System::GravitySystem>();
		a_world.RegisterSystem<System::RotationSystem>();
		a_world.RegisterSystem<System::JointSwaySystem>();
		// プレイヤーの旋回は「撃っているか」で進行方向/狙い方向を切り替えるので専用システムが持つ
		a_world.RegisterSystem<System::LockOnRotationSystem>();
		a_world.RegisterSystem<System::FaceTargetSystem>();
		// 見失い探索中の旋回。視認中(FaceTargetSystem)とは条件が排他
		a_world.RegisterSystem<System::LookAroundSystem>();
		a_world.RegisterSystem<System::AnimationSystem>();
		// AnimationSystem がバインドポーズでリセットした後、
		// CalcNodeSystem が local→world を組む前に加算する必要がある
		a_world.RegisterSystem<System::AdditivePoseSystem>();
		a_world.RegisterSystem<System::LowerBodyTurnSystem>();
		a_world.RegisterSystem<System::StancePoseSystem>();
		a_world.RegisterSystem<System::CalcNodeSystem>();
		a_world.RegisterSystem<System::SkinningSystem>();
		a_world.RegisterSystem<System::PositionIntegrationSystem>();
		a_world.RegisterSystem<System::MovementIntegrationSystem>();
		a_world.RegisterSystem<System::CharacterMovementSystem>();
		a_world.RegisterSystem<System::EnemyMovementSystem>();
		a_world.RegisterSystem<System::TPSSystem>();
		// スピードで動く画角(TPSSystem が fovBoost を書く)を射影行列へ反映する。
		// CameraParamComponent を読むので TPSSystem より後に回る
		a_world.RegisterSystem<System::CameraProjUpdateSystem>();
		// スピードに応じたラジアルブラーの強さ。
		// 画角と同じ speed01 を読むので、それを書く TPSSystem より後に回る
		a_world.RegisterSystem<System::RadialBlurSpeedSystem>();
		// ボスの咆哮などで画面のラジアルブラーを震わせる(速度ぶんとは別に持ち、送るときに足す)
		a_world.RegisterSystem<System::RadialBlurShakeSystem>();
		// スピードラインの量(カメラのエフェクトの params[0])。
		// 画角・ブラーと同じ speed01 を読むので、それを書く TPSSystem より後に回る
		a_world.RegisterSystem<System::SpeedLineSystem>();
		// 映すカメラを1台選んで SingletonEntityResource へ置く。
		// 使う側(狙点・ロックオン・描画のカメラ設定)より手前の帯(PreUpdate)で回る
		a_world.RegisterSystem<System::MainCameraSystem>();
		// カメラ姿勢が確定した後に狙点レイを撃つ(TPSSystem より後に登録すること)
		a_world.RegisterSystem<System::AimTargetSystem>();
		a_world.RegisterSystem<System::CalcMatrixSystem>();
		// レティクル内の敵集めとロック。ワールド行列を読むので
		// それを書く CalcMatrix / CommitHierarchyWorldMatrix より後ろに回る
		a_world.RegisterSystem<System::LockOnTargetSystem>();
		a_world.RegisterSystem<System::MissileSalvoSystem>();
		// ボスのミサイル。溜め方だけが違い、要求を出す先(肩の武器)は同じ
		a_world.RegisterSystem<System::BossMissileSalvoSystem>();
		// 肩の武器が受けた一斉射の要求を撃ち出す(上の2つの後ろ)
		a_world.RegisterSystem<System::MissileFireSystem>();
		a_world.RegisterSystem<System::RobotBoostSystem>();
		// チャージダッシュ。速度を書く仲間(重力・ブースト)より後に登録して、
		// ダッシュ中はこちらの値が最後に残るようにする
		a_world.RegisterSystem<System::ChargeDashSystem>();
		a_world.RegisterSystem<System::FollowAnimationNodeSystem>();
		a_world.RegisterSystem<System::RayCollisionSystem>();
		a_world.RegisterSystem<System::StaticObjectDrawSystem>();
		a_world.RegisterSystem<System::DynamicObjectDrawSystem>();
		a_world.RegisterSystem<System::AnimationOptionalDrawSystem>();
		a_world.RegisterSystem<System::RegisterRayWorldSystem>();
		// エフェクト : 時間を進めるのは Update、出すのは Draw
		a_world.RegisterSystem<System::EffectUpdateSystem>();
		// ブースターの噴射の置き方と、吹かした瞬間の膨らみをエフェクトへ渡す
		a_world.RegisterSystem<System::BoosterEffectSystem>();
		a_world.RegisterSystem<System::EffectDrawSystem>();
		a_world.RegisterSystem<System::AnimationMatrixFreeSystem>();
		a_world.RegisterSystem<System::AnimatorFreeSystem>();
		a_world.RegisterSystem<System::AdditivePoseFreeSystem>();
		a_world.RegisterSystem<System::SoundFreeSystem>();
		a_world.RegisterSystem<System::PhysicsBodyFreeSystem>();
		a_world.RegisterSystem<System::RegisterPrevWorldMatSystem>();
		a_world.RegisterSystem<System::UpdateHierarchyDepthSystem>();
		a_world.RegisterSystem<System::CommitHierarchyWorldMatrixSystem>();
		a_world.RegisterSystem<System::SkinningRegisterSystem>();
		a_world.RegisterSystem<System::RegisterAnimatedRayWorldSystem>();
		a_world.RegisterSystem<System::CapsuleCollisionSystem>();
		a_world.RegisterSystem<System::SphereCollisionSystem>();
		// プレイヤーを行ける範囲(Boundary)の内側へ挟む。押し出しの後ろ(タスク側の After で決めている)
		a_world.RegisterSystem<System::BoundaryClampSystem>();
		a_world.RegisterSystem<System::InputActionSystem>();
		// 手持ちの武器 : 熱と弾倉の進行 → 撃ち方ごとの発射判定(Job) → 銃口の位置と向き → 弾の生成 → 銃口の光
		a_world.RegisterSystem<System::WeaponLimitSystem>();
		a_world.RegisterSystem<System::WeaponFireModeSystem>();
		a_world.RegisterSystem<System::WeaponMuzzleSystem>();
		a_world.RegisterSystem<System::WeaponProjectileSpawnSystem>();
		a_world.RegisterSystem<System::MuzzleFlashSystem>();
		// 動くコライダーのボディを今の姿勢へ合わせる
		a_world.RegisterSystem<System::SyncPhysicsBodySystem>();
		a_world.RegisterSystem<System::CollisionEventClearSystem>();
		a_world.RegisterSystem<System::HitEventClearSystem>();
		// 死亡イベントも読み手が複数(エフェクトとスコア)になったので、
		// ヒットと同じく捨てる係を分けてある
		a_world.RegisterSystem<System::DeathEventClearSystem>();
		a_world.RegisterSystem<System::HitDetectSystem>();
		a_world.RegisterSystem<System::ExplodeOnHitSystem>();
		// 被弾で体力を削り、尽きたら死亡状態にする(体力持ちは ExplodeOnHit の対象外)
		a_world.RegisterSystem<System::HealthSystem>();
		// デバッグキーで倒すと予約されたものを死亡させる(Development まで。入口が無ければ何もしない)
		a_world.RegisterSystem<System::DebugKillSystem>();
		// 死亡状態のあいだ入力/AIを止め、指定秒たったら解放予約する。
		// 体力を書く HealthSystem との前後は、タスク側の After("HealthSystem") で決めている
		a_world.RegisterSystem<System::DeathStateSystem>();
		// 寿命持ち(弾・エフェクトなど)の共通処理。尽きたら自分で消える
		a_world.RegisterSystem<System::LifeTimeSystem>();
		// 出来事の対応表(EffectEventsComponent)からエフェクトを出す(Fixup も含む)
		a_world.RegisterSystem<System::EffectEventSystem>();
		// 倒した相手ぶんのスコアを足す(死亡を積む側より後ろで回る)
		a_world.RegisterSystem<System::ScoreSystem>();
		// 3Dサウンドの聞き手。鳴らす側より先に登録して、先にリスナーを更新させる
		a_world.RegisterSystem<System::AudioListenerSystem>();
		a_world.RegisterSystem<System::MuzzleStartSystem>();
		// 群れの操舵 : 全員の写しを作ってから、チャンクを分けて操舵する
		a_world.RegisterSystem<System::BoidSnapshotSystem>();
		a_world.RegisterSystem<System::BoidSteeringSystem>();
		a_world.RegisterSystem<System::FollowLeaderSystem>();
		// 群れのボスの向き(リーダー/小隊長は進行方向、ボイドは小隊長の向きへ)。
		// 前方を使う PlatoonFollowSystem より前に置く
		a_world.RegisterSystem<System::SwarmLookSystem>();
		// リーダーの移動入力(SwarmBossController が作る)を目標速度へ
		a_world.RegisterSystem<System::SwarmLeaderMoveSystem>();
		// 小隊長を一つ前の相手の後ろへ追従させる(目標速度だけ書く)
		a_world.RegisterSystem<System::PlatoonFollowSystem>();
		// 体を走る発光のウェーブをボイドへ塗る(ウェーブを出すのは SwarmBossController)。
		// 小隊長の軸をまとめてから、ボイドの発光を塗る
		a_world.RegisterSystem<System::PlatoonAxisSystem>();
		a_world.RegisterSystem<System::BoidWaveSystem>();
		// 上下にレイを打って地表の高さと地中に居るかを書く(ワームボスのアッパー攻撃が読む)
		a_world.RegisterSystem<System::SerchGroundSystem>();
		// ワームの体(ボイド)から上下にレイを打ち、地表へ砂埃を炊く(設定は SwarmBossController)
		a_world.RegisterSystem<System::BoidGroundEffectSystem>();
		// 地面の近くを速く動く機体から真下へレイを打ち、地表へ砂埃を炊く(ボイドの砂埃と同じ作り)
		a_world.RegisterSystem<System::GroundDustEmitterSystem>();
		// エフェクトプレハブの破片を撒く(破片のハンドルは PostDeserialize で取る)
		a_world.RegisterSystem<System::DebrisEmitterSystem>();
		// 撒かれた破片を放物線で飛ばし、地面で跳ねて止める
		a_world.RegisterSystem<System::BallisticSystem>();
		// ワームの体(ボイド)がプレイヤーに触れたらダメージを積む(減らすのは HealthSystem)
		a_world.RegisterSystem<System::BoidContactDamageSystem>();
		// ワームの体(ボイド)を切り離して自爆ミサイルにする(切り離す小隊長を決めるのはワームボスの巻き付き攻撃)
		a_world.RegisterSystem<System::SwarmMissileSystem>();
		// ワームボスの死亡で爆散したボイドを飛ばし、時間が来たら落とす(爆散させるのは SwarmBossController)
		a_world.RegisterSystem<System::SwarmBurstSystem>();
		// グラウンドエフェクトを炊く際の基準となるメッシュを登録
		a_world.RegisterSystem<System::GroundEffectSetSystem>();
		// グラウンドフィールドへ衝撃を積む(テスト用)
		a_world.RegisterSystem<System::GroundImpulseEmitSystem>();
		// HUD が読む値を集めて PlayerHUDResource へ置く(HUD はワールドを直接見ない)。
		// 読むだけで、書くのは自分のリソースだけなので、書く側の後ろへ自動で回る
		a_world.RegisterSystem<System::HUDGatherSystem>();

		// インスタンスデータの登録
		a_world.AddResource<Engine::Pool::ItemPool<Engine::Resource::StateMachineInstance>>();

		a_world.AddResource<Engine::Pool::RangePool<Engine::Resource::BoneMatrix>>();
		a_world.AddResource<Engine::Pool::RangePool<Engine::Resource::NodePoseMatrix>>();
		a_world.AddResource<Engine::Pool::RangePool<InstanceResource::AdditiveBoneEntry>>();

		a_world.AddResource<Engine::Pool::ItemPool<Engine::Graphics::Raytracing::DynamicRaytracingData>>();
		a_world.AddResource<std::vector<Engine::Graphics::Raytracing::DynamicRaytracingInitRequest>>();
		a_world.AddResource<Engine::Pool::ItemPool<Engine::Graphics::Animation::SkinningMeshData>>();
	

		// シングルトンインスタンスの登録
		a_world.AddResource<InstanceResource::HierarchyResource>();
		a_world.AddResource<InstanceResource::SingletonEntityResource>();
		a_world.AddResource<InstanceResource::ResourceWaitResource>();
		a_world.AddResource<InstanceResource::HitEventResource>();
		a_world.AddResource<InstanceResource::DeathEventResource>();
		a_world.AddResource<InstanceResource::WaveAnnounceResource>();
		a_world.AddResource<InstanceResource::WormWaveResource>();
		// 群れの操舵の写しと、小隊長の軸(前段のシステムが作り、Job が読む)
		a_world.AddResource<InstanceResource::BoidSnapshotResource>();
		a_world.AddResource<InstanceResource::PlatoonAxisResource>();
		// ワームの体が炊く砂埃の設定(SwarmBossController が書き、BoidGroundEffectSystem が読む)
		a_world.AddResource<InstanceResource::WormGroundEffectResource>();
		// ワームの体当たりの設定とプレイヤーの形(SwarmBossController が書き、BoidContactDamageSystem が読む)
		a_world.AddResource<InstanceResource::SwarmContactDamageResource>();
		// 自爆ミサイルの切り離しの要求と飛び方(巻き付き攻撃が書き、SwarmMissileSystem が読む)
		a_world.AddResource<InstanceResource::SwarmMissileResource>();
		// HUD が読む値(HUDGatherSystem が書き、HUD が読む)
		a_world.AddResource<InstanceResource::PlayerHUDResource>();
		// UI のカーソルの取り合い(書くのも読むのも UI。システムは触らない)
		a_world.AddResource<InstanceResource::UICursorResource>();
		// 行ける範囲(Boundary オブジェクトが書き、BoundaryClampSystem が読む)
		a_world.AddResource<InstanceResource::BoundaryResource>();
		// 画面のラジアルブラーの震え(ボスの咆哮が書き、RadialBlurShakeSystem が読む)
		a_world.AddResource<InstanceResource::RadialBlurShakeResource>();

		// 初期化
		a_world.RefResource<Engine::Pool::RangePool<Engine::Resource::BoneMatrix>>().Init(10000);
		a_world.RefResource<Engine::Pool::RangePool<Engine::Resource::NodePoseMatrix>>().Init(10000);
		a_world.RefResource<Engine::Pool::RangePool<InstanceResource::AdditiveBoneEntry>>().Init(10000);

		a_world.RefResource<Engine::Pool::ItemPool<Engine::Graphics::Raytracing::DynamicRaytracingData>>().Reserve(100);
		a_world.RefResource<Engine::Pool::ItemPool<Engine::Graphics::Animation::SkinningMeshData>>().Reserve(100);
		a_world.RefResource<std::vector<Engine::Graphics::Raytracing::DynamicRaytracingInitRequest>>();

		a_world.RefResource<InstanceResource::HierarchyResource>().isDirty = true;

		// 1フレーム分のヒット数はたかが知れているので少なめに確保
		a_world.RefResource<InstanceResource::HitEventResource>().Reserve(256);
		a_world.RefResource<InstanceResource::DeathEventResource>().Reserve(64);

		// 同時に走るウェーブは数本(SwarmBossController の Max Wave)
		a_world.RefResource<InstanceResource::WormWaveResource>().Reserve(16);
		// 切り離しの要求は1フレームに数件
		a_world.RefResource<InstanceResource::SwarmMissileResource>().Reserve(16);
	}
}
