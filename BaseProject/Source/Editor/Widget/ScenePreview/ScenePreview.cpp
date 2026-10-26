#include "ScenePreview.h"

#include "../../EditorCamera/EditorCamera.h"

#include "Engine/MainEngine.h"
#include "Engine/ECS/World/World.h"
#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"

namespace Editor
{
	namespace
	{
		// カメラの定位置。原点に出るものが正面に収まる位置
		const Math::Vector3 CAMERA_HOME_POS = { 0.0f, 2.5f, -8.0f };
		constexpr float CAMERA_HOME_YAW = 0.0f;
		constexpr float CAMERA_HOME_PITCH = 10.0f;

		// 上段のボタンの幅
		const Math::Vector2 TOOL_BUTTON_SIZE = { 80.0f, 0.0f };
	}

	ScenePreview::ScenePreview(ECS::EngineServices* a_pServices)
		: m_pServices(a_pServices)
	{}
	ScenePreview::~ScenePreview() = default;

	//======================================================================================
	// 開閉
	//======================================================================================
	void ScenePreview::BeginOpen()
	{
		auto* _pGE = m_pServices->pMainEngine->RefGraphicsEngine();

		if (!m_upCamera)
		{
			m_upCamera = std::make_unique<EditorCamera>();
			m_upCamera->Init(_pGE);
		}

		// ゲームのシーンとまったく同じ構成(コンポーネント・システム・ワールドリソース)で作る。
		// 見え方と動き方を本番と揃えるのが目的なので、ここで簡易版を組んではいけない
		if (!m_upWorld)
		{
			m_upWorld = Scene::CreateSceneWorld(*m_pServices->pMainEngine, true);
		}

		//----------------------------------------------------------------------------------
		// 描画構成を借りる
		//
		// ゲームのシーンが直前まで画面を作っていた構成をそのまま使う。
		// ここで別のものを組むと、合わせた見た目がゲームへ持っていくとずれる。
		//
		// 借りるのは開いた瞬間の1回だけ。開いているあいだはこちらのカメラが
		// メインになるので、毎フレーム引き直すと自分の値を借り直すことになる
		//----------------------------------------------------------------------------------
		if (_pGE)
		{
			m_pipelineHandle = _pGE->GetCameraPipelines()->GetLastMainPipelineHandle();
		}

		// カメラは開くたびに定位置へ。
		// 前回どこかへ飛ばしたまま開くと、出したものが画面の外から始まってしまう
		ResetCamera();

		// 再生は開くたびに頭から
		m_isPlaying = true;
	}

	void ScenePreview::EndOpen()
	{
		if (m_upCamera) m_upCamera->CancelControl();
	}

	void ScenePreview::Release()
	{
		// World::Release() は呼ばない。
		// あちらは「ECS参照カウントを全部リセット → GCで数え直し → 未参照を解放」まで走るので、
		// 数えられるのはこのワールドの中身だけになり、ゲームのシーンが使っている
		// モデルやテクスチャまで巻き添えで解放されてしまう。
		// ここが呼ばれるのはアプリ終了時(ResourceManager::Release のあと)なので、
		// 持ち物を捨てるだけでよい。
		m_upWorld.reset();
		m_upCamera.reset();
	}

	//======================================================================================
	// ワールド
	//======================================================================================
	void ScenePreview::RunUpdate(float a_dt)
	{
		if (!m_upWorld) return;

		// ---- ゲームのシーンと同じ順でフェーズを回す(BaseScene::Update と同じ) ----
		// 当たり判定の空間もこのワールドの持ち物(CreateSceneWorld が足している)なので、
		// ゲームのシーンと同じ手順をそのまま踏める
		m_upWorld->BeginFrame();

		m_upWorld->RunSystem(ECS::ESystemType::Input, a_dt);
		m_upWorld->RunSystem(ECS::ESystemType::PreUpdate, a_dt);
		m_upWorld->RunSystem(ECS::ESystemType::Update, a_dt);

		// 判定クエリ(Physics)の前に物理空間を進める
		m_upWorld->RefResource<Physics::PhysicsWorld>().Update(a_dt);

		m_upWorld->RunSystem(ECS::ESystemType::Physics, a_dt);
		m_upWorld->RunSystem(ECS::ESystemType::Animation, a_dt);
		m_upWorld->RunSystem(ECS::ESystemType::Camera, a_dt);
		m_upWorld->RunSystem(ECS::ESystemType::PostUpdate, a_dt);
	}

	void ScenePreview::RunDraw()
	{
		if (!m_upWorld) return;

		if (auto* _pGE = m_pServices->pMainEngine->RefGraphicsEngine())
		{
			// 確認用ワールドのアニメーションモデルの BLAS と頂点領域(PreDraw より前に)
			_pGE->RefDrawSubmitter()->ProcessDynamicRaytracingInit(*m_upWorld);
		}

		m_upWorld->RunSystem(ECS::ESystemType::PreDraw, 0.0f);
		m_upWorld->RunSystem(ECS::ESystemType::Draw, 0.0f);
		m_upWorld->RunSystem(ECS::ESystemType::PostDraw, 0.0f);

		//----------------------------------------------------------------------------------
		// このプレビューのカメラを積む
		//
		// このワールドにはカメラのエンティティが居ないので、
		// CameraPipelineSubmitSystem は何も送らない。ここで直接送る
		//----------------------------------------------------------------------------------
		SubmitCamera();

		// 大きさの目安になる格子
		if (m_isDrawGrid) DrawGrid();
	}

	void ScenePreview::DestroyEntities(const std::vector<ECS::Entity>& a_entityVec)
	{
		if (!m_upWorld || a_entityVec.empty()) return;

		for (const ECS::Entity& _entity : a_entityVec)
		{
			m_upWorld->ReserveReleaseEntity(_entity);
		}

		// 解放予約は次の BeginFrame で消化される。
		// 閉じたあとはこのワールドを回さないので、ここで1回だけ空回しして始末しておく
		m_upWorld->BeginFrame();
	}

	void ScenePreview::DestroyAllEntities()
	{
		if (!m_upWorld) return;

		std::vector<ECS::Entity> _targets = {};
		for (const auto& _location : m_upWorld->GetEntityList())
		{
			const ECS::Entity _entity = m_upWorld->GetEntity(_location);
			if (_entity == ECS::Limits::INVALID_ENTITY) continue;

			_targets.push_back(_entity);
		}

		DestroyEntities(_targets);
	}

	//======================================================================================
	// カメラ : シーンビューのフリーカメラと同じもの
	//======================================================================================
	void ScenePreview::UpdateCamera(float a_dt)
	{
		if (m_upCamera) m_upCamera->Update(a_dt);
	}

	bool ScenePreview::TryGetCameraOverride(Math::Matrix& a_outWorldMat, Math::Matrix& a_outProjMat) const
	{
		if (!m_upCamera) return false;

		a_outWorldMat = m_upCamera->GetWorldMatrix();
		a_outProjMat = m_upCamera->GetProjMatrix();
		return true;
	}

	void ScenePreview::SubmitCamera()
	{
		if (!m_upWorld || !m_upCamera) return;
		if (!m_pipelineHandle.IsValid()) return;

		auto* _pGE = m_pServices->pMainEngine->RefGraphicsEngine();
		if (!_pGE) return;

		// isMain を立てるのは、ゲームのカメラが1台も積まれないあいだに
		// 画面を作る役が居なくなるのを防ぐため。
		// 実際に見えるのはモーダルの中なので、バックバッファの中身は問題にならない
		Engine::Graphics::CameraSubmitDesc _desc = {};
		_desc.pWorld			= m_upWorld.get();
		_desc.entity			= PREVIEW_CAMERA_ENTITY;
		_desc.pipelineHandle	= m_pipelineHandle;
		_desc.worldMat			= m_upCamera->GetWorldMatrix();
		_desc.projMat			= m_upCamera->GetProjMatrix();
		_desc.viewportWidth		= static_cast<UINT>(_pGE->GetRenderWidth());
		_desc.viewportHeight	= static_cast<UINT>(_pGE->GetRenderHeight());
		_desc.order				= 0;
		_desc.isMain			= true;

		_pGE->RefCameraPipelines()->SubmitCamera(_desc);
	}

	void ScenePreview::DrawGrid() const
	{
		const Math::Color _lineColor(0.30f, 0.32f, 0.36f, 1.0f);
		const Math::Color _axisColor(0.55f, 0.58f, 0.65f, 1.0f);

		// 積む先はエンジン側の置き場。エディターも他と同じ経路で入れる
		auto* _pDebugDraw = m_pServices->pDebugDraw;
		if (!_pDebugDraw) return;

		// 格子は出す位置(原点)に敷く。1mごと
		const float _half = m_gridSize;
		const int   _count = static_cast<int>(m_gridSize);

		for (int _i = -_count; _i <= _count; ++_i)
		{
			const float _p = static_cast<float>(_i);
			const Math::Color& _col = (_i == 0) ? _axisColor : _lineColor;

			_pDebugDraw->DrawLine(Math::Vector3(_p, 0.0f, -_half), Math::Vector3(_p, 0.0f, _half), _col);
			_pDebugDraw->DrawLine(Math::Vector3(-_half, 0.0f, _p), Math::Vector3(_half, 0.0f, _p), _col);
		}
	}

	void ScenePreview::ResetCamera()
	{
		if (m_upCamera) m_upCamera->SetPose(CAMERA_HOME_POS, CAMERA_HOME_YAW, CAMERA_HOME_PITCH);
	}

	//======================================================================================
	// UI の部品
	//======================================================================================
	void ScenePreview::DrawPlayButton()
	{
		if (Engine::EditorField::Button(m_isPlaying ? "Pause" : "Play", TOOL_BUTTON_SIZE))
		{
			m_isPlaying = !m_isPlaying;
		}
	}

	void ScenePreview::DrawSpeedSlider()
	{
		Engine::EditorField::SetNextItemWidth(160.0f);
		Engine::EditorField::Slider("Speed", m_playSpeed, 0.05f, 3.0f, "x%.2f");
	}

	void ScenePreview::DrawGridToggle()
	{
		Engine::EditorField::Field("Grid", m_isDrawGrid);
	}

	void ScenePreview::DrawResetCameraButton()
	{
		if (Engine::EditorField::Button("Reset Camera")) ResetCamera();
		Engine::EditorField::Tooltip("右ドラッグ中のみ視点操作 / WASD・EQ移動 / Shift加速");
	}

	void ScenePreview::DrawWireOffNote(bool a_isNeeded) const
	{
		if (!a_isNeeded) return;
		if (!m_pServices->pDebugDraw || m_pServices->pDebugDraw->GetConfig().drawWire) return;

		Engine::EditorField::SameLine();
		Engine::EditorField::HelpText("(Option の Draw Debug Wire が off のため線は出ません)");
	}

	bool ScenePreview::DrawViewport(float a_reserveLines, ImVec2& a_outMin, ImVec2& a_outSize)
	{
		auto* _pGE = m_pServices->pMainEngine->RefGraphicsEngine();
		if (!_pGE) { Engine::EditorField::HelpText("GraphicsEngine がありません"); return false; }

		if (!m_pipelineHandle.IsValid())
		{
			Engine::EditorField::HelpText("描画構成が決まっていません(シーンを一度開いてから開き直してください)");
			return false;
		}

		// このプレビューのカメラが描いた絵をそのまま出す。
		// ゲームのシーンと同じ設計図を通っているので、ここで見えているものが本番の見え方
		const auto* _pTex = _pGE->GetCameraPipelines()->GetCameraFinalTexture(m_upWorld.get(), PREVIEW_CAMERA_ENTITY);
		if (!_pTex) { Engine::EditorField::HelpText("出力テクスチャがまだありません"); return false; }

		const float _aspect = (_pGE->GetRenderHeight() > 0)
			? static_cast<float>(_pGE->GetRenderWidth()) / static_cast<float>(_pGE->GetRenderHeight())
			: 16.0f / 9.0f;

		// 情報欄のぶんを残して、収まる大きさへ合わせる
		const ImVec2 _avail = ImGui::GetContentRegionAvail();
		const float _reserveY = ImGui::GetTextLineHeightWithSpacing() * a_reserveLines;
		const float _maxH = (std::max)(64.0f, _avail.y - _reserveY);

		ImVec2 _size((std::max)(64.0f, _avail.x), 0.0f);
		_size.y = _size.x / _aspect;
		if (_size.y > _maxH)
		{
			_size.y = _maxH;
			_size.x = _maxH * _aspect;
		}

		a_outMin = ImGui::GetCursorScreenPos();
		a_outSize = _size;

		auto _gpuHandle = EditorHelper::GetImGuiTexHandle(_pTex->GetImGuiSRV());
		ImGui::Image(static_cast<ImTextureID>(_gpuHandle.ptr), _size);

		// フリーカメラへホバー状態を渡す。
		// 右クリックの開始位置がこの画像の上の時だけ操作を始めるための判定(シーンビューと同じ)
		if (m_upCamera) m_upCamera->SetViewportHovered(ImGui::IsItemHovered());
		return true;
	}
}
