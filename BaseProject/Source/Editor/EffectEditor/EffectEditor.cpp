#include "EffectEditor.h"

#include "../Editor.h"
#include "../Helper/EditorHelper.h"
#include "../EditorCamera/EditorCamera.h"

#include "../Panel/InspectorPanel/AssetInspector/ResourceDraw/EffectAssetEdit/EffectAssetEdit.h"
#include "../Panel/InspectorPanel/AssetInspector/ResourceDraw/ParticleEdit/ParticleEdit.h"

#include "Engine/MainEngine.h"
#include "Engine/Effect/EffectPlayer.h"
#include "Engine/ECS/World/World.h"
#include "Engine/Scene/BaseScene/BaseScene.h"
#include "Engine/Scene/SceneAmbient/SceneAmbient.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"
#include "Engine/Resource/Data/Particles/ParticlesAsset.h"
#include "Engine/Audio/AudioManager.h"

#include "../Panel/InspectorPanel/AssetInspector/ResourceDraw/ResourceDraw.h"
#include "Engine/Resource/Data/EffectPrefab/EffectPrefab.h"

#include "Application/Components/Effect/EffectAssetComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Core/LifeTimeComponent.h"
#include "Application/Utility/EffectSpawnHelper.h"
#include "Application/Utility/EffectPrefabSpawnHelper.h"

namespace Editor
{
	namespace
	{
		// ポップアップのID。OpenPopup と BeginPopupModal で同じものを使う
		constexpr const char* POPUP_ID = "Effect Editor";

		// エフェクトを出す位置。プレビューは常に原点固定にしておく
		const Math::Vector3 EFFECT_ORIGIN = { 0.0f, 0.0f, 0.0f };

		// カメラの定位置。原点に出るエフェクトが正面に収まる位置
		const Math::Vector3 CAMERA_HOME_POS = { 0.0f, 2.5f, -8.0f };
		constexpr float CAMERA_HOME_YAW = 0.0f;
		constexpr float CAMERA_HOME_PITCH = 10.0f;
	}

	//======================================================================================
	// プレビュー中のエフェクトへの参照
	//
	// 実体はワールドの中にあり、生成は遅延(次の BeginFrame)なので、
	// 「まだ居ない」状態を素直に扱えるようにまとめてある。
	//======================================================================================
	struct EffectEditor::EffectRef
	{
		ECS::Entity entity = ECS::Limits::INVALID_ENTITY;
		App::Component::EffectRuntimeComponent* pRuntime = nullptr;			// ハンドルと進行状態
		App::Component::EffectPlayRequestComponent* pRequest = nullptr;		// 再生の要求

		bool IsValid() const { return pRuntime != nullptr && pRequest != nullptr; }
	};

	EffectEditor::EffectEditor(ECS::EngineServices* a_pServices)
		: m_pServices(a_pServices)
		, m_upPlainAmbient(std::make_unique<Scene::SceneAmbient>(Scene::SceneAmbient::CreatePlain()))
	{}
	EffectEditor::~EffectEditor() = default;

	//======================================================================================
	// 開く / 閉じる
	//======================================================================================
	void EffectEditor::Open(const Core::GUID& a_effectGUID)
	{
		if (a_effectGUID == Core::DEFAULT_GUID) return;

		// 開き直しでも中身は作り直す(別のエフェクトを選んだ場合があるため)
		DestroyEffectEntity();

		m_mode = EMode::Effect;
		m_effectGUID = a_effectGUID;
		m_effectHandle =
			m_pServices->pResourceManager->LoadImmediate<Resource::EffectAsset>(a_effectGUID);
		m_effectPrefabHandle = {};

		BeginOpen();
	}

	void EffectEditor::OpenEffectPrefab(const Core::GUID& a_effectPrefabGUID)
	{
		if (a_effectPrefabGUID == Core::DEFAULT_GUID) return;

		DestroyEffectEntity();

		m_mode = EMode::EffectPrefab;
		m_effectGUID = a_effectPrefabGUID;
		m_effectHandle = {};
		m_effectPrefabHandle =
			m_pServices->pResourceManager->LoadImmediate<Resource::EffectPrefab>(a_effectPrefabGUID);

		BeginOpen();
	}

	void EffectEditor::BeginOpen()
	{
		m_isOpen = true;
		m_isOpenRequest = true;

		// 再生状態は開くたびに頭から
		m_isPlaying = true;
		m_isRestartRequest = false;
		m_selectedParticlePart = 0;

		EnsureWorld();

		//----------------------------------------------------------------------------------
		// 描画構成を借りる
		//
		// ゲームのシーンが直前まで画面を作っていた構成をそのまま使う。
		// ここで別のものを組むと、合わせた見た目がゲームへ持っていくとずれる。
		//
		// 借りるのは開いた瞬間の1回だけ。開いているあいだはこちらのカメラが
		// メインになるので、毎フレーム引き直すと自分の値を借り直すことになる
		//----------------------------------------------------------------------------------
		if (auto* _pGE = MainEngine::Instance().RefGraphicsEngine())
		{
			m_pipelineHandle = _pGE->GetCameraPipelines()->GetLastMainPipelineHandle();
		}

		// カメラは開くたびに定位置へ。
		// 前回どこかへ飛ばしたまま開くと、出したものが画面の外から始まってしまう
		if (m_upCamera) m_upCamera->SetPose(CAMERA_HOME_POS, CAMERA_HOME_YAW, CAMERA_HOME_PITCH);

		ReserveSpawn();
	}

	void EffectEditor::Close()
	{
		if (!m_isOpen) return;

		DestroyEffectEntity();

		m_isOpen = false;
		m_mode = EMode::Effect;
		m_effectGUID = Core::DEFAULT_GUID;
		m_effectHandle = {};
		m_effectPrefabHandle = {};

		// 押しっぱなし扱いを閉じたあとへ持ち越さない
		if (m_upCamera) m_upCamera->CancelControl();
	}

	void EffectEditor::Release()
	{
		// World::Release() は呼ばない。
		// あちらは「ECS参照カウントを全部リセット → GCで数え直し → 未参照を解放」まで走るので、
		// 数えられるのはこのワールドの中身だけになり、ゲームのシーンが使っている
		// モデルやテクスチャまで巻き添えで解放されてしまう。
		// ここが呼ばれるのはアプリ終了時(ResourceManager::Release のあと)なので、
		// 持ち物を捨てるだけでよい。
		m_upWorld.reset();
		m_upCamera.reset();
		m_isOpen = false;
	}

	//======================================================================================
	// プレビュー用ワールド
	//======================================================================================
	void EffectEditor::EnsureWorld()
	{
		if (!m_upCamera)
		{
			m_upCamera = std::make_unique<EditorCamera>();
			m_upCamera->Init(m_pServices->pMainEngine->RefGraphicsEngine());
		}

		if (m_upWorld) return;

		// ゲームのシーンとまったく同じ構成(コンポーネント・システム・ワールドリソース)で作る。
		// 描画のされ方を本番と揃えるのが目的なので、ここで簡易版を組んではいけない
		m_upWorld = Scene::CreateSceneWorld(*m_pServices->pMainEngine, true);
	}

	void EffectEditor::ReserveSpawn()
	{
		if (!m_upWorld) return;
		if (m_effectGUID == Core::DEFAULT_GUID) return;

		// エフェクトプレハブ : ゲームと同じ経路で炊く(寿命も付くので、放っておけば全部消える)。
		// 編集中の値は、メモリ上のアセットをそのまま使うので保存しなくても反映される
		if (m_mode == EMode::EffectPrefab)
		{
			m_prefabElapsed = 0.0f;
			if (const auto* _pEffectPrefab = RefEffectPrefab())
			{
				App::Utility::SpawnEffectPrefab(*m_upWorld, *_pEffectPrefab, EFFECT_ORIGIN);
			}
			return;
		}

		// 実体化は次の BeginFrame。
		// 出し切っても消えないようにしておく(何度も再生し直したいので寿命はこちらが握る)。
		// 発生位置は常に原点。カメラは自由に動かせるので、見る位置と出す位置は分けておく
		App::Utility::ReserveSpawnEffectAt(*m_upWorld, m_effectGUID, EFFECT_ORIGIN, false);
	}

	void EffectEditor::DestroyEffectEntity()
	{
		if (!m_upWorld) return;

		// プレビュー用ワールドにはエフェクトしか居ないので、見つけたものを全部片付ける。
		// エフェクトプレハブで出したもの(破片など、エフェクトを持たないものもある)は
		// 必ず寿命を持つので、そちらでも拾う
		std::vector<ECS::Entity> _targets = {};
		m_upWorld->ForEach<App::Component::EffectAssetComponent>(
			[&](ECS::Chunk* a_pChunk, uint32_t a_count, App::Component::EffectAssetComponent*)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					_targets.push_back(a_pChunk->entityData[_i]);
				}
			}
		);
		m_upWorld->ForEach<App::Component::LifeTimeComponent>(
			[&](ECS::Chunk* a_pChunk, uint32_t a_count, App::Component::LifeTimeComponent*)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					_targets.push_back(a_pChunk->entityData[_i]);
				}
			}
		);

		// 両方を持つものは2回積まれるので、重ねて解放予約しないよう1つにする
		std::sort(_targets.begin(), _targets.end());
		_targets.erase(std::unique(_targets.begin(), _targets.end()), _targets.end());

		for (const ECS::Entity& _entity : _targets)
		{
			m_upWorld->ReserveReleaseEntity(_entity);
		}

		// 解放予約は次の BeginFrame で消化される。
		// 閉じたあとはこのワールドを回さないので、ここで1回だけ空回しして始末しておく
		if (!_targets.empty())
		{
			m_upWorld->BeginFrame();
		}
	}

	EffectEditor::EffectRef EffectEditor::FindEffect() const
	{
		EffectRef _ref = {};
		if (!m_upWorld) return _ref;

		m_upWorld->ForEach<App::Component::EffectRuntimeComponent, App::Component::EffectPlayRequestComponent>(
			[&](ECS::Chunk* a_pChunk, uint32_t a_count, App::Component::EffectRuntimeComponent* a_runtimeArray, App::Component::EffectPlayRequestComponent* a_requestArray)
			{
				if (_ref.IsValid()) return;		// 先に見つけたものを使う(プレビューは常に1つ)
				if (a_count == 0) return;

				_ref.entity = a_pChunk->entityData[0];
				_ref.pRuntime = &a_runtimeArray[0];
				_ref.pRequest = &a_requestArray[0];
			}
		);

		return _ref;
	}

	Resource::EffectAsset* EffectEditor::RefEffectAsset() const
	{
		return m_pServices->pResourceManager->Ref(m_effectHandle);
	}

	Resource::EffectPrefab* EffectEditor::RefEffectPrefab() const
	{
		return m_pServices->pResourceManager->Ref(m_effectPrefabHandle);
	}

	int EffectEditor::CountPrefabEntities() const
	{
		if (!m_upWorld) return 0;

		// エフェクトプレハブで出したものは全部寿命を持つ(SpawnEffectPrefab が付ける)
		int _count = 0;
		m_upWorld->ForEach<App::Component::LifeTimeComponent>(
			[&](ECS::Chunk*, uint32_t a_count, App::Component::LifeTimeComponent*)
			{
				_count += static_cast<int>(a_count);
			}
		);
		return _count;
	}

	Resource::ParticlesAsset* EffectEditor::RefSelectedParticleAsset() const
	{
		const auto* _pEffect = RefEffectAsset();
		if (!_pEffect) return nullptr;

		const auto& _parts = _pEffect->GetParticleParts();
		if (m_selectedParticlePart < 0) return nullptr;
		if (static_cast<size_t>(m_selectedParticlePart) >= _parts.size()) return nullptr;

		return m_pServices->pResourceManager->Ref(_parts[m_selectedParticlePart].particleHandle);
	}

	//======================================================================================
	// 更新 : ゲームのシーンの代わりに、このワールドだけを回す
	//======================================================================================
	void EffectEditor::UpdateScene(float a_dt)
	{
		if (!m_isOpen || !m_upWorld) return;

		// 止めているあいだは時間を進めない。
		// エフェクトの経過時間もパーティクルの発生もここの dt で決まるので、
		// 0 を流すだけで「その瞬間で固まる」
		const float _dt = m_isPlaying ? (a_dt * m_playSpeed) : 0.0f;

		//------------------------------------------------------------------
		// エフェクトプレハブ : 全部消えたら炊き直す / Restart で片付けて炊き直す
		//
		// 生成は遅延なので、炊いた直後の数フレームは数が0のまま。
		// 少し時間が経ってからの0だけを「消えた」とみなす
		//------------------------------------------------------------------
		if (m_mode == EMode::EffectPrefab)
		{
			if (m_isRestartRequest)
			{
				m_isRestartRequest = false;
				DestroyEffectEntity();
				ReserveSpawn();
			}
			else if (m_isLoop && m_prefabElapsed > 0.2f && CountPrefabEntities() == 0)
			{
				ReserveSpawn();
			}

			m_prefabElapsed += _dt;
		}
		// ---- 再生の指示をコンポーネントへ書いてから回す ----
		else if (EffectRef _ref = FindEffect(); _ref.IsValid())
		{
			auto* _pEffect = m_pServices->pResourceManager->Ref(_ref.pRuntime->effectHandle);

			// 頭から再生し直す。
			// isPlay を落として立ち上げ直すと2フレームかかるので、実体を直接叩く
			if (m_isRestartRequest && _pEffect)
			{
				Effect::EffectPlayer::Play(*_pEffect, _ref.pRuntime->instance);
				m_isRestartRequest = false;
			}

			_ref.pRequest->isPlay = true;

			// 持ち主の位置と向き(回したとき・動かしたときの見え方を確かめる)
			ApplyPreviewTransform(_ref.entity, _dt);

			// 出し切ったら頭から。
			// 出しっぱなしのパーツを含むエフェクトは IsFinished が立たないので、
			// ループ指定でも何も起きない(それでよい : もともと終わらない演出のため)
			//
			// 音も見るのはゲーム側(EffectUpdateSystem)と揃えるため。
			// 見ないと、絵が終わった時点で頭出しされて音が毎回途中で切れる
			if (m_isLoop && _pEffect &&
				Effect::EffectPlayer::IsFinished(*_pEffect, _ref.pRuntime->instance, m_pServices->pAudioManager))
			{
				m_isRestartRequest = true;
			}
		}

		// ---- ゲームのシーンと同じ順でフェーズを回す ----
		// 当たり判定の空間もこのワールドの持ち物(CreateSceneWorld が足している)なので、
		// ゲームのシーンと同じ手順をそのまま踏める。
		// プレビューにコライダーが居なければ空のまま素通りするだけ
		m_upWorld->BeginFrame();

		m_upWorld->RunSystem(ECS::ESystemType::Input, _dt);
		m_upWorld->RunSystem(ECS::ESystemType::PreUpdate, _dt);
		m_upWorld->RunSystem(ECS::ESystemType::Update, _dt);

		// 判定クエリ(Physics)の前に物理空間を進める。BaseScene::Update と同じ位置
		m_upWorld->RefResource<Physics::PhysicsWorld>().Update(_dt);

		m_upWorld->RunSystem(ECS::ESystemType::Physics, _dt);
		m_upWorld->RunSystem(ECS::ESystemType::Animation, _dt);
		m_upWorld->RunSystem(ECS::ESystemType::Camera, _dt);
		m_upWorld->RunSystem(ECS::ESystemType::PostUpdate, _dt);
	}

	//======================================================================================
	// プレビューの持ち主の位置と向き
	//
	// 回転は Pitch(X) / Yaw(Y) / Roll(Z) の度。回すと形状の +Z(噴き出す向き)が付いてくる。
	// 円運動は止めている(dt = 0)あいだは進まない
	//======================================================================================
	void EffectEditor::ApplyPreviewTransform(ECS::Entity a_entity, float a_dt)
	{
		if (!m_upWorld) return;
		if (!m_upWorld->HasComponent<App::Component::LocalTransformComponent>(a_entity)) return;

		auto* _pTransform = m_upWorld->RefData<App::Component::LocalTransformComponent>(a_entity);
		if (!_pTransform) return;

		Math::Vector3 _pos = EFFECT_ORIGIN;
		if (m_isPreviewOrbit)
		{
			m_previewOrbitAngle = std::fmod(m_previewOrbitAngle + m_previewOrbitSpeed * a_dt, 360.0f);
			const float _rad = DirectX::XMConvertToRadians(m_previewOrbitAngle);
			_pos += Math::Vector3(std::cos(_rad), 0.0f, std::sin(_rad)) * m_previewOrbitRadius;
		}

		const Math::Quaternion _quat = Math::Quaternion::CreateFromYawPitchRoll(
			DirectX::XMConvertToRadians(m_previewRotation.y),
			DirectX::XMConvertToRadians(m_previewRotation.x),
			DirectX::XMConvertToRadians(m_previewRotation.z));

		_pTransform->pos = _pos;
		_pTransform->quat = _quat;
		_pTransform->isDirty = true;
	}

	//======================================================================================
	// 描画 : ゲームのシーンの代わりに、このワールドの命令をレンダーグラフへ流す
	//======================================================================================
	void EffectEditor::DrawScene()
	{
		if (!m_isOpen || !m_upWorld) return;

		// 背景(空・フォグ・光)は SceneManager がこの前に流し込み済み(GetPreviewAmbient で選んだもの)
		if (auto* _pGE = MainEngine::Instance().RefGraphicsEngine())
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
		// CameraPipelineSubmitSystem は何も送らない。ここで直接送る。
		//
		// isMain を立てるのは、ゲームのカメラが1台も積まれないこのあいだに
		// 画面を作る役が居なくなるのを防ぐため。
		// 実際に見えるのはモーダルの中なので、バックバッファの中身は問題にならない
		//----------------------------------------------------------------------------------
		SubmitPreviewCamera();

		// 大きさの目安になる格子
		if (m_isDrawGrid) DrawGrid();
	}

	//======================================================================================
	// 背景の環境設定
	//
	// 黒背景 : 空・フォグ無し。粒や加算の抜けが空の色に紛れないので、単体の見え方を詰めやすい。
	// 今のシーン : nullptr を返し、SceneManager にゲームのシーンのものを流させる
	//======================================================================================
	const Scene::SceneAmbient* EffectEditor::GetPreviewAmbient() const
	{
		if (m_background == EBackground::CurrentScene) return nullptr;
		return m_upPlainAmbient.get();
	}

	void EffectEditor::SubmitPreviewCamera()
	{
		if (!m_upWorld || !m_upCamera) return;
		if (!m_pipelineHandle.IsValid()) return;

		auto* _pGE = MainEngine::Instance().RefGraphicsEngine();
		if (!_pGE) return;

		const auto* _pGraphicsEngine = m_pServices->pMainEngine->RefGraphicsEngine();

		Engine::Graphics::CameraSubmitDesc _desc = {};
		_desc.pWorld			= m_upWorld.get();
		_desc.entity			= PREVIEW_CAMERA_ENTITY;
		_desc.pipelineHandle	= m_pipelineHandle;
		_desc.worldMat			= m_upCamera->GetWorldMatrix();
		_desc.projMat			= m_upCamera->GetProjMatrix();
		_desc.viewportWidth		= static_cast<UINT>(_pGraphicsEngine->GetRenderWidth());
		_desc.viewportHeight	= static_cast<UINT>(_pGraphicsEngine->GetRenderHeight());
		_desc.order				= 0;
		_desc.isMain			= true;

		_pGE->RefCameraPipelines()->SubmitCamera(_desc);
	}

	void EffectEditor::DrawGrid() const
	{
		const Math::Color _lineColor(0.30f, 0.32f, 0.36f, 1.0f);
		const Math::Color _axisColor(0.55f, 0.58f, 0.65f, 1.0f);

		// 積む先はエンジン側の置き場。エディターも他と同じ経路で入れる
		auto* _pDebugDraw = MainEngine::Instance().RefGraphicsEngine()->RefDebugDraw();
		if (!_pDebugDraw) return;

		// 格子はエフェクトの発生位置(原点)に敷く
		const float _half = m_gridSize;
		const int   _count = static_cast<int>(m_gridSize);	// 1mごと

		for (int _i = -_count; _i <= _count; ++_i)
		{
			const float _p = static_cast<float>(_i);
			const Math::Color& _col = (_i == 0) ? _axisColor : _lineColor;

			_pDebugDraw->DrawLine(Math::Vector3(_p, 0.0f, -_half), Math::Vector3(_p, 0.0f, _half), _col);
			_pDebugDraw->DrawLine(Math::Vector3(-_half, 0.0f, _p), Math::Vector3(_half, 0.0f, _p), _col);
		}
	}

	//======================================================================================
	// カメラ : シーンビューのフリーカメラと同じもの
	//======================================================================================
	void EffectEditor::UpdateCamera(float a_dt)
	{
		if (!m_isOpen || !m_upCamera) return;

		m_upCamera->Update(a_dt);
	}

	bool EffectEditor::TryGetCameraOverride(Math::Matrix& a_outWorldMat, Math::Matrix& a_outProjMat) const
	{
		if (!m_isOpen || !m_upCamera) return false;

		a_outWorldMat = m_upCamera->GetWorldMatrix();
		a_outProjMat = m_upCamera->GetProjMatrix();
		return true;
	}

	//======================================================================================
	// UI
	//======================================================================================
	void EffectEditor::OnDrawImGui()
	{
		if (!m_isOpen) return;

		// 開いた最初のフレームだけ ImGui へ知らせる。
		// Open() はインスペクターの描画中(ウィンドウの中)から呼ばれるので、
		// ID スタックが素の状態になるここまで待ってから開く
		if (m_isOpenRequest)
		{
			ImGui::OpenPopup(POPUP_ID);
			m_isOpenRequest = false;
		}

		// 画面の大部分を使う。モーダルなので後ろのパネルは触れない
		const ImVec2 _display = ImGui::GetIO().DisplaySize;
		ImGui::SetNextWindowSize(ImVec2(_display.x * 0.9f, _display.y * 0.9f), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(ImVec2(_display.x * 0.5f, _display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		bool _isWindowOpen = true;
		if (ImGui::BeginPopupModal(POPUP_ID, &_isWindowOpen, ImGuiWindowFlags_NoCollapse))
		{
			DrawToolbar();
			Engine::EditorField::Line();

			// 左 : 見る / 右 : 組む
			const float _paneWidth = (std::min)(m_editPaneWidth, ImGui::GetContentRegionAvail().x * 0.6f);
			const float _viewWidth =
				ImGui::GetContentRegionAvail().x - _paneWidth - ImGui::GetStyle().ItemSpacing.x;

			if (ImGui::BeginChild("EffectEditorViewport", ImVec2((std::max)(64.0f, _viewWidth), 0.0f)))
			{
				DrawViewport();
				DrawInfo();
			}
			ImGui::EndChild();

			Engine::EditorField::SameLine();

			if (ImGui::BeginChild("EffectEditorEditPane", ImVec2(_paneWidth, 0.0f), true))
			{
				DrawEditPane();
			}
			ImGui::EndChild();

			ImGui::EndPopup();
		}

		// × で閉じられた
		if (!_isWindowOpen)
		{
			ImGui::CloseCurrentPopup();
			Close();
		}
	}

	//--------------------------------------------------------------------------------------
	// 上段 : 再生と表示
	//--------------------------------------------------------------------------------------
	void EffectEditor::DrawToolbar()
	{
		const auto _fileName = m_pServices->pAssetDatabase->GetFileNameFromGUID(m_effectGUID);
		Engine::EditorField::Text("%s : %s", (m_mode == EMode::EffectPrefab) ? "Effect Prefab" : "Effect", _fileName.c_str());
		Engine::EditorField::SameLine();
		Engine::EditorField::HelpText("(%s)", m_effectGUID.String().c_str());

		// ---- 再生 ----
		if (ImGui::Button(m_isPlaying ? "Pause" : "Play", ImVec2(80.0f, 0.0f)))
		{
			m_isPlaying = !m_isPlaying;
		}
		Engine::EditorField::SameLine();
		if (ImGui::Button("Restart", ImVec2(80.0f, 0.0f)))
		{
			m_isRestartRequest = true;
			m_isPlaying = true;
		}
		Engine::EditorField::SameLine();
		Engine::EditorField::Field("Loop", m_isLoop);
		Engine::EditorField::Slider("Speed", m_playSpeed, 0.05f, 3.0f, "x%.2f");

		// ---- 表示 ----
		// 背景は1行で置く。
		// 値の欄は行の残りいっぱいに広がるので、後ろへ SameLine で続けると窓の外へ押し出され、
		// 折り返す注意書きが1文字ずつ縦に並んでプレビューが潰れる
		Engine::EditorField::Field("Background", m_background);
		Engine::EditorField::Tooltip("Black : 空・フォグ無しの黒背景 / CurrentScene : 開く前のシーンの環境設定(空・フォグ・光)");
		Engine::EditorField::Field("Grid", m_isDrawGrid);
		if (m_isDrawGrid && !m_pServices->pDebugDraw->GetConfig().drawWire)
		{
			Engine::EditorField::SameLine();
			Engine::EditorField::HelpText("(Option の Draw Debug Wire が off のため出ません)");
		}
		Engine::EditorField::SameLine();
		if (ImGui::Button("Reset Camera"))
		{
			if (m_upCamera) m_upCamera->SetPose(CAMERA_HOME_POS, CAMERA_HOME_YAW, CAMERA_HOME_PITCH);
		}
		Engine::EditorField::Tooltip("右ドラッグ中のみ視点操作 / WASD・EQ移動 / Shift加速");

		// ---- 持ち主の動かし方(エフェクトのときだけ) ----
		if (m_mode == EMode::Effect)
		{
			Engine::EditorField::Field("Owner Rotation (deg)", m_previewRotation, 1.0f);
			Engine::EditorField::Tooltip("エフェクトを付けた持ち主の向き(Pitch / Yaw / Roll)。形状の +Z が付いてくる");
			Engine::EditorField::SameLine();
			if (ImGui::Button("Reset##OwnerRotation"))
			{
				m_previewRotation = { 0.0f, 0.0f, 0.0f };
			}

			Engine::EditorField::Field("Owner Orbit", m_isPreviewOrbit);
			Engine::EditorField::Tooltip("持ち主を原点のまわりで回す。Local の粒は付いてきて、World の粒は置き去りになる");
			if (m_isPreviewOrbit)
			{
				Engine::EditorField::SameLine();
				Engine::EditorField::Field("Radius", m_previewOrbitRadius, 0.05f, 0.0f, 50.0f);
				Engine::EditorField::SameLine();
				Engine::EditorField::Field("Speed (deg/s)", m_previewOrbitSpeed, 1.0f, -720.0f, 720.0f);
			}
		}

		Engine::EditorField::SameLine();
		if (EditorField::DeleteButton("Close"))
		{
			ImGui::CloseCurrentPopup();
			Close();
		}
	}

	//--------------------------------------------------------------------------------------
	// 左 : ゲームと同じレンダーグラフの出力
	//--------------------------------------------------------------------------------------
	void EffectEditor::DrawViewport()
	{
		auto* _pGE = MainEngine::Instance().RefGraphicsEngine();
		if (!_pGE) { Engine::EditorField::HelpText("GraphicsEngine がありません"); return; }

		if (!m_pipelineHandle.IsValid())
		{
			Engine::EditorField::HelpText("描画構成が決まっていません(シーンを一度開いてから開き直してください)");
			return;
		}

		// このプレビューのカメラが描いた絵をそのまま出す。
		// ゲームのシーンと同じ設計図を通っているので、ここで見えているものが本番の見え方
		const auto* _pTex = _pGE->GetCameraPipelines()->GetCameraFinalTexture(m_upWorld.get(), PREVIEW_CAMERA_ENTITY);
		if (!_pTex) { Engine::EditorField::HelpText("出力テクスチャがまだありません"); return; }

		const auto* _pGraphicsEngine = m_pServices->pMainEngine->RefGraphicsEngine();
		const float _aspect = (_pGraphicsEngine->GetRenderHeight() > 0)
			? static_cast<float>(_pGraphicsEngine->GetRenderWidth()) / static_cast<float>(_pGraphicsEngine->GetRenderHeight())
			: 16.0f / 9.0f;

		// 情報欄のぶんを残して、収まる大きさへ合わせる
		const ImVec2 _avail = ImGui::GetContentRegionAvail();
		const float _reserveY = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
		const float _maxH = (std::max)(64.0f, _avail.y - _reserveY);

		ImVec2 _size((std::max)(64.0f, _avail.x), 0.0f);
		_size.y = _size.x / _aspect;
		if (_size.y > _maxH)
		{
			_size.y = _maxH;
			_size.x = _maxH * _aspect;
		}

		auto _gpuHandle = EditorHelper::GetImGuiTexHandle(_pTex->GetImGuiSRV());
		ImGui::Image(static_cast<ImTextureID>(_gpuHandle.ptr), _size);

		// フリーカメラへホバー状態を渡す。
		// 右クリックの開始位置がこの画像の上の時だけ操作を始めるための判定(シーンビューと同じ)
		if (m_upCamera) m_upCamera->SetViewportHovered(ImGui::IsItemHovered());
	}

	void EffectEditor::DrawInfo()
	{
		if (m_mode == EMode::EffectPrefab)
		{
			const auto* _pEffectPrefab = RefEffectPrefab();
			Engine::EditorField::Value("Elapsed", "%.2f / %.2f s", m_prefabElapsed, _pEffectPrefab ? _pEffectPrefab->GetLifeTime() : 0.0f);
			Engine::EditorField::Tooltip("| Alive entities : %d", CountPrefabEntities());
			return;
		}

		EffectRef _ref = FindEffect();
		if (!_ref.IsValid())
		{
			// 生成は遅延なので、開いた直後の1〜2フレームはここを通る
			Engine::EditorField::HelpText("エフェクトを生成中...");
			return;
		}

		const auto* _pEffect = m_pServices->pResourceManager->Get(_ref.pRuntime->effectHandle);
		if (!_pEffect)
		{
			Engine::EditorField::HelpText("アセットを読み込めませんでした");
			return;
		}

		Engine::EditorField::Value("Elapsed", "%.2f s", _ref.pRuntime->instance.elapsed);
		Engine::EditorField::Tooltip("| Particle Parts : %d / Mesh Parts : %d", static_cast<int>(_pEffect->GetParticleParts().size()), static_cast<int>(_pEffect->GetMeshParts().size()));
	}

	//--------------------------------------------------------------------------------------
	// 右 : 組む
	//
	// 中身はアセットインスペクターと同じ関数を呼ぶだけ。
	// ここで独自のUIを書くと、パーツにフィールドを足したときに片方だけ直し忘れる
	//--------------------------------------------------------------------------------------
	void EffectEditor::DrawEditPane()
	{
		if (m_mode == EMode::EffectPrefab)
		{
			DrawEffectPrefabEditPane();
			return;
		}

		auto* _pEffect = RefEffectAsset();
		if (!_pEffect)
		{
			Engine::EditorField::HelpText("エフェクトアセットを読み込めませんでした");
			return;
		}

		if (!ImGui::BeginTabBar("EffectEditorTabs")) return;

		if (ImGui::BeginTabItem("Effect"))
		{
			// 自分自身を開くボタンは要らないので出さない
			Inspector::EffectAssetEdit(*m_pServices, m_effectGUID, _pEffect, false);
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Particle"))
		{
			DrawParticleTab();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	//--------------------------------------------------------------------------------------
	// エフェクトプレハブの編集欄
	//
	// コンポーネントの編集はアセットインスペクターと同じ関数(PrefabComponentsEdit)。
	// 編集はメモリ上のアセットを書き換えるので、次に炊いたときから効く
	//--------------------------------------------------------------------------------------
	void EffectEditor::DrawEffectPrefabEditPane()
	{
		auto* _pEffectPrefab = RefEffectPrefab();
		if (!_pEffectPrefab || !m_upWorld)
		{
			Engine::EditorField::HelpText("エフェクトプレハブを読み込めませんでした");
			return;
		}

		// 保存にはコンポーネント名が要る。プレビューのワールドもゲームと同じ登録なのでそのまま使える
		if (ImGui::Button("Save"))
		{
			auto _path = m_pServices->pAssetDatabase->GetFilePathFromGUID(m_effectGUID);
			_pEffectPrefab->Save(m_upWorld.get(), _path);
			ENGINE_LOG("Save EffectPrefab : %s", _path.c_str());
		}
		Engine::EditorField::Tooltip("変更は次に炊いたときから効く(Restart)");

		float _lifeTime = _pEffectPrefab->GetLifeTime();
		if (Engine::EditorField::Field("Life Time", _lifeTime, 0.05f, Resource::EffectPrefab::MIN_LIFE_TIME, 60.0f))
		{
			_pEffectPrefab->SetLifeTime(_lifeTime);
		}

		Engine::EditorField::Line();

		Inspector::PrefabComponentsEdit(m_upWorld.get(), &_pEffectPrefab->RefPrefab());
	}

	//--------------------------------------------------------------------------------------
	// Particle タブ : パーツが使っている粒そのものを触る
	//
	// 「どこから・どれだけ出すか」は Effect タブ(パーツ)、
	// 「1粒がどう飛んでどう消えるか」はこちら(パーティクルアセット)。
	// 同じ粒を別のエフェクトも使っている場合、ここでの変更はそちらにも効く
	//--------------------------------------------------------------------------------------
	void EffectEditor::DrawParticleTab()
	{
		const auto* _pEffect = RefEffectAsset();
		if (!_pEffect) return;

		const auto& _parts = _pEffect->GetParticleParts();
		if (_parts.empty())
		{
			Engine::EditorField::HelpText("パーティクルパーツがありません");
			Engine::EditorField::HelpText("Effect タブの Add Particle Part から足してください");
			return;
		}

		// どのパーツの粒を触るか
		m_selectedParticlePart = std::clamp(m_selectedParticlePart, 0, static_cast<int>(_parts.size()) - 1);

		const std::string _preview = "Particle " + std::to_string(m_selectedParticlePart);
		if (Engine::EditorField::ComboScope _combo{ "Part", _preview.c_str() })
		{
			for (size_t _i = 0; _i < _parts.size(); ++_i)
			{
				const std::string _name =
					"Particle " + std::to_string(_i) + " : " +
					m_pServices->pAssetDatabase->GetFileNameFromGUID(_parts[_i].particleGUID);

				const bool _isSelected = (static_cast<int>(_i) == m_selectedParticlePart);
				if (ImGui::Selectable(_name.c_str(), _isSelected))
				{
					m_selectedParticlePart = static_cast<int>(_i);
				}
			}
		}

		Engine::EditorField::Line();

		auto* _pParticles = RefSelectedParticleAsset();
		if (!_pParticles)
		{
			Engine::EditorField::HelpText("このパーツにはパーティクルが割り当てられていません");
			Engine::EditorField::HelpText("Effect タブでアセットを選ぶと、ここで中身を触れます");
			return;
		}

		Engine::EditorField::HelpText("この粒を使っている他のエフェクトにも変更が効きます");
		Inspector::ParticleEdit(
			*m_pServices, _parts[m_selectedParticlePart].particleGUID, _pParticles);
	}
}
