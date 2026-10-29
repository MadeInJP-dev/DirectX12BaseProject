#include "ModelFadeSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"

#include "Application/Components/Render/ModelComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Camera/TPSCameraStateComponent.h"
#include "Application/Components/Camera/CameraCollisionComponent.h"

#include "Application/InstanceResource/SingletonEntityResource.h"

namespace App::System
{
	//==========================================================================================
	// ModelFadeSystem
	//
	// モデルのフェード係数(ModelComponent::fade)を、メインカメラの依頼へ寄せる。
	//
	// ・依頼を作るのは TPSSystem で、メインカメラの TPSCameraStateComponent に置いてある。
	//     fadeTarget / playerFade             … 自機。カメラとの距離で決まる
	//     fadeObstacles / fadeObstacleValues  … 自機 → カメラのレイに掛かった地形。当たった点とカメラの距離で決まる
	// ・依頼は「そのエンティティと子孫」に当てる。自機の武器や外装は別エンティティの子なので、
	//   親を辿って依頼の相手に行き着いたモデルも一緒に消す。
	// ・障害物は大きすぎるモデル(CameraCollisionComponent::obstacleFadeMaxRadius 超)には当てない。
	//   地面や岩山まで消えて足元が抜けるのを防ぐ。
	// ・依頼の無いモデルは 1(見える)へ戻す。消えるのは速く、戻るのはゆっくり補間する。
	//
	// TPSSystem(カメラの位置と依頼を書く)の後に回るよう、TPSCameraState を読むと宣言している。
	// 書くのは自分のチャンクの ModelComponent だけなので、ワーカーで回す。
	//==========================================================================================
	namespace
	{
		//--------------------------------------------------------------------------------------
		// 指数減衰の補間係数。rate が 0 以下なら補間せずにすぐ合わせる
		//--------------------------------------------------------------------------------------
		float DampFactorOrSnap(float a_rate, float a_dt)
		{
			if (!(a_rate > 0.0f)) return 1.0f;
			if (!(a_dt > 0.0f)) return 0.0f;
			return 1.0f - std::exp(-a_rate * a_dt);
		}

		// メインカメラから集めた依頼
		struct FadeRequest
		{
			Engine::ECS::Entity target = Engine::ECS::Limits::INVALID_ENTITY;
			float playerFade = 1.0f;

			std::array<Engine::ECS::Entity, Component::TPSCameraStateComponent::FADE_OBSTACLE_MAX> obstacles = {};
			std::array<float, Component::TPSCameraStateComponent::FADE_OBSTACLE_MAX> obstacleValues = {};
			uint32_t obstacleCount = 0;

			Component::CameraCollisionComponent setting = {};

			// 1 未満の依頼が1つでもあるか。無ければ親を辿らずに全部 1 へ戻すだけ
			bool HasRequest() const
			{
				return obstacleCount > 0 || playerFade < 1.0f;
			}
		};

		//--------------------------------------------------------------------------------------
		// メインカメラ(TPS)の依頼を引く。TPSカメラでなければ依頼なし(全部 1 へ戻す)
		//--------------------------------------------------------------------------------------
		FadeRequest GatherRequest(Engine::ECS::World& a_world)
		{
			FadeRequest _req = {};
			if (!a_world.HasResource<InstanceResource::SingletonEntityResource>()) return _req;

			const Engine::ECS::Entity _camera = a_world.GetResource<InstanceResource::SingletonEntityResource>().mainCamera;
			if (_camera == Engine::ECS::Limits::INVALID_ENTITY) return _req;
			if (!a_world.IsAliveEntity(_camera)) return _req;

			if (a_world.HasComponent<Component::CameraCollisionComponent>(_camera))
			{
				if (const auto* _pSetting = a_world.RefData<Component::CameraCollisionComponent>(_camera))
				{
					_req.setting = *_pSetting;
				}
			}

			if (!a_world.HasComponent<Component::TPSCameraStateComponent>(_camera)) return _req;
			const auto* _pState = a_world.RefData<Component::TPSCameraStateComponent>(_camera);
			if (!_pState) return _req;

			_req.target		= _pState->fadeTarget;
			_req.playerFade	= std::clamp(_pState->playerFade, 0.0f, 1.0f);
			_req.obstacleCount = std::min(_pState->fadeObstacleCount, Component::TPSCameraStateComponent::FADE_OBSTACLE_MAX);
			for (uint32_t _k = 0; _k < _req.obstacleCount; ++_k)
			{
				_req.obstacles[_k]		= _pState->fadeObstacles[_k];
				_req.obstacleValues[_k]	= std::clamp(_pState->fadeObstacleValues[_k], 0.0f, 1.0f);
			}
			return _req;
		}

		//--------------------------------------------------------------------------------------
		// モデルのワールドでの境界球の半径(m)。求められなければ 0
		//--------------------------------------------------------------------------------------
		float CalcWorldRadius(
			Engine::ECS::World& a_world,
			const Engine::Resource::ResourceManager& a_resourceManager,
			Engine::ECS::Entity a_self,
			const Component::ModelComponent& a_model)
		{
			const auto* _pModel = a_resourceManager.Get(a_model.handle);
			if (!_pModel) return 0.0f;

			float _scale = 1.0f;
			if (a_world.HasComponent<Component::WorldMatrixComponent>(a_self))
			{
				if (const auto* _pMat = a_world.RefData<Component::WorldMatrixComponent>(a_self))
				{
					const Math::Matrix& _m = _pMat->worldMat;
					_scale = std::max({
						Math::Vector3(_m._11, _m._12, _m._13).Length(),
						Math::Vector3(_m._21, _m._22, _m._23).Length(),
						Math::Vector3(_m._31, _m._32, _m._33).Length() });
				}
			}
			return _pModel->GetLocalBounds().Radius * _scale;
		}

		//--------------------------------------------------------------------------------------
		// このモデルが寄せる先。自分と親を辿り、依頼の相手に行き着けばその値(複数なら小さい方)
		//--------------------------------------------------------------------------------------
		float CalcGoalFade(
			Engine::ECS::World& a_world,
			const Engine::Resource::ResourceManager& a_resourceManager,
			Engine::ECS::Entity a_self,
			const Component::ModelComponent& a_model,
			const FadeRequest& a_req)
		{
			// 親子の深さの上限(循環していても止まるように)
			constexpr int MAX_DEPTH = 16;

			float _goal = 1.0f;
			float _radius = -1.0f;	// 障害物に当たったときだけ求める

			Engine::ECS::Entity _cur = a_self;
			for (int _depth = 0; _depth < MAX_DEPTH; ++_depth)
			{
				if (_cur == Engine::ECS::Limits::INVALID_ENTITY) break;

				if (_cur == a_req.target)
				{
					_goal = std::min(_goal, a_req.playerFade);
				}

				for (uint32_t _k = 0; _k < a_req.obstacleCount; ++_k)
				{
					if (_cur != a_req.obstacles[_k]) continue;
					if (!(a_req.obstacleValues[_k] < _goal)) continue;

					// 大きな地形(地面など)は消さない
					const float _maxRadius = a_req.setting.obstacleFadeMaxRadius;
					if (_maxRadius > 0.0f)
					{
						if (_radius < 0.0f) _radius = CalcWorldRadius(a_world, a_resourceManager, a_self, a_model);
						if (_radius > _maxRadius) continue;
					}
					_goal = a_req.obstacleValues[_k];
				}

				// 親へ
				if (!a_world.IsAliveEntity(_cur)) break;
				if (!a_world.HasComponent<Component::HierarchyComponent>(_cur)) break;
				const auto* _pHierarchy = a_world.RefData<Component::HierarchyComponent>(_cur);
				if (!_pHierarchy) break;
				_cur = _pHierarchy->parentID;
			}
			return _goal;
		}
	}

	void ModelFadeSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveJobTask<Component::ModelComponent>(
			Engine::ECS::ESystemType::Camera,
			"ModelFadeSystem",
			[](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Engine::ECS::SystemContext& a_ctx,
				Component::ActiveTag* /*a_tags*/,
				Component::ModelComponent* a_modelArray
			)
			{
				if (!a_ctx.pWorld || !a_ctx.pServices || !a_ctx.pServices->pResourceManager) return;
				auto& _world = *a_ctx.pWorld;
				const auto& _resourceManager = *a_ctx.pServices->pResourceManager;

				const FadeRequest _req = GatherRequest(_world);
				const bool _hasRequest = _req.HasRequest();

				for (size_t _i = 0; _i < a_count; ++_i)
				{
					Component::ModelComponent& _model = a_modelArray[_i];

					// 依頼が無く、もう見えているものは何もしない(ほとんどのモデルはここで抜ける)
					if (!_hasRequest && _model.fade >= 1.0f) continue;

					const float _goal = _hasRequest
						? CalcGoalFade(_world, _resourceManager, a_pChunk->entityData[_i], _model, _req)
						: 1.0f;

					// 消えるのは速く、戻るのはゆっくり
					const float _rate = (_goal < _model.fade) ? _req.setting.fadeOutRate : _req.setting.fadeInRate;
					_model.fade += (_goal - _model.fade) * DampFactorOrSnap(_rate, a_ctx.dt);

					// 戻り切る直前は 1 に揃える(1 未満のままだとフェード用の PSO で描き続ける)
					if (_goal >= 1.0f && _model.fade > 0.995f) _model.fade = 1.0f;
					_model.fade = std::clamp(_model.fade, 0.0f, 1.0f);
				}
			}
		)
		// 絞り込みに使わない読み : カメラの依頼と設定、親子、境界球の大きさに掛ける拡大率
		.Reads<Component::TPSCameraStateComponent, Component::CameraCollisionComponent,
			Component::HierarchyComponent, Component::WorldMatrixComponent>()
		.ReadsResource<InstanceResource::SingletonEntityResource>();
	}
}
