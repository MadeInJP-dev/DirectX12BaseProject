#include "PrefabEditor.h"

#include "../Helper/EditorHelper.h"
#include "../EditorCamera/EditorCamera.h"

#include "../Panel/InspectorPanel/AssetInspector/ResourceDraw/ResourceDraw.h"

#include "Engine/ECS/World/World.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Prefab/Prefab.h"

#include "Engine/ECS/Component/GUIDComponent.h"
#include "Application/Components/Core/NameComponent.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Utility/PrefabSpawnHelper.h"

namespace Editor
{
	namespace
	{
		// ポップアップの名前(ID)。見出し(名前と未保存の印)が変わっても同じ窓として扱う
		constexpr const char* POPUP_NAME = "PrefabEditor";

		// 画面に対する大きさ。モーダルなので後ろのパネルは触れない
		constexpr float SCREEN_RATIO = 0.9f;

		// プレハブを出す位置。プレビューは常に原点
		const Math::Vector3 PREFAB_ORIGIN = { 0.0f, 0.0f, 0.0f };

		// 選んでいるノードに描く軸の長さ(m)
		constexpr float NODE_AXIS_LENGTH = 0.5f;

		// FNV-1a でバイト列を混ぜる
		uint64_t HashBytes(uint64_t a_hash, const void* a_pData, size_t a_size)
		{
			constexpr uint64_t FNV_PRIME = 1099511628211ull;

			const auto* _pBytes = static_cast<const uint8_t*>(a_pData);
			for (size_t _i = 0; _i < a_size; ++_i)
			{
				a_hash ^= _pBytes[_i];
				a_hash *= FNV_PRIME;
			}
			return a_hash;
		}

		template<typename T>
		uint64_t HashValue(uint64_t a_hash, const T& a_value)
		{
			return HashBytes(a_hash, &a_value, sizeof(a_value));
		}

		// ノードの位置(-1 = ルート)を、ルート先頭の並び(m_nodeEntityVec など)の添え字へ直す
		size_t ToFlatIndex(int a_nodeIndex)
		{
			return static_cast<size_t>(a_nodeIndex + 1);
		}
	}

	PrefabEditor::PrefabEditor(ECS::EngineServices* a_pServices)
		: m_pServices(a_pServices)
		, m_preview(a_pServices)
		, m_popup(POPUP_NAME)
		, m_closeConfirmPopup("Unsaved Prefab")
	{
		m_popup.SetScreenRatio(SCREEN_RATIO);
		m_popup.SetContent([this]() { DrawContent(); });

		// × で閉じられた(未保存のときは × を出していないので、ここは保存済み)
		m_popup.SetOnClosed([this]() { Close(); });

		m_closeConfirmPopup.SetMessage("保存していない変更があります", "Discard はファイルから読み直して、変更を捨てます");
		m_closeConfirmPopup.AddChoice("Save", [this]() { Save(); m_isCloseRequest = true; });
		m_closeConfirmPopup.AddChoice("Discard", [this]() { Revert(); m_isCloseRequest = true; }, EConfirmStyle::Danger);
	}
	PrefabEditor::~PrefabEditor() = default;

	//======================================================================================
	// 開く / 閉じる
	//======================================================================================
	void PrefabEditor::Open(const Core::GUID& a_prefabGUID)
	{
		if (a_prefabGUID == Core::DEFAULT_GUID) return;

		// 開き直しでも中身は作り直す(別のプレハブを選んだ場合があるため)
		DestroyAllEntities();

		m_prefabGUID = a_prefabGUID;
		m_prefabHandle =
			m_pServices->pResourceManager->LoadImmediate<Resource::Prefab>(a_prefabGUID);

		m_isOpen = true;
		m_isCloseRequest = false;

		// Open() はインスペクターの描画中から呼ばれるので、開く要求だけ出しておき
		// ID スタックが素の状態になる OnDrawImGui で開く
		m_popup.Open();

		m_selectedNode = Resource::Prefab::ROOT_NODE;

		// 開いた時点の中身を「保存済み」とみなす。
		// インスペクターで触って保存していない分も、ここからは未保存の印に出ない
		m_savedHash = ComputePrefabHash();

		// ワールド・カメラ・描画構成の用意(描画構成はゲームが直前まで使っていたものを借りる)
		m_preview.BeginOpen();

		// 実体は次の更新(ゲームのシーンの代わりに回るところ)で出す
		m_isRespawnRequest = true;
	}

	void PrefabEditor::Close()
	{
		if (!m_isOpen) return;

		DestroyAllEntities();

		m_isOpen = false;
		m_isCloseRequest = false;
		m_prefabGUID = Core::DEFAULT_GUID;
		m_prefabHandle = {};

		m_preview.EndOpen();
	}

	void PrefabEditor::RequestClose()
	{
		if (IsModified())
		{
			m_closeConfirmPopup.Open();
			return;
		}

		m_isCloseRequest = true;
	}

	void PrefabEditor::Release()
	{
		m_preview.Release();
		m_nodeEntityVec.clear();
		m_isOpen = false;
	}

	//--------------------------------------------------------------------------------------
	// 実体を出す
	//
	// ゲームが出すときと同じ材料作り(BuildSpawnInstanceData)を通すので、
	// 保存しない印や位置の上書きもゲームと揃う。
	// 生成は即時(ヒエラルキーパネルと同じ)。ノードごとのIDを受け取って、
	// 選んでいるノードの軸やギズモをその実体に合わせるため
	//--------------------------------------------------------------------------------------
	void PrefabEditor::SpawnInstance()
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		m_nodeEntityVec.clear();
		m_spawnedHash = ComputePrefabHash();

		if (!_pWorld) return;

		const auto* _pPrefab = RefPrefab();
		if (!_pPrefab) return;

		App::Utility::SpawnParams _params = {};
		_params.pos = PREFAB_ORIGIN;

		std::vector<Resource::PrefabInstanceData> _instanceVec = {};
		if (!App::Utility::BuildSpawnInstanceData(*_pWorld, *_pPrefab, _params, _instanceVec)) return;

		App::Utility::CreateInstanceNow(*_pWorld, _instanceVec, &m_nodeEntityVec);
	}

	void PrefabEditor::DestroyAllEntities()
	{
		// プレビュー用ワールドには、出したプレハブと、それが動いて出したものしか居ない。
		// 後者(弾・エフェクトなど)も出し直しのたびに積み上がらないよう、居るものを全部消す
		m_nodeEntityVec.clear();
		m_preview.DestroyAllEntities();
	}


	ECS::Entity PrefabEditor::GetNodeEntity(int a_nodeIndex) const
	{
		const size_t _flatIndex = ToFlatIndex(a_nodeIndex);
		if (_flatIndex >= m_nodeEntityVec.size()) return ECS::Limits::INVALID_ENTITY;

		return m_nodeEntityVec[_flatIndex];
	}

	//======================================================================================
	// 編集対象
	//======================================================================================
	Resource::Prefab* PrefabEditor::RefPrefab() const
	{
		return m_pServices->pResourceManager->Ref(m_prefabHandle);
	}

	//--------------------------------------------------------------------------------------
	// 中身から作る値
	//
	// 「出し直すか」「未保存か」の判定に使う。
	// データの並び(unordered_map)の順に頼らないよう、型IDの順に混ぜる
	//--------------------------------------------------------------------------------------
	uint64_t PrefabEditor::ComputePrefabHash() const
	{
		constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;

		const auto* _pPrefab = RefPrefab();
		if (!_pPrefab) return 0;

		uint64_t _hash = FNV_OFFSET;
		_hash = HashValue(_hash, _pPrefab->GetNodeCount());

		for (int _node = Resource::Prefab::ROOT_NODE; _node < static_cast<int>(_pPrefab->GetChildren().size()); ++_node)
		{
			if (_node != Resource::Prefab::ROOT_NODE)
			{
				_hash = HashValue(_hash, _pPrefab->GetChildren()[static_cast<size_t>(_node)].parentIndex);
			}

			const ECS::Signature& _sig = _pPrefab->GetSignature(_node);
			const auto& _dataMap = _pPrefab->GetDataMap(_node);

			for (size_t _typeID = 0; _typeID < _sig.size(); ++_typeID)
			{
				if (!_sig.test(_typeID)) continue;

				_hash = HashValue(_hash, _typeID);

				auto _it = _dataMap.find(static_cast<ECS::ComponentTypeID>(_typeID));
				if (_it == _dataMap.end() || _it->second.empty()) continue;

				_hash = HashBytes(_hash, _it->second.data(), _it->second.size());
			}
		}

		return _hash;
	}

	bool PrefabEditor::IsModified() const
	{
		return RefPrefab() && ComputePrefabHash() != m_savedHash;
	}

	void PrefabEditor::Save()
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		auto* _pPrefab = RefPrefab();
		if (!_pPrefab || !_pWorld) return;

		// 保存にはコンポーネント名が要る。プレビューのワールドもゲームと同じ登録なのでそのまま使える
		const auto _path = m_pServices->pAssetDatabase->GetFilePathFromGUID(m_prefabGUID);
		_pPrefab->Save(_pWorld, _path);
		m_savedHash = ComputePrefabHash();

		ENGINE_LOG("Save Prefab : %s", _path.c_str());
	}

	void PrefabEditor::Revert()
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		auto* _pPrefab = RefPrefab();
		if (!_pPrefab || !_pWorld) return;

		const auto _path = m_pServices->pAssetDatabase->GetFilePathFromGUID(m_prefabGUID);
		_pPrefab->Load(_pWorld, _path);
		m_savedHash = ComputePrefabHash();

		// 子が減っていることがあるので、選択は必ず在るところへ戻す
		if (!_pPrefab->IsValidNode(m_selectedNode)) m_selectedNode = Resource::Prefab::ROOT_NODE;

		m_isRespawnRequest = true;

		ENGINE_LOG("Revert Prefab : %s", _path.c_str());
	}

	//--------------------------------------------------------------------------------------
	// 子ノードを足す
	//
	// シーンのヒエラルキーで空のエンティティを作ったときと同じ構成に、親子を足したもの。
	// 親子は保存時GUID(鍵)で持つので、親の鍵を取ってから(無ければ振ってから)指す
	//--------------------------------------------------------------------------------------
	void PrefabEditor::AddChildNode(int a_parentIndex)
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		auto* _pPrefab = RefPrefab();
		if (!_pPrefab || !_pWorld) return;
		if (!_pPrefab->IsValidNode(a_parentIndex)) return;

		ECS::World& _world = *_pWorld;

		// 親の深さ(ルートは 0)
		UINT _parentDepth = 0;
		const ECS::ComponentTypeID _hierarchyTypeID = _world.GetCompTypeID<App::Component::HierarchyComponent>();
		if (a_parentIndex != Resource::Prefab::ROOT_NODE)
		{
			if (const uint8_t* _pParentHierarchy = _pPrefab->RefData(_hierarchyTypeID, a_parentIndex))
			{
				App::Component::HierarchyComponent _parentHierarchy = {};
				std::memcpy(&_parentHierarchy, _pParentHierarchy, sizeof(_parentHierarchy));
				_parentDepth = _parentHierarchy.depth;
			}
		}

		const Core::GUID _parentGUID = _pPrefab->RequestNodeGUID(a_parentIndex);

		Resource::PrefabChild _child = {};
		_child.savedGUID.Create();
		_child.parentIndex = a_parentIndex;

		const Core::GUID _childGUID = _child.savedGUID;
		_pPrefab->AddChild(std::move(_child));

		const int _childIndex = static_cast<int>(_pPrefab->GetChildren().size()) - 1;

		// ---- 既定の構成 ----
		const ECS::ComponentTypeID _ltTypeID   = _world.GetCompTypeID<App::Component::LocalTransformComponent>();
		const ECS::ComponentTypeID _wmTypeID   = _world.GetCompTypeID<App::Component::WorldMatrixComponent>();
		const ECS::ComponentTypeID _nameTypeID = _world.GetCompTypeID<App::Component::NameComponent>();
		const ECS::ComponentTypeID _guidTypeID = _world.GetCompTypeID<Engine::ECS::GUIDComponent>();

		_pPrefab->AddComponentDefault(&_world, _ltTypeID, _childIndex);
		_pPrefab->AddComponentDefault(&_world, _wmTypeID, _childIndex);
		_pPrefab->AddComponentDefault(&_world, _nameTypeID, _childIndex);
		_pPrefab->AddComponentDefault(&_world, _hierarchyTypeID, _childIndex);
		_pPrefab->AddComponentDefault(&_world, _guidTypeID, _childIndex);

		if (uint8_t* _pName = _pPrefab->RefData(_nameTypeID, _childIndex))
		{
			App::Component::NameComponent _name = {};
			std::snprintf(_name.name, sizeof(_name.name), "Child_%02d", _childIndex);
			std::memcpy(_pName, &_name, sizeof(_name));
		}

		if (uint8_t* _pHierarchy = _pPrefab->RefData(_hierarchyTypeID, _childIndex))
		{
			App::Component::HierarchyComponent _hierarchy = {};
			_hierarchy.parentGUID = _parentGUID;
			_hierarchy.depth = _parentDepth + 1;
			std::memcpy(_pHierarchy, &_hierarchy, sizeof(_hierarchy));
		}

		// 子の GUID は鍵と同じものを入れておく(Create Prefab で作った子と揃える)。
		// 実体化のたびに振り直されるので、ここに入れた値がシーンへ出ることはない
		if (uint8_t* _pGUID = _pPrefab->RefData(_guidTypeID, _childIndex))
		{
			Engine::ECS::GUIDComponent _guidComp = {};
			_guidComp.guid = _childGUID;
			std::memcpy(_pGUID, &_guidComp, sizeof(_guidComp));
		}

		m_selectedNode = _childIndex;
	}

	void PrefabEditor::RemoveNode(int a_nodeIndex)
	{
		auto* _pPrefab = RefPrefab();
		if (!_pPrefab) return;
		if (a_nodeIndex == Resource::Prefab::ROOT_NODE || !_pPrefab->IsValidNode(a_nodeIndex)) return;

		// 選択は親へ移す。親は自分より前に並んでいるので、取り除いても添え字は変わらない
		const int _parentIndex = _pPrefab->GetChildren()[static_cast<size_t>(a_nodeIndex)].parentIndex;

		const size_t _removeCount = _pPrefab->RemoveChildTree(a_nodeIndex);
		m_selectedNode = _parentIndex;

		ENGINE_LOG("[PrefabEditor] ノードを取り除きました(%d 個)", static_cast<int>(_removeCount));
	}

	std::string PrefabEditor::GetNodeName(int a_nodeIndex) const
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		const std::string _fallback = (a_nodeIndex == Resource::Prefab::ROOT_NODE)
			? std::string("Root")
			: ("Child " + std::to_string(a_nodeIndex));

		const auto* _pPrefab = RefPrefab();
		if (!_pPrefab || !_pWorld) return _fallback;

		const auto& _dataMap = _pPrefab->GetDataMap(a_nodeIndex);
		auto _it = _dataMap.find(_pWorld->GetCompTypeID<App::Component::NameComponent>());
		if (_it == _dataMap.end() || _it->second.size() < sizeof(App::Component::NameComponent)) return _fallback;

		App::Component::NameComponent _name = {};
		std::memcpy(&_name, _it->second.data(), sizeof(_name));
		_name.name[sizeof(_name.name) - 1] = '\0';

		if (_name.name[0] == '\0') return _fallback;
		return _name.name;
	}

	//======================================================================================
	// 更新 : ゲームのシーンの代わりに、このワールドだけを回す
	//======================================================================================
	void PrefabEditor::UpdateScene(float a_dt)
	{
		if (!m_isOpen || !m_preview.RefWorld()) return;

		//------------------------------------------------------------------
		// 出し直し
		//
		// 値を触っている最中(ドラッグ中・入力中・ギズモ操作中)は出し直さない。
		// 1フレームごとに実体を作り直すことになり、物理のボディやモデルの
		// 確保が毎フレーム走るため。手を離した時点の値で出し直す
		//------------------------------------------------------------------
		const bool _isEditing = ImGui::IsAnyItemActive() || ImGuizmo::IsUsingAny();
		const bool _isChanged = m_isAutoRespawn && !_isEditing && (ComputePrefabHash() != m_spawnedHash);

		if (m_isRespawnRequest || _isChanged)
		{
			m_isRespawnRequest = false;
			DestroyAllEntities();
			SpawnInstance();
		}

		// 止めているあいだは時間を進めない(0 を流すとその瞬間で固まる)
		m_preview.RunUpdate(m_preview.GetDeltaTime(a_dt));
	}

	//======================================================================================
	// 描画 : ゲームのシーンの代わりに、このワールドの命令をレンダーグラフへ流す
	//======================================================================================
	void PrefabEditor::DrawScene()
	{
		if (!m_isOpen) return;

		// エフェクトエディターと違って空は消さない。
		// モデルの見え方(映り込み・環境光)は空込みで決まるので、本番と同じ条件で見る
		m_preview.RunDraw();

		if (m_isDrawAxis) DrawSelectedNodeAxis();
	}


	void PrefabEditor::DrawSelectedNodeAxis() const
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		if (!_pWorld) return;

		const ECS::Entity _entity = GetNodeEntity(m_selectedNode);
		if (_entity == ECS::Limits::INVALID_ENTITY) return;
		if (!_pWorld->IsAliveEntity(_entity)) return;
		if (!_pWorld->HasComponent<App::Component::WorldMatrixComponent>(_entity)) return;

		auto* _pDebugDraw = m_pServices->pDebugDraw;
		if (!_pDebugDraw) return;

		const Math::Matrix& _mat = _pWorld->RefData<App::Component::WorldMatrixComponent>(_entity)->worldMat;
		const Math::Vector3 _pos = _mat.Translation();

		// 向きだけを見るので、スケールの影響を落としてから一定の長さで描く
		auto _axis = [&](const Math::Vector3& a_unit)
			{
				Math::Vector3 _dir = Math::Vector3::TransformNormal(a_unit, _mat);
				if (_dir.LengthSquared() > 1e-8f) _dir.Normalize();
				return _pos + _dir * NODE_AXIS_LENGTH;
			};

		_pDebugDraw->DrawLine(_pos, _axis(Math::Vector3::Right()), Math::Color(1.0f, 0.25f, 0.25f, 1.0f));
		_pDebugDraw->DrawLine(_pos, _axis(Math::Vector3::Up()), Math::Color(0.25f, 1.0f, 0.25f, 1.0f));
		_pDebugDraw->DrawLine(_pos, _axis(Math::Vector3::Forward()), Math::Color(0.35f, 0.55f, 1.0f, 1.0f));
	}

	//======================================================================================
	// カメラ : シーンビューのフリーカメラと同じもの
	//======================================================================================
	void PrefabEditor::UpdateCamera(float a_dt)
	{
		if (!m_isOpen) return;

		m_preview.UpdateCamera(a_dt);
	}

	bool PrefabEditor::TryGetCameraOverride(Math::Matrix& a_outWorldMat, Math::Matrix& a_outProjMat) const
	{
		if (!m_isOpen) return false;

		return m_preview.TryGetCameraOverride(a_outWorldMat, a_outProjMat);
	}

	//======================================================================================
	// UI
	//======================================================================================
	void PrefabEditor::OnDrawImGui()
	{
		if (!m_isOpen) return;

		// 未保存なら名前に印を付け、× は出さない。
		// × は押した瞬間に ImGui が閉じてしまい、確かめる間が無いため(閉じるのは Close ボタンから)
		const bool _isModified = IsModified();
		m_popup.SetTitle(
			"Prefab Editor : " + m_pServices->pAssetDatabase->GetFileNameFromGUID(m_prefabGUID) + (_isModified ? " *" : ""));
		m_popup.SetClosable(!_isModified);

		m_popup.Draw();
	}

	void PrefabEditor::DrawContent()
	{
		DrawToolbar();
		Engine::EditorField::Line();

		// 左 : ノード / 中 : 見る / 右 : 組む
		const float _spacing = ImGui::GetStyle().ItemSpacing.x;
		const float _availX = ImGui::GetContentRegionAvail().x;
		const float _nodeWidth = (std::min)(m_nodePaneWidth, _availX * 0.25f);
		const float _editWidth = (std::min)(m_editPaneWidth, _availX * 0.45f);
		const float _viewWidth = _availX - _nodeWidth - _editWidth - _spacing * 2.0f;

		if (ImGui::BeginChild("PrefabEditorNodePane", ImVec2(_nodeWidth, 0.0f), true))
		{
			DrawNodePane();
		}
		ImGui::EndChild();

		Engine::EditorField::SameLine();

		if (ImGui::BeginChild("PrefabEditorViewport", ImVec2((std::max)(64.0f, _viewWidth), 0.0f)))
		{
			DrawViewport();
			DrawInfo();
		}
		ImGui::EndChild();

		Engine::EditorField::SameLine();

		if (ImGui::BeginChild("PrefabEditorEditPane", ImVec2(_editWidth, 0.0f), true))
		{
			DrawEditPane();
		}
		ImGui::EndChild();

		// 未保存のまま閉じようとしたときの確認(このモーダルの中に重ねて出す)
		m_closeConfirmPopup.Draw();

		// 確認が済んだ・保存済みだったので閉じる
		if (m_isCloseRequest)
		{
			m_popup.Close();
			Close();
		}
	}

	//--------------------------------------------------------------------------------------
	// 上段 : 保存・再生・表示
	//--------------------------------------------------------------------------------------
	void PrefabEditor::DrawToolbar()
	{
		Engine::EditorField::Text("Prefab : %s", m_pServices->pAssetDatabase->GetFileNameFromGUID(m_prefabGUID).c_str());
		Engine::EditorField::SameLine();
		Engine::EditorField::HelpText("(%s)", m_prefabGUID.String().c_str());

		// ---- 保存 ----
		const bool _isModified = IsModified();
		if (Engine::EditorField::Button("Save", Math::Vector2(80.0f, 0.0f)))
		{
			Save();
		}
		Engine::EditorField::SameLine();
		ImGui::BeginDisabled(!_isModified);
		if (Engine::EditorField::Button("Revert", Math::Vector2(80.0f, 0.0f)))
		{
			Revert();
		}
		ImGui::EndDisabled();
		Engine::EditorField::Tooltip("ファイルから読み直して、保存していない変更を捨てる");
		Engine::EditorField::SameLine();
		if (_isModified)
		{
			Engine::EditorField::WarningText("未保存の変更があります");
		}
		else
		{
			Engine::EditorField::HelpText("保存済み");
		}

		// ---- 再生 ----
		m_preview.DrawPlayButton();
		Engine::EditorField::SameLine();
		if (Engine::EditorField::Button("Respawn", Math::Vector2(80.0f, 0.0f)))
		{
			m_isRespawnRequest = true;
		}
		Engine::EditorField::Tooltip("出し直す(動いて位置や状態が変わったものを最初の姿へ戻す)");
		Engine::EditorField::SameLine();
		Engine::EditorField::Field("Auto Respawn", m_isAutoRespawn);
		Engine::EditorField::Tooltip("中身を変えたら(欄から手を離した時点で)自動で出し直す");
		Engine::EditorField::SameLine();
		m_preview.DrawSpeedSlider();

		// ---- 表示 ----
		m_preview.DrawGridToggle();
		Engine::EditorField::SameLine();
		Engine::EditorField::Field("Node Axis", m_isDrawAxis);
		Engine::EditorField::Tooltip("選んでいるノードの位置に軸を描く(X 赤 / Y 緑 / Z 青)");
		Engine::EditorField::SameLine();
		Engine::EditorField::Field("Gizmo", m_isUseGizmo);
		Engine::EditorField::Tooltip("選んでいる子ノードを移動ギズモで動かす(Ctrl でスナップ)");
		Engine::EditorField::SameLine();
		m_preview.DrawResetCameraButton();
		m_preview.DrawWireOffNote(true);

		Engine::EditorField::SameLine();
		if (Engine::EditorField::DeleteButton("Close"))
		{
			RequestClose();
		}
	}

	//--------------------------------------------------------------------------------------
	// 左 : ノードの木
	//--------------------------------------------------------------------------------------
	void PrefabEditor::DrawNodePane()
	{
		auto* _pPrefab = RefPrefab();
		if (!_pPrefab)
		{
			Engine::EditorField::HelpText("プレハブを読み込めませんでした");
			return;
		}

		// 選択は在るところへ寄せておく(Revert やインスペクター側の変更で子が減ることがある)
		if (!_pPrefab->IsValidNode(m_selectedNode)) m_selectedNode = Resource::Prefab::ROOT_NODE;

		// ---- 操作 ----
		if (Engine::EditorField::CreateButton("Add Child"))
		{
			AddChildNode(m_selectedNode);
		}
		Engine::EditorField::Tooltip("選んでいるノードの下に子を足す");

		Engine::EditorField::SameLine();
		ImGui::BeginDisabled(m_selectedNode == Resource::Prefab::ROOT_NODE);
		if (Engine::EditorField::DeleteButton("Remove"))
		{
			RemoveNode(m_selectedNode);
		}
		ImGui::EndDisabled();
		Engine::EditorField::Tooltip("選んでいるノードを子孫ごと取り除く(ルートは取り除けない)");

		Engine::EditorField::Line();

		// 親 → 子の対応表(先頭がルートの子、以降は子の添え字 + 1 の子)
		const auto& _childVec = _pPrefab->GetChildren();
		std::vector<std::vector<int>> _childIndexMap(_pPrefab->GetNodeCount());
		for (size_t _i = 0; _i < _childVec.size(); ++_i)
		{
			const int _parent = _childVec[_i].parentIndex;
			if (!_pPrefab->IsValidNode(_parent)) continue;

			_childIndexMap[ToFlatIndex(_parent)].push_back(static_cast<int>(_i));
		}

		DrawNodeTreeItem(Resource::Prefab::ROOT_NODE, _childIndexMap);
	}

	void PrefabEditor::DrawNodeTreeItem(int a_nodeIndex, const std::vector<std::vector<int>>& a_childIndexMap)
	{
		const std::vector<int>& _children = a_childIndexMap[ToFlatIndex(a_nodeIndex)];

		ImGuiTreeNodeFlags _flags =
			ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth;
		if (_children.empty()) _flags |= ImGuiTreeNodeFlags_Leaf;
		if (a_nodeIndex == m_selectedNode) _flags |= ImGuiTreeNodeFlags_Selected;

		// 実体を作れなかったノード(コンポーネントが空など)は分かるようにしておく
		const bool _isSpawned = GetNodeEntity(a_nodeIndex) != ECS::Limits::INVALID_ENTITY;
		const std::string _label = GetNodeName(a_nodeIndex) + (_isSpawned ? "" : "  (not spawned)");

		ImGui::PushID(a_nodeIndex);
		const bool _isNodeOpen = ImGui::TreeNodeEx("##Node", _flags, "%s", _label.c_str());

		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			m_selectedNode = a_nodeIndex;
		}

		// 右クリックからも足し引きできるようにする
		if (ImGui::BeginPopupContextItem("NodeContext"))
		{
			m_selectedNode = a_nodeIndex;

			if (ImGui::MenuItem("Add Child"))
			{
				AddChildNode(a_nodeIndex);
			}
			if (ImGui::MenuItem("Remove", nullptr, false, a_nodeIndex != Resource::Prefab::ROOT_NODE))
			{
				RemoveNode(a_nodeIndex);
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();

		if (_isNodeOpen)
		{
			// 足し引きで木が変わっても、このフレームは組み立てたときの表で描き切る
			for (const int _child : _children)
			{
				DrawNodeTreeItem(_child, a_childIndexMap);
			}
			ImGui::TreePop();
		}
	}

	//--------------------------------------------------------------------------------------
	// 中 : ゲームと同じレンダーグラフの出力
	//--------------------------------------------------------------------------------------
	void PrefabEditor::DrawViewport()
	{
		// 情報欄のぶん(3行)を下に残す
		ImVec2 _imageMin = {};
		ImVec2 _imageSize = {};
		if (!m_preview.DrawViewport(3.0f, _imageMin, _imageSize)) return;

		DrawNodeGizmo(_imageMin, _imageSize);
	}

	//--------------------------------------------------------------------------------------
	// 選んでいる子ノードの移動ギズモ
	//
	// 見えている実体とプレハブの中身の両方へ同じ量を書く。
	// 実体へも書くのは、操作中は出し直さない(手を離すまで待つ)ので、
	// そのままだと離すまで見た目が付いてこないため。
	// 両方を揃えて動かすので、離したあとの出し直しも要らない(出した時点の値を合わせて進める)
	//--------------------------------------------------------------------------------------
	void PrefabEditor::DrawNodeGizmo(const ImVec2& a_imageMin, const ImVec2& a_imageSize)
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		EditorCamera* _pCamera = m_preview.RefCamera();
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(a_imageMin.x, a_imageMin.y, a_imageSize.x, a_imageSize.y);

		if (!m_isUseGizmo || !_pWorld || !_pCamera) return;
		if (m_selectedNode == Resource::Prefab::ROOT_NODE) return;	// ルートの位置は出す側が上書きする

		auto* _pPrefab = RefPrefab();
		if (!_pPrefab || !_pPrefab->IsValidNode(m_selectedNode)) return;

		const ECS::ComponentTypeID _ltTypeID = _pWorld->GetCompTypeID<App::Component::LocalTransformComponent>();
		uint8_t* _pPrefabLT = _pPrefab->RefData(_ltTypeID, m_selectedNode);
		if (!_pPrefabLT) return;

		const ECS::Entity _entity = GetNodeEntity(m_selectedNode);
		if (_entity == ECS::Limits::INVALID_ENTITY || !_pWorld->IsAliveEntity(_entity)) return;
		if (!_pWorld->HasComponent<App::Component::LocalTransformComponent>(_entity)) return;
		if (!_pWorld->HasComponent<App::Component::WorldMatrixComponent>(_entity)) return;

		auto* _pEntityLT = _pWorld->RefData<App::Component::LocalTransformComponent>(_entity);
		auto* _pEntityWM = _pWorld->RefData<App::Component::WorldMatrixComponent>(_entity);

		// 親のワールド行列(ローカルの移動量へ直すのに使う)
		Math::Matrix _parentMat = Math::Matrix::Identity();
		const int _parentIndex = _pPrefab->GetChildren()[static_cast<size_t>(m_selectedNode)].parentIndex;
		const ECS::Entity _parentEntity = GetNodeEntity(_parentIndex);
		if (_parentEntity != ECS::Limits::INVALID_ENTITY &&
			_pWorld->IsAliveEntity(_parentEntity) &&
			_pWorld->HasComponent<App::Component::WorldMatrixComponent>(_parentEntity))
		{
			_parentMat = _pWorld->RefData<App::Component::WorldMatrixComponent>(_parentEntity)->worldMat;
		}

		const Math::Matrix _beforeMat = _pEntityWM->worldMat;
		Math::Matrix _gizmoMat = _beforeMat;

		const Math::Matrix _viewMat = _pCamera->GetWorldMatrix().Invert();
		const float _snap = Engine::EditorField::IsCtrlDown() ? 0.1f : 0.0f;

		// 後ろのシーンビューも同じフレームにギズモを出しているので、掴んだ状態が混ざらないよう ID を分ける
		ImGuizmo::PushID("PrefabEditorGizmo");
		const bool _isUsing = Engine::EditorField::TranslateGizmo(_viewMat, _pCamera->GetProjMatrix(), _gizmoMat, _snap);
		ImGuizmo::PopID();
		if (!_isUsing) return;

		const Math::Vector3 _worldDelta = _gizmoMat.Translation() - _beforeMat.Translation();

		constexpr float MOVE_EPS = 1e-8f;
		if (_worldDelta.LengthSquared() <= MOVE_EPS) return;

		// 親のスケールが 0 などで逆行列が作れないときは、ローカルが壊れるので動かさない
		float _det = 0.0f;
		const Math::Matrix _invParent = _parentMat.Invert(_det);
		if (_det == 0.0f) return;

		// 位置ではなく変位なので平行移動成分は使わない
		const Math::Vector3 _localDelta = Math::Vector3::TransformNormal(_worldDelta, _invParent);

		// ---- プレハブの中身 ----
		App::Component::LocalTransformComponent _prefabLT = {};
		std::memcpy(&_prefabLT, _pPrefabLT, sizeof(_prefabLT));
		_prefabLT.pos += _localDelta;
		_prefabLT.isDirty = true;
		std::memcpy(_pPrefabLT, &_prefabLT, sizeof(_prefabLT));

		// ---- 見えている実体 ----
		_pEntityLT->pos += _localDelta;
		_pEntityLT->isDirty = true;
		_pEntityWM->worldMat.SetTranslation(_beforeMat.Translation() + _worldDelta);

		// 実体はもう同じ値になっているので、これで出し直しを起こさない
		m_spawnedHash = ComputePrefabHash();
	}

	void PrefabEditor::DrawInfo()
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		const auto* _pPrefab = RefPrefab();
		if (!_pPrefab) return;

		const int _nodeCount = static_cast<int>(_pPrefab->GetNodeCount());
		const int _aliveCount = _pWorld ? static_cast<int>(_pWorld->GetAliveEntityCount()) : 0;

		Engine::EditorField::Value("Nodes", "%d", _nodeCount);
		Engine::EditorField::Tooltip("| Alive entities : %d", _aliveCount);

		if (_nodeCount > 0 && m_nodeEntityVec.empty())
		{
			Engine::EditorField::HelpText("実体を出せませんでした(コンポーネントを持っていないかもしれません)");
		}
	}

	//--------------------------------------------------------------------------------------
	// 右 : 選んでいるノードのコンポーネント
	//
	// 中身はアセットインスペクターと同じ関数(PrefabComponentsEdit)を呼ぶだけ
	//--------------------------------------------------------------------------------------
	void PrefabEditor::DrawEditPane()
	{
		ECS::World* _pWorld = m_preview.RefWorld();
		auto* _pPrefab = RefPrefab();
		if (!_pPrefab || !_pWorld)
		{
			Engine::EditorField::HelpText("プレハブを読み込めませんでした");
			return;
		}

		if (!_pPrefab->IsValidNode(m_selectedNode)) m_selectedNode = Resource::Prefab::ROOT_NODE;

		Engine::EditorField::Header(GetNodeName(m_selectedNode).c_str());

		if (m_selectedNode == Resource::Prefab::ROOT_NODE)
		{
			Engine::EditorField::HelpText("ルートの位置は、出す側(スポナー・ヒエラルキー)が上書きします");
		}
		else
		{
			const int _parentIndex = _pPrefab->GetChildren()[static_cast<size_t>(m_selectedNode)].parentIndex;
			Engine::EditorField::HelpText("Parent : %s", GetNodeName(_parentIndex).c_str());
		}

		Engine::EditorField::Line();

		Inspector::PrefabComponentsEdit(_pWorld, _pPrefab, m_selectedNode);
	}
}
