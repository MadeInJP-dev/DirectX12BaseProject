#include "EffectEditor.h"

#include "../Helper/EditorHelper.h"

#include "../Panel/InspectorPanel/AssetInspector/ResourceDraw/EffectAssetEdit/EffectAssetEdit.h"
#include "../Panel/InspectorPanel/AssetInspector/ResourceDraw/ParticleEdit/ParticleEdit.h"

#include "Engine/Effect/EffectPlayer.h"
#include "Engine/ECS/World/World.h"
#include "Engine/Scene/SceneAmbient/SceneAmbient.h"
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
		// エフェクトを出す位置。プレビューは常に原点固定にしておく
		const Math::Vector3 EFFECT_ORIGIN = { 0.0f, 0.0f, 0.0f };

		// 画面に対する大きさ。モーダルなので後ろのパネルは触れない
		constexpr float SCREEN_RATIO = 0.9f;
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
		, m_preview(a_pServices)
		, m_popup("Effect Editor")
		, m_upPlainAmbient(std::make_unique<Scene::SceneAmbient>(Scene::SceneAmbient::CreatePlain()))
	{
		m_popup.SetScreenRatio(SCREEN_RATIO);
		m_popup.SetContent([this]() { DrawContent(); });

		// × で閉じられた
		m_popup.SetOnClosed([this]() { Close(); });
	}
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

		// Open() はインスペクターの描画中(ウィンドウの中)から呼ばれるので、
		// 開く要求だけ出しておき、ID スタックが素の状態になる OnDrawImGui で開く
		m_popup.Open();

		// 再生状態は開くたびに頭から
		m_isRestartRequest = false;
		m_selectedParticlePart = 0;

		// ワールド・カメラ・描画構成の用意(描画構成はゲームが直前まで使っていたものを借りる)
		m_preview.BeginOpen();

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

		m_preview.EndOpen();
	}

	void EffectEditor::Release()
	{
		m_preview.Release();
		m_isOpen = false;
	}

	void EffectEditor::ReserveSpawn()
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		if (!_pWorld) return;
		if (m_effectGUID == Core::DEFAULT_GUID) return;

		// エフェクトプレハブ : ゲームと同じ経路で炊く(寿命も付くので、放っておけば全部消える)。
		// 編集中の値は、メモリ上のアセットをそのまま使うので保存しなくても反映される
		if (m_mode == EMode::EffectPrefab)
		{
			m_prefabElapsed = 0.0f;
			if (const auto* _pEffectPrefab = RefEffectPrefab())
			{
				App::Utility::SpawnEffectPrefab(*_pWorld, *_pEffectPrefab, EFFECT_ORIGIN);
			}
			return;
		}

		// 実体化は次の BeginFrame。
		// 出し切っても消えないようにしておく(何度も再生し直したいので寿命はこちらが握る)。
		// 発生位置は常に原点。カメラは自由に動かせるので、見る位置と出す位置は分けておく
		App::Utility::ReserveSpawnEffectAt(*_pWorld, m_effectGUID, EFFECT_ORIGIN, false);
	}

	void EffectEditor::DestroyEffectEntity()
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		if (!_pWorld) return;

		// プレビュー用ワールドにはエフェクトしか居ないので、見つけたものを全部片付ける。
		// エフェクトプレハブで出したもの(破片など、エフェクトを持たないものもある)は
		// 必ず寿命を持つので、そちらでも拾う
		std::vector<ECS::Entity> _targets = {};
		_pWorld->ForEach<App::Component::EffectAssetComponent>(
			[&](ECS::Chunk* a_pChunk, uint32_t a_count, App::Component::EffectAssetComponent*)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					_targets.push_back(a_pChunk->entityData[_i]);
				}
			}
		);
		_pWorld->ForEach<App::Component::LifeTimeComponent>(
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

		m_preview.DestroyEntities(_targets);
	}

	EffectEditor::EffectRef EffectEditor::FindEffect() const
	{
		EffectRef _ref = {};
		ECS::World* _pWorld = m_preview.RefWorld();
		if (!_pWorld) return _ref;

		_pWorld->ForEach<App::Component::EffectRuntimeComponent, App::Component::EffectPlayRequestComponent>(
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
		ECS::World* _pWorld = m_preview.RefWorld();
		if (!_pWorld) return 0;

		// エフェクトプレハブで出したものは全部寿命を持つ(SpawnEffectPrefab が付ける)
		int _count = 0;
		_pWorld->ForEach<App::Component::LifeTimeComponent>(
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
		if (!m_isOpen || !m_preview.RefWorld()) return;

		// 止めているあいだは時間を進めない。
		// エフェクトの経過時間もパーティクルの発生もここの dt で決まるので、
		// 0 を流すだけで「その瞬間で固まる」
		const float _dt = m_preview.GetDeltaTime(a_dt);

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
		// プレビューにコライダーが居なければ、当たり判定の空間は空のまま素通りするだけ
		m_preview.RunUpdate(_dt);
	}

	//======================================================================================
	// プレビューの持ち主の位置と向き
	//
	// 回転は Pitch(X) / Yaw(Y) / Roll(Z) の度。回すと形状の +Z(噴き出す向き)が付いてくる。
	// 円運動は止めている(dt = 0)あいだは進まない
	//======================================================================================
	void EffectEditor::ApplyPreviewTransform(ECS::Entity a_entity, float a_dt)
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		if (!_pWorld) return;
		if (!_pWorld->HasComponent<App::Component::LocalTransformComponent>(a_entity)) return;

		auto* _pTransform = _pWorld->RefData<App::Component::LocalTransformComponent>(a_entity);
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
		if (!m_isOpen) return;

		// 背景(空・フォグ・光)は SceneManager がこの前に流し込み済み(GetPreviewAmbient で選んだもの)
		m_preview.RunDraw();
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

	//======================================================================================
	// カメラ : シーンビューのフリーカメラと同じもの
	//======================================================================================
	void EffectEditor::UpdateCamera(float a_dt)
	{
		if (!m_isOpen) return;

		m_preview.UpdateCamera(a_dt);
	}

	bool EffectEditor::TryGetCameraOverride(Math::Matrix& a_outWorldMat, Math::Matrix& a_outProjMat) const
	{
		if (!m_isOpen) return false;

		return m_preview.TryGetCameraOverride(a_outWorldMat, a_outProjMat);
	}

	//======================================================================================
	// UI
	//======================================================================================
	void EffectEditor::OnDrawImGui()
	{
		if (!m_isOpen) return;

		m_popup.Draw();
	}

	void EffectEditor::DrawContent()
	{
		DrawToolbar();
		Engine::EditorField::Line();

		// 左 : 見る / 右 : 組む
		const float _paneWidth = (std::min)(m_editPaneWidth, ImGui::GetContentRegionAvail().x * 0.6f);
		const float _viewWidth =
			ImGui::GetContentRegionAvail().x - _paneWidth - ImGui::GetStyle().ItemSpacing.x;

		if (ImGui::BeginChild("EffectEditorViewport", ImVec2((std::max)(64.0f, _viewWidth), 0.0f)))
		{
			// ゲームと同じレンダーグラフの出力。情報欄のぶん(3行)を下に残す
			ImVec2 _imageMin = {};
			ImVec2 _imageSize = {};
			m_preview.DrawViewport(3.0f, _imageMin, _imageSize);
			DrawInfo();
		}
		ImGui::EndChild();

		Engine::EditorField::SameLine();

		if (ImGui::BeginChild("EffectEditorEditPane", ImVec2(_paneWidth, 0.0f), true))
		{
			DrawEditPane();
		}
		ImGui::EndChild();
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
		m_preview.DrawPlayButton();
		Engine::EditorField::SameLine();
		if (Engine::EditorField::Button("Restart", Math::Vector2(80.0f, 0.0f)))
		{
			m_isRestartRequest = true;
			m_preview.SetPlaying(true);
		}
		Engine::EditorField::SameLine();
		Engine::EditorField::Field("Loop", m_isLoop);
		Engine::EditorField::SameLine();
		m_preview.DrawSpeedSlider();

		// ---- 表示 ----
		// 背景は1行で置く。
		// 値の欄は行の残りいっぱいに広がるので、後ろへ SameLine で続けると窓の外へ押し出され、
		// 折り返す注意書きが1文字ずつ縦に並んでプレビューが潰れる
		Engine::EditorField::Field("Background", m_background);
		Engine::EditorField::Tooltip("Black : 空・フォグ無しの黒背景 / CurrentScene : 開く前のシーンの環境設定(空・フォグ・光)");
		m_preview.DrawGridToggle();
		Engine::EditorField::SameLine();
		m_preview.DrawResetCameraButton();
		m_preview.DrawWireOffNote(true);

		// ---- 持ち主の動かし方(エフェクトのときだけ) ----
		if (m_mode == EMode::Effect)
		{
			Engine::EditorField::Field("Owner Rotation (deg)", m_previewRotation, 1.0f);
			Engine::EditorField::Tooltip("エフェクトを付けた持ち主の向き(Pitch / Yaw / Roll)。形状の +Z が付いてくる");
			Engine::EditorField::SameLine();
			if (Engine::EditorField::Button("Reset##OwnerRotation"))
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
			m_popup.Close();
			Close();
		}
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
		ECS::World* _pWorld = m_preview.RefWorld();
		if (!_pEffectPrefab || !_pWorld)
		{
			Engine::EditorField::HelpText("エフェクトプレハブを読み込めませんでした");
			return;
		}

		// 保存にはコンポーネント名が要る。プレビューのワールドもゲームと同じ登録なのでそのまま使える
		if (ImGui::Button("Save"))
		{
			auto _path = m_pServices->pAssetDatabase->GetFilePathFromGUID(m_effectGUID);
			_pEffectPrefab->Save(_pWorld, _path);
			ENGINE_LOG("Save EffectPrefab : %s", _path.c_str());
		}
		Engine::EditorField::Tooltip("変更は次に炊いたときから効く(Restart)");

		float _lifeTime = _pEffectPrefab->GetLifeTime();
		if (Engine::EditorField::Field("Life Time", _lifeTime, 0.05f, Resource::EffectPrefab::MIN_LIFE_TIME, 60.0f))
		{
			_pEffectPrefab->SetLifeTime(_lifeTime);
		}

		Engine::EditorField::Line();

		Inspector::PrefabComponentsEdit(_pWorld, &_pEffectPrefab->RefPrefab());
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
