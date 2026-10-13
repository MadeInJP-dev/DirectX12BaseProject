#include "ModelEdit.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Model/IO/ModelConverter/ModelConverter.h"

#include "../../AssetLink.h"

namespace Editor::Inspector
{
	namespace
	{
		//-----------------------------------------------------------------------------------------
		// 行列を読み取り専用で表示
		//-----------------------------------------------------------------------------------------
		void DrawMatrixText(const char* a_label, const Math::Matrix& a_mat)
		{
			if (!ImGui::TreeNode(a_label)) { return; }

			for (int _row = 0; _row < 4; ++_row)
			{
				Engine::EditorField::Text("%8.3f, %8.3f, %8.3f, %8.3f", a_mat.m[_row][0], a_mat.m[_row][1], a_mat.m[_row][2], a_mat.m[_row][3]);
			}
			ImGui::TreePop();
		}

		//-----------------------------------------------------------------------------------------
		// ノード1つ分の詳細表示
		//-----------------------------------------------------------------------------------------
		void DrawNodeDetail(const Resource::Node& a_node, int a_nodeIdx)
		{
			Engine::EditorField::Value("Index", "%d", a_nodeIdx);
			Engine::EditorField::Value("NameHash", "%u", a_node.nodeNameHash);
			Engine::EditorField::Value("Parent", "%d", a_node.parent);
			Engine::EditorField::Value("Children", "%zu", a_node.children.size());
			Engine::EditorField::Value("BoneIndex", "%d", a_node.boneIndex);
			Engine::EditorField::Value("IsSkinMesh", "%s", a_node.isSkinMesh ? "true" : "false");

			// このノードが持つメッシュ
			if (a_node.meshIndices.empty())
			{
				Engine::EditorField::Value("MeshIndices", "none");
			}
			else
			{
				std::string _meshIdxStr;
				for (auto _meshIdx : a_node.meshIndices)
				{
					if (!_meshIdxStr.empty()) { _meshIdxStr += ", "; }
					_meshIdxStr += std::to_string(_meshIdx);
				}
				Engine::EditorField::Value("MeshIndices", "%s", _meshIdxStr.c_str());
			}

			// 各種行列
			DrawMatrixText("LocalTransform", a_node.localTransform);
			DrawMatrixText("WorldTransform", a_node.worldTransform);
			DrawMatrixText("BoneInverseWorldMatrix", a_node.boneInverseWorldMatrix);
		}

		//-----------------------------------------------------------------------------------------
		// ノード階層を再帰的に表示
		//-----------------------------------------------------------------------------------------
		void DrawNodeTree(const std::vector<Resource::Node>& a_nodeVec, int a_nodeIdx)
		{
			// 不正なインデックスは無視
			if (a_nodeIdx < 0 || a_nodeIdx >= static_cast<int>(a_nodeVec.size())) { return; }

			const auto& _node = a_nodeVec[a_nodeIdx];

			// ノード種別が一目で分かるようにサフィックスを付ける
			std::string _label = _node.name;
			if (_node.boneIndex >= 0) { _label += " [Bone]"; }
			if (!_node.meshIndices.empty()) { _label += " [Mesh]"; }

			// 子を持たないノードも、開けば詳細を見られるようにしておく
			bool _isOpen = ImGui::TreeNodeEx(
				reinterpret_cast<void*>(static_cast<intptr_t>(a_nodeIdx)),
				ImGuiTreeNodeFlags_None,
				"%s", _label.c_str()
			);
			if (!_isOpen) { return; }

			// このノード自身の詳細
			if (ImGui::TreeNode("Detail"))
			{
				DrawNodeDetail(_node, a_nodeIdx);
				ImGui::TreePop();
			}

			// 子ノード
			for (auto _childIdx : _node.children)
			{
				DrawNodeTree(a_nodeVec, _childIdx);
			}

			ImGui::TreePop();
		}

		//=========================================================================================
		// ボーンレイヤー
		//=========================================================================================

		// 編集してまだ保存していないモデル
		// (アセットを選び直しても未保存が分かるように、インスペクターの外に置く)
		std::unordered_set<const Resource::Model*> s_dirtyModelSet;

		//-----------------------------------------------------------------------------------------
		// レイヤー内のノードの重みを引く。載っていなければ nullptr
		//-----------------------------------------------------------------------------------------
		Resource::BoneWeight* FindBoneWeight(Resource::BoneMask& a_mask, int a_nodeIdx)
		{
			auto _it = std::lower_bound(a_mask.bones.begin(), a_mask.bones.end(), a_nodeIdx,
				[](const Resource::BoneWeight& a_bone, int a_idx) { return a_bone.nodeIndex < a_idx; });
			if (_it == a_mask.bones.end() || _it->nodeIndex != a_nodeIdx) return nullptr;
			return &(*_it);
		}

		//-----------------------------------------------------------------------------------------
		// ノードの重みを設定する(無ければノード番号の昇順を保って足す)
		//-----------------------------------------------------------------------------------------
		void SetBoneWeight(Resource::BoneMask& a_mask, int a_nodeIdx, float a_weight)
		{
			auto _it = std::lower_bound(a_mask.bones.begin(), a_mask.bones.end(), a_nodeIdx,
				[](const Resource::BoneWeight& a_bone, int a_idx) { return a_bone.nodeIndex < a_idx; });
			if (_it != a_mask.bones.end() && _it->nodeIndex == a_nodeIdx)
			{
				_it->weight = a_weight;
				return;
			}
			a_mask.bones.insert(_it, Resource::BoneWeight{ static_cast<uint16_t>(a_nodeIdx), a_weight });
		}

		//-----------------------------------------------------------------------------------------
		// ノードをレイヤーから外す
		//-----------------------------------------------------------------------------------------
		void RemoveBoneWeight(Resource::BoneMask& a_mask, int a_nodeIdx)
		{
			std::erase_if(a_mask.bones, [a_nodeIdx](const Resource::BoneWeight& a_bone) { return a_bone.nodeIndex == a_nodeIdx; });
		}

		//-----------------------------------------------------------------------------------------
		// 自身と子孫すべてに重みを設定する。a_weight が無ければ外す
		//-----------------------------------------------------------------------------------------
		void ApplyToDescendants(const std::vector<Resource::Node>& a_nodeVec, Resource::BoneMask& a_mask, int a_nodeIdx, std::optional<float> a_weight)
		{
			if (a_nodeIdx < 0 || a_nodeIdx >= static_cast<int>(a_nodeVec.size())) { return; }

			if (a_weight) { SetBoneWeight(a_mask, a_nodeIdx, *a_weight); }
			else          { RemoveBoneWeight(a_mask, a_nodeIdx); }

			for (auto _childIdx : a_nodeVec[a_nodeIdx].children)
			{
				ApplyToDescendants(a_nodeVec, a_mask, _childIdx, a_weight);
			}
		}

		//-----------------------------------------------------------------------------------------
		// 他と被らないレイヤー名を作る
		//-----------------------------------------------------------------------------------------
		std::string MakeUniqueBoneMaskName(const std::vector<Resource::BoneMask>& a_maskVec, const std::string& a_baseName)
		{
			auto _isUsed = [&](const std::string& a_name)
				{
					return std::any_of(a_maskVec.begin(), a_maskVec.end(),
						[&](const Resource::BoneMask& a_mask) { return a_mask.name == a_name; });
				};

			if (!_isUsed(a_baseName)) { return a_baseName; }
			for (int _i = 1;; ++_i)
			{
				std::string _name = a_baseName + std::to_string(_i);
				if (!_isUsed(_name)) { return _name; }
			}
		}

		//-----------------------------------------------------------------------------------------
		// ノード階層を、レイヤーに含めるかのチェックと重み付きで再帰表示
		// 右クリックで子孫へまとめて設定できる
		//-----------------------------------------------------------------------------------------
		bool DrawBoneMaskTree(const std::vector<Resource::Node>& a_nodeVec, Resource::BoneMask& a_mask, int a_nodeIdx)
		{
			if (a_nodeIdx < 0 || a_nodeIdx >= static_cast<int>(a_nodeVec.size())) { return false; }

			const auto& _node = a_nodeVec[a_nodeIdx];
			bool _isChanged = false;

			ImGui::PushID(a_nodeIdx);

			ImGuiTreeNodeFlags _flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowOverlap;
			if (_node.children.empty()) { _flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; }
			const bool _isOpen = ImGui::TreeNodeEx("##BoneMaskNode", _flags);

			// 含めるかどうか
			ImGui::SameLine();
			bool _isInclude = (FindBoneWeight(a_mask, a_nodeIdx) != nullptr);
			std::string _label = _node.name;
			if (_node.boneIndex >= 0) { _label += " [Bone]"; }
			if (ImGui::Checkbox(_label.c_str(), &_isInclude))
			{
				if (_isInclude) { SetBoneWeight(a_mask, a_nodeIdx, 1.0f); }
				else            { RemoveBoneWeight(a_mask, a_nodeIdx); }
				_isChanged = true;
			}

			// 子孫へまとめて設定
			if (ImGui::BeginPopupContextItem("BoneMaskNodeMenu"))
			{
				const auto* _pBone = FindBoneWeight(a_mask, a_nodeIdx);

				if (ImGui::MenuItem("Include with descendants (weight 1.0)"))
				{
					ApplyToDescendants(a_nodeVec, a_mask, a_nodeIdx, 1.0f);
					_isChanged = true;
				}
				if (ImGui::MenuItem("Copy weight to descendants", nullptr, false, _pBone != nullptr))
				{
					ApplyToDescendants(a_nodeVec, a_mask, a_nodeIdx, _pBone->weight);
					_isChanged = true;
				}
				if (ImGui::MenuItem("Exclude with descendants"))
				{
					ApplyToDescendants(a_nodeVec, a_mask, a_nodeIdx, std::nullopt);
					_isChanged = true;
				}
				ImGui::EndPopup();
			}

			// 重み : 上の操作で配列が動いているかもしれないので、ここで引き直す
			if (auto* _pBone = FindBoneWeight(a_mask, a_nodeIdx))
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::SliderFloat("##Weight", &_pBone->weight, 0.0f, 1.0f, "%.2f"))
				{
					_pBone->weight = std::clamp(_pBone->weight, 0.0f, 1.0f);
					_isChanged = true;
				}
			}

			// 子ノード
			if (_isOpen && !_node.children.empty())
			{
				for (auto _childIdx : _node.children)
				{
					_isChanged |= DrawBoneMaskTree(a_nodeVec, a_mask, _childIdx);
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
			return _isChanged;
		}

		//-----------------------------------------------------------------------------------------
		// ボーンレイヤーの一覧・追加・削除・保存
		//-----------------------------------------------------------------------------------------
		void DrawBoneMasks(EditorContext& a_editContext, Resource::Model& a_model)
		{
			auto& _maskVec = a_model.RefBoneMaskVec();
			const auto& _nodeVec = a_model.GetOriginalNodeVec();
			bool _isChanged = false;

			Engine::EditorField::HelpText("(right click a node to apply to its descendants)");

			// 追加
			if (Engine::EditorField::CreateButton("Add Layer"))
			{
				Resource::BoneMask _mask = {};
				_mask.name = MakeUniqueBoneMaskName(_maskVec, "NewLayer");
				_mask.nameHash = Core::String::ToHash(_mask.name);
				_maskVec.push_back(std::move(_mask));
				_isChanged = true;
			}

			// 保存 : .mdl へ書き出す(gltf のままのモデルはコンバートされる)
			Engine::EditorField::SameLine();
			if (Engine::EditorField::Button("Save") && a_editContext.pAssetProp)
			{
				if (Resource::Converter::ModelConverter::SaveModelAsset(*a_editContext.pServices->pResourceManager, a_editContext.pAssetProp->guid))
				{
					s_dirtyModelSet.erase(&a_model);
					ENGINE_LOG("ボーンレイヤーを保存 : %s", a_model.GetName().c_str());
				}
			}
			if (s_dirtyModelSet.contains(&a_model))
			{
				Engine::EditorField::SameLine();
				Engine::EditorField::WarningText("(unsaved)");
			}

			// レイヤーごと
			int _deleteIdx = -1;
			for (size_t _i = 0; _i < _maskVec.size(); ++_i)
			{
				auto& _mask = _maskVec[_i];
				ImGui::PushID(static_cast<int>(_i));

				const bool _isOpen = ImGui::TreeNodeEx("BoneMask", ImGuiTreeNodeFlags_AllowOverlap, "%s (%zu nodes)", _mask.name.c_str(), _mask.bones.size());

				Engine::EditorField::SameLine();
				if (Engine::EditorField::DeleteSmallButton("Delete"))
				{
					_deleteIdx = static_cast<int>(_i);
				}

				if (_isOpen)
				{
					// 名前 : 参照側はハッシュで引くので一緒に作り直す
					if (Engine::EditorField::Field("Name", _mask.name))
					{
						_mask.nameHash = Core::String::ToHash(_mask.name);
						_isChanged = true;
					}
					const auto _sameNameCount = std::count_if(_maskVec.begin(), _maskVec.end(),
						[&](const Resource::BoneMask& a_other) { return a_other.name == _mask.name; });
					if (_sameNameCount > 1)
					{
						Engine::EditorField::WarningText("Name is duplicated. Only the first one is found.");
					}
					if (_mask.name.empty())
					{
						Engine::EditorField::WarningText("Name is empty.");
					}

					// まとめて操作
					if (Engine::EditorField::SmallButton("Include All"))
					{
						for (auto _rootIdx : a_model.GetRootNodeVec())
						{
							ApplyToDescendants(_nodeVec, _mask, _rootIdx, 1.0f);
						}
						_isChanged = true;
					}
					Engine::EditorField::SameLine();
					if (Engine::EditorField::SmallButton("Clear"))
					{
						_mask.bones.clear();
						_isChanged = true;
					}

					// ノード階層
					for (auto _rootIdx : a_model.GetRootNodeVec())
					{
						_isChanged |= DrawBoneMaskTree(_nodeVec, _mask, _rootIdx);
					}

					ImGui::TreePop();
				}

				ImGui::PopID();
			}

			if (_deleteIdx >= 0)
			{
				_maskVec.erase(_maskVec.begin() + _deleteIdx);
				_isChanged = true;
			}

			if (_isChanged)
			{
				s_dirtyModelSet.insert(&a_model);
			}
		}

		//-----------------------------------------------------------------------------------------
		// LOD の段を読み直して、モデルへ持たせる(編集した結果をすぐ描画へ反映するため)
		//
		// 読み込み時(ModelIO::LoadLodModels)と同じく、自分自身と、LOD を持っているモデルは段にしない。
		// LOD を持てるのは LOD0 だけ
		//-----------------------------------------------------------------------------------------
		void ReloadLodModels(EditorContext& a_editContext, Resource::Model& a_model)
		{
			auto& _resManager = *a_editContext.pServices->pResourceManager;
			auto& _assetDB = _resManager.RefAssetDatabase();
			const Core::GUID _selfGUID = a_editContext.pAssetProp ? a_editContext.pAssetProp->guid : Core::GUID();

			std::vector<Engine::ResourceRef<Resource::Model>> _lodModels = {};
			for (const auto& _level : a_model.GetLodLevelVec())
			{
				Engine::ResourceRef<Resource::Model> _lodModel = {};

				const auto* _pProp = _level.modelGUID.IsValid() ? _assetDB.FindAssetProperty(_level.modelGUID) : nullptr;
				if (_pProp && _pProp->type == "Model" && !(_level.modelGUID == _selfGUID))
				{
					_lodModel = _resManager.LoadImmediate<Resource::Model>(_level.modelGUID);

					const auto* _pLodModel = _resManager.Get(_lodModel);
					if (_pLodModel && !_pLodModel->GetLodLevelVec().empty())
					{
						ENGINE_LOG("LOD を持っているモデルは LOD の段にできません : %s", _pLodModel->GetName().c_str());
						_lodModel = {};
					}
				}
				_lodModels.push_back(std::move(_lodModel));
			}
			a_model.SetLodModels(std::move(_lodModels));
		}

		//-----------------------------------------------------------------------------------------
		// LOD の段の編集
		//-----------------------------------------------------------------------------------------
		void DrawLodLevels(EditorContext& a_editContext, Resource::Model& a_model)
		{
			auto& _levelVec = a_model.RefLodLevelVec();
			const auto& _lodModels = a_model.GetLodModels();
			auto& _resManager = *a_editContext.pServices->pResourceManager;
			bool _isChanged = false;

			Engine::EditorField::HelpText("LOD モデルの描画コマンド(サブセット)は、このモデルと同じ並びにしてください");
			Engine::EditorField::HelpText("Screen Size : 画面の高さに対する直径の割合。これより小さく映るとその段で描きます(上の段ほど大きく)");
			Engine::EditorField::HelpText("アニメーションするモデルには効きません");

			// 追加 : 1つ上の段の半分の大きさで切り替える
			if (_levelVec.size() < Resource::MAX_MODEL_LOD_LEVELS && Engine::EditorField::CreateButton("Add LOD"))
			{
				Resource::ModelLodLevel _level = {};
				if (!_levelVec.empty()) _level.screenSize = _levelVec.back().screenSize * 0.5f;
				_levelVec.push_back(_level);
				_isChanged = true;
			}

			// 保存 : .mdl へ書き出す(gltf のままのモデルはコンバートされる)
			Engine::EditorField::SameLine();
			if (Engine::EditorField::Button("Save##Lod") && a_editContext.pAssetProp)
			{
				if (Resource::Converter::ModelConverter::SaveModelAsset(_resManager, a_editContext.pAssetProp->guid))
				{
					s_dirtyModelSet.erase(&a_model);
					ENGINE_LOG("LOD を保存 : %s", a_model.GetName().c_str());
				}
			}
			if (s_dirtyModelSet.contains(&a_model))
			{
				Engine::EditorField::SameLine();
				Engine::EditorField::WarningText("(unsaved)");
			}

			// 段ごと
			int _deleteIdx = -1;
			for (size_t _i = 0; _i < _levelVec.size(); ++_i)
			{
				auto& _level = _levelVec[_i];
				ImGui::PushID(static_cast<int>(_i));

				Engine::EditorField::Value("Level", "LOD%zu", _i + 1);
				Engine::EditorField::SameLine();
				if (Engine::EditorField::DeleteSmallButton("Delete"))
				{
					_deleteIdx = static_cast<int>(_i);
				}

				_isChanged |= Engine::EditorField::AssetField(*a_editContext.pServices, "Model", "Model", _level.modelGUID);
				_isChanged |= Engine::EditorField::Field("Screen Size", _level.screenSize, 0.001f, 0.0f, 1.0f, "%.3f");

				// 段にできなかったものを知らせる(未設定・自分自身・LOD を持つモデル)
				const bool _isLoaded = (_i < _lodModels.size()) && _resManager.Get(_lodModels[_i]);
				if (!_isLoaded)
				{
					Engine::EditorField::WarningText("(この段は LOD0 で描かれます)");
				}

				ImGui::PopID();
			}

			if (_deleteIdx >= 0)
			{
				_levelVec.erase(_levelVec.begin() + _deleteIdx);
				_isChanged = true;
			}

			if (_isChanged)
			{
				ReloadLodModels(a_editContext, a_model);
				s_dirtyModelSet.insert(&a_model);
			}
		}
	}

	//-----------------------------------------------------------------------------------------
	// モデルの詳細表示
	//-----------------------------------------------------------------------------------------
	void ModelEdit(EditorContext& a_editContext, Resource::Model* a_pModel)
	{
		if (!a_pModel) { return; }

		const auto& _assetData = a_pModel->GetAssestData();
		const auto& _nodeVec = a_pModel->GetOriginalNodeVec();

		// ---- 概要 ----
		Engine::EditorField::Value("Name", "%s", a_pModel->GetName().c_str());
		Engine::EditorField::Value("Nodes", "%zu", _nodeVec.size());
		Engine::EditorField::Value("Meshes", "%zu", _assetData.meshGUIDs.size());
		Engine::EditorField::Value("Materials", "%zu", _assetData.materialGUIDs.size());
		Engine::EditorField::Value("Animations", "%zu", _assetData.animationGUIDs.size());
		Engine::EditorField::Value("Bones", "%zu", a_pModel->GetBoneNodeVec().size());
		Engine::EditorField::Value("Bone Layers", "%zu", a_pModel->GetBoneMaskVec().size());
		Engine::EditorField::Value("LOD Levels", "%zu", a_pModel->GetLodLevelVec().size());

		Engine::EditorField::Line();

		// ---- ノード階層 ----
		if (ImGui::CollapsingHeader("Node Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
		{
			Engine::EditorField::HelpText("(open a node to see its detail)");

			// ルートノードから再帰的に表示
			for (auto _rootIdx : a_pModel->GetRootNodeVec())
			{
				DrawNodeTree(_nodeVec, _rootIdx);
			}
		}

		// ---- ボーンレイヤー ----
		// アニメーションレイヤリングで、重ねるアニメーターを効かせるノードのマスク
		if (ImGui::CollapsingHeader("Bone Layers"))
		{
			DrawBoneMasks(a_editContext, *a_pModel);
		}

		// ---- LOD ----
		// 遠くで描く粗いモデル。どの段で描くかは増幅シェーダーがパスごとのカメラで決める
		if (ImGui::CollapsingHeader("LOD"))
		{
			DrawLodLevels(a_editContext, *a_pModel);
		}

		// ---- アニメーション ----
		if (ImGui::CollapsingHeader("Animations"))
		{
			const auto& _animHandleVec = a_pModel->GetAnimationHandles();
			if (_animHandleVec.empty())
			{
				Engine::EditorField::HelpText("No animation");
			}

			for (size_t _i = 0; _i < _animHandleVec.size(); ++_i)
			{
				ImGui::PushID(static_cast<int>(_i));

				const auto* _pAnim = a_editContext.pServices->pResourceManager->Ref(_animHandleVec[_i]);

				// 未ロードでもアセットとしては飛べるので、リンクだけは出す
				if (!_pAnim)
				{
					const Core::GUID _animGUID = (_i < _assetData.animationGUIDs.size())
						? _assetData.animationGUIDs[_i]
						: Core::DEFAULT_GUID;

					const std::string _index = "[" + std::to_string(_i) + "]";
					DrawAssetLink(&a_editContext, _index.c_str(), _animGUID);
					Engine::EditorField::SameLine();
					Engine::EditorField::HelpText("(not loaded)");

					ImGui::PopID();
					continue;
				}

				if (ImGui::TreeNode("AnimNode", "[%zu] %s", _i, _pAnim->name.c_str()))
				{
					if (_i < _assetData.animationGUIDs.size())
					{
						DrawAssetLink(&a_editContext, "Asset     :", _assetData.animationGUIDs[_i]);
					}
					Engine::EditorField::Value("MaxLength", "%.3f frame", _pAnim->maxLength);
					Engine::EditorField::Value("AnimNodes", "%zu", _pAnim->nodes.size());

					// アニメーションが動かすノードとキー数
					if (ImGui::TreeNode("Channels"))
					{
						for (const auto& _animNode : _pAnim->nodes)
						{
							// 対象ノード名を引けるなら名前で表示
							std::string _targetName = "unknown";
							if (_animNode.nodeOffset >= 0 &&
								_animNode.nodeOffset < static_cast<int>(_nodeVec.size()))
							{
								_targetName = _nodeVec[_animNode.nodeOffset].name;
							}

							Engine::EditorField::Text("%s (node %d) : T=%zu R=%zu S=%zu", _targetName.c_str(), _animNode.nodeOffset, _animNode.translations.size(), _animNode.rotations.size(), _animNode.scales.size());
						}
						ImGui::TreePop();
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}

		// ---- メッシュ ----
		if (ImGui::CollapsingHeader("Meshes"))
		{
			const auto& _meshHandleVec = a_pModel->GetMeshHandles();
			for (size_t _i = 0; _i < _meshHandleVec.size(); ++_i)
			{
				const Core::GUID _meshGUID = (_i < _assetData.meshGUIDs.size())
					? _assetData.meshGUIDs[_i]
					: Core::DEFAULT_GUID;

				ImGui::PushID(static_cast<int>(_i));

				// 中身を見に行けるように、まず飛べる見出しを出す
				const std::string _index = "[" + std::to_string(_i) + "]";
				DrawAssetLink(&a_editContext, _index.c_str(), _meshGUID);

				const auto* _pMesh = a_editContext.pServices->pResourceManager->Ref(_meshHandleVec[_i]);
				if (!_pMesh)
				{
					Engine::EditorField::SameLine();
					Engine::EditorField::HelpText("(not loaded)");
					ImGui::PopID();
					continue;
				}

				// ここで出すのは概要だけ。詳しくはリンク先のメッシュインスペクタで見る
				const auto& _metaData = _pMesh->GetMetaData();
				Engine::EditorField::SameLine();
				Engine::EditorField::HelpText("verts=%zu subsets=%zu%s", _pMesh->GetVertexVec().size(), _metaData.subsets.size(), _metaData.isSkinMesh ? " [Skin]" : "");

				ImGui::PopID();
			}
		}

		// ---- マテリアル ----
		if (ImGui::CollapsingHeader("Materials"))
		{
			const auto& _materialHandleVec = a_pModel->GetMaterialHandles();
			for (size_t _i = 0; _i < _materialHandleVec.size(); ++_i)
			{
				const Core::GUID _materialGUID = (_i < _assetData.materialGUIDs.size())
					? _assetData.materialGUIDs[_i]
					: Core::DEFAULT_GUID;

				ImGui::PushID(static_cast<int>(_i));

				const auto* _pMaterial = a_editContext.pServices->pResourceManager->Ref(_materialHandleVec[_i]);

				// 表示はマテリアル名を優先する(ファイル名と食い違うことがある)。
				// ロードできていなければアセット名に任せる
				const std::string _index = "[" + std::to_string(_i) + "]";
				DrawAssetLink(&a_editContext, _index.c_str(), _materialGUID,
					_pMaterial ? _pMaterial->name.c_str() : nullptr);

				if (!_pMaterial)
				{
					Engine::EditorField::SameLine();
					Engine::EditorField::HelpText("(not loaded)");
				}

				ImGui::PopID();
			}
		}

		// ---- 描画コマンド ----
		if (ImGui::CollapsingHeader("Draw Commands"))
		{
			const auto& _drawCommandVec = a_pModel->GetDrawCommandVec();
			Engine::EditorField::Value("Count", "%zu", _drawCommandVec.size());

			for (size_t _i = 0; _i < _drawCommandVec.size(); ++_i)
			{
				const auto& _cmd = _drawCommandVec[_i];

				// ノード名が引けるなら名前で表示
				std::string _nodeName = "unknown";
				if (_cmd.nodeIndex < _nodeVec.size())
				{
					_nodeName = _nodeVec[_cmd.nodeIndex].name;
				}

				Engine::EditorField::Text("[%zu] node=%s sub=%u mesh=%u(gen%u) material=%u(gen%u) alpha=%s", _i, _nodeName.c_str(), static_cast<UINT>(_cmd.subIdx), static_cast<UINT>(_cmd.meshHandle.GetIndex()), static_cast<UINT>(_cmd.meshHandle.GetGeneration()), static_cast<UINT>(_cmd.materialHandle.GetIndex()), static_cast<UINT>(_cmd.materialHandle.GetGeneration()), std::string(magic_enum::enum_name(_cmd.alphaMode)).c_str());
			}
		}
	}
}
