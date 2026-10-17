#include "CamSetShaderSystem.h"

#include "Application/ECS/World/APPWorld.h"
#include "Engine/MainEngine.h"

#include "Application/Components/Camera/CameraTag.h"

#include "Application/Components/Transform/WorldMatrixComponent.h"

#include "Application/Components/Camera/CameraParamComponent.h"
#include "Application/Components/Camera/FocusParamComponent.h"
#include "Application/Components/Camera/RadialBlurComponent.h"
#include "Application/Components/Camera/FishEyeComponent.h"
#include "Application/Components/Camera/ProjMatComponent.h"

#include "Application/InstanceResource/SingletonEntityResource.h"

#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/SceneView/CameraEffectData.h"

namespace App::System
{
	//==========================================================================================
	// CamSetShaderSystem
	//
	// メインカメラのビュー/射影行列(と被写界深度の設定)を GraphicsEngine へ送る。
	//
	// どのカメラで映すかは MainCameraSystem が決めて
	// SingletonEntityResource.mainCamera に置いてあるので、ここでは探さずに引くだけ。
	//==========================================================================================
	void CamSetShaderSystem::Init(App::ECS::APPWorld& a_world)
	{
		a_world.ActiveCustomTask(
			Engine::ECS::ESystemType::PreDraw,
			"CamSetShaderSystem",
			Engine::ECS::ReadList<Component::CameraTag, Component::ProjMatComponent, Component::WorldMatrixComponent, Component::FocusParamComponent, Component::RadialBlurComponent, Component::FishEyeComponent>{},
			Engine::ECS::WriteList<>{},
			[](const Engine::ECS::SystemContext& a_ctx)
			{
				if (!a_ctx.pWorld) return;
				if (!a_ctx.pWorld->HasResource<InstanceResource::SingletonEntityResource>()) return;

				const auto& _singleton = a_ctx.pWorld->GetResource<InstanceResource::SingletonEntityResource>();

				const Engine::ECS::Entity _camera = _singleton.mainCamera;
				if (_camera == Engine::ECS::Limits::INVALID_ENTITY) return;
				if (!a_ctx.pWorld->IsAliveEntity(_camera)) return;

				// 行列が無いカメラは送りようがない
				if (!a_ctx.pWorld->HasComponent<Component::ProjMatComponent>(_camera))		return;
				if (!a_ctx.pWorld->HasComponent<Component::WorldMatrixComponent>(_camera))	return;

				const auto* _pProjMat	= a_ctx.pWorld->RefData<Component::ProjMatComponent>(_camera);
				const auto* _pWorldMat	= a_ctx.pWorld->RefData<Component::WorldMatrixComponent>(_camera);
				if (!_pProjMat || !_pWorldMat) return;

				auto* _pGE = a_ctx.pServices->pMainEngine->RefGraphicsEngine();

				_pGE->RefSceneView()->SetCameraMat(_pWorldMat->worldMat);
				_pGE->RefSceneView()->SetProjMat(_pProjMat->projMat);

				//============================================================
				// 画面効果の設定
				//------------------------------------------------------------
				// ピントもラジアルブラーも魚眼レンズもカメラの持ち物なので、それぞれの
				// コンポーネントから送る。持っていないカメラもあるので、
				// あるときだけ送る。
				// (無いことを理由に打ち切ると、その設定を持たないカメラが
				//  ビュー/射影行列ごと設定されなくなってしまう。
				//  片方が無いだけでもう片方まで送られないのも同じ理由で不可)
				//
				// 未設定のフレームは GraphicsEngine 側でリセット済み = 効果なし。
				//============================================================

				// 被写界深度(DoF)
				if (a_ctx.pWorld->HasComponent<Component::FocusParamComponent>(_camera))
				{
					if (const auto* _pFocus = a_ctx.pWorld->RefData<Component::FocusParamComponent>(_camera))
					{
						Engine::Graphics::DoFOptionCB _dofCB = {};
						_dofCB.focusDistance	= _pFocus->focusDistance;
						_dofCB.focusRange		= _pFocus->focusRange;
						_dofCB.nearRange		= _pFocus->nearRange;
						_dofCB.farRange			= _pFocus->farRange;
						_dofCB.maxBlurRadius	= _pFocus->maxBlurRadius;
						_dofCB.enable			= _pFocus->enable ? 1 : 0;
						_pGE->RefSceneView()->SetDoFData(_dofCB);
					}
				}

				// ラジアルブラー
				if (a_ctx.pWorld->HasComponent<Component::RadialBlurComponent>(_camera))
				{
					if (const auto* _pRadial = a_ctx.pWorld->RefData<Component::RadialBlurComponent>(_camera))
					{
						Engine::Graphics::RadialBlurOptionCB _radialCB = {};
						// 中心は震え(RadialBlurShakeSystem)のぶんずらす
						const Math::Vector2 _center = _pRadial->GetCenter();
						_radialCB.blurCenter	= { _center.x, _center.y };
						// 速度から作った量 + 常時掛かる量 + 震えのぶん。
						// 速度ぶんを書いているのは RadialBlurSpeedSystem、震えは RadialBlurShakeSystem(どちらも Camera帯)
						_radialCB.strength		= _pRadial->GetStrength();
						_radialCB.sampleCount	= _pRadial->sampleCount;
						_radialCB.radius		= _pRadial->radius;
						_radialCB.falloff		= _pRadial->falloff;
						_radialCB.enable		= _pRadial->IsActive() ? 1 : 0;
						_pGE->RefSceneView()->SetRadialBlurData(_radialCB);
					}
				}

				// 魚眼レンズ
				if (a_ctx.pWorld->HasComponent<Component::FishEyeComponent>(_camera))
				{
					if (const auto* _pFishEye = a_ctx.pWorld->RefData<Component::FishEyeComponent>(_camera))
					{
						Engine::Graphics::FishEyeOptionCB _fishEyeCB = {};
						_fishEyeCB.center	= { _pFishEye->center.x, _pFishEye->center.y };
						_fishEyeCB.strength	= _pFishEye->strength;
						_fishEyeCB.enable	= _pFishEye->enable ? 1 : 0;
						_pGE->RefSceneView()->SetFishEyeData(_fishEyeCB);
					}
				}
			}
		)
		.ReadsResource<InstanceResource::SingletonEntityResource>();
	}
}
