#include "Model.h"

#include "../../Manager/ResourceManager/ResourceManager.h"
#include "../../Manager/AssetDatabase/AssetDatabase.h"

#include "Core/BinaryHelper/BinaryHelper.h"

namespace Engine::Resource
{
	void Model::Save(ResourceManager& a_resourceManager, const std::string& a_fileDir)
	{
		auto& _assetDB = a_resourceManager.RefAssetDatabase();

		// ---- 参照しているデータもセーブ ----
		// マテリアルの保存
		UINT _mtrlHandleSize = static_cast<UINT>(m_runtimeData.materials.size());
		m_AssetData.materialGUIDs.resize(_mtrlHandleSize);
		for(UINT _i = 0; _i < _mtrlHandleSize; ++_i)
		{
			// マテリアル取得
			auto _mateHandle = m_runtimeData.materials[_i];
			auto* _matrial = a_resourceManager.Ref(_mateHandle);

			// 保存データ作成
			auto _fileName = Core::File::GetFileNameWithoutExtension(m_AssetData.name) + std::to_string(_i);
			std::string basePath ="Asset/Material/" + _fileName;

			// 保存
			auto _fullBasePath = basePath + "/" + _fileName;													// 拡張子なしのパス
			m_AssetData.materialGUIDs[_i] = _assetDB.AddMetaData(_fullBasePath, "Material");	// メタファイルを作成
			Persistence::Archive _ar(Persistence::Archive::EMode::Save, basePath, _fileName, "mtrl");
			_matrial->Archive(_ar);
			//_matrial->Save(basePath, _fileName);														// メタファイルの隣にデータ作成

		}
		// メッシュの保存
		UINT _meshHandleSize = static_cast<UINT>(m_runtimeData.meshes.size());
		m_AssetData.meshGUIDs.resize(_meshHandleSize);
		for (UINT _i = 0; _i < _meshHandleSize; ++_i)
		{
			// メッシュ取得
			auto _meshHandle = m_runtimeData.meshes[_i];
			auto* _mesh = a_resourceManager.Ref(_meshHandle);

			// 保存データ作成
			auto _fileName = Core::File::GetFileNameWithoutExtension(m_AssetData.name) + std::to_string(_i);
			std::string basePath ="Asset/Mesh/" + _fileName;

			// 保存
			auto _fullBasePath = basePath + "/" + _fileName;
			m_AssetData.meshGUIDs[_i] = _assetDB.AddMetaData(_fullBasePath, "Mesh");
			_mesh->Save(basePath, _fileName);

		}
		// アニメーションの保存
		UINT _animHandleSize = static_cast<UINT>(m_runtimeData.animations.size());
		m_AssetData.animationGUIDs.resize(_animHandleSize);
		for (UINT _i = 0; _i < _animHandleSize; ++_i)
		{
			// アニメーションの取得
			auto _animHandle = m_runtimeData.animations[_i];
			auto* _anim = a_resourceManager.Ref(_animHandle);

			// 保存データ作成
			auto _fileName = Core::File::GetFileNameWithoutExtension(m_AssetData.name) + std::to_string(_i);
			std::string basePath = "Asset/Animation/" + _fileName;

			// 保存
			auto _fullBasePath = basePath + "/" + _fileName;
			m_AssetData.animationGUIDs[_i] = _assetDB.AddMetaData(_fullBasePath, "Animation");
			_anim->Save(basePath, _fileName);
		}

		// モデルデータの保存
		auto _dir = Core::File::GetDirFromPath(a_fileDir);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_fileDir);
		Persistence::Archive _ar(Persistence::Archive::EMode::Save, _dir, _fileName, "mdl");
		_ar.StringField("ModelName", m_AssetData.name);

		_ar.GUIDVectorField("MaterialGUID", m_AssetData.materialGUIDs);
		_ar.GUIDVectorField("MeshGUID", m_AssetData.meshGUIDs);
		_ar.GUIDVectorField("AnimationGUID", m_AssetData.animationGUIDs);

		UINT _nodeCount = static_cast<UINT>(m_AssetData.originalNodes.size());
		_ar.Field("NodeCount",_nodeCount);
		for (UINT _i = 0; _i < _nodeCount; ++_i)
		{
			m_AssetData.originalNodes[_i].Archive(_ar, _i);
		}

		_ar.VectorField("RootNodeIndices", m_AssetData.rootNodeIndices);
		_ar.VectorField("BoneNodeIndices", m_AssetData.boneNodeIndices);
		_ar.VectorField("MeshNodeIndices", m_AssetData.meshNodeIndices);
		_ar.VectorField("CollisionMeshNodeIndices", m_AssetData.collisionMeshNodeIndices);
		_ar.VectorField("DrawMeshNodeIndices", m_AssetData.drawMeshNodeIndices);

		ArchiveBoneMasks(_ar, m_AssetData.boneMasks);
		ArchiveLodLevels(_ar, m_AssetData.lodLevels);

	}
	void Model::Release()
	{
		
	}
	Core::GUID Model::GetAnimationGUIDFromHandle(const Handle<AnimationData>& a_handle) const
	{
		// セーブ時の1回だけ線形探索
		for (size_t _i = 0; _i < m_runtimeData.animations.size(); ++_i)
		{
			if (m_runtimeData.animations[_i] == a_handle)
			{
				// 同じインデックスのGUIDを返す
				return m_AssetData.animationGUIDs[_i];
			}
		}

		// 見つからなかった場合
		return Core::GUID();
	}
	Handle<AnimationData> Model::GetAnimationHandleFromGUID(const Core::GUID& a_guid) const
	{
		// セーブ時の1回だけ線形探索
		for (size_t _i = 0; _i < m_AssetData.animationGUIDs.size(); ++_i)
		{
			if (m_AssetData.animationGUIDs[_i] == a_guid)
			{
				// 同じインデックスのGUIDを返す
				return m_runtimeData.animations[_i];
			}
		}

		// 見つからなかった場合
		return Handle<AnimationData>();
	}
	const BoneMask* Model::FindBoneMask(UINT a_nameHash) const
	{
		for (const auto& _mask : m_AssetData.boneMasks)
		{
			if (_mask.nameHash == a_nameHash) return &_mask;
		}
		return nullptr;
	}

	void ArchiveBoneMasks(Persistence::Archive& a_ar, std::vector<BoneMask>& a_masks)
	{
		// 追加前に書き出された .mdl は末尾にこのデータが無い。
		// そのときは要素数が読めず 0 のままなので、レイヤー無しとして読まれる
		size_t _maskCount = a_ar.IsLoading() ? 0 : a_masks.size();
		if (!a_ar.BeginArray("BoneMasks", _maskCount)) return;

		a_masks.resize(_maskCount);
		for (size_t _i = 0; _i < _maskCount; ++_i)
		{
			if (!a_ar.BeginObject(_i)) continue;

			auto& _mask = a_masks[_i];
			a_ar.StringField("Name", _mask.name);

			size_t _boneCount = a_ar.IsLoading() ? 0 : _mask.bones.size();
			if (a_ar.BeginArray("Bones", _boneCount))
			{
				_mask.bones.resize(_boneCount);
				for (size_t _j = 0; _j < _boneCount; ++_j)
				{
					if (!a_ar.BeginObject(_j)) continue;
					a_ar.Field("NodeIndex", _mask.bones[_j].nodeIndex);
					a_ar.Field("Weight", _mask.bones[_j].weight);
					a_ar.EndObject();
				}
				a_ar.EndArray();
			}

			if (a_ar.IsLoading())
			{
				_mask.nameHash = Core::String::ToHash(_mask.name);
			}
			a_ar.EndObject();
		}
		a_ar.EndArray();
	}

	void ArchiveLodLevels(Persistence::Archive& a_ar, std::vector<ModelLodLevel>& a_levels)
	{
		// 追加前に書き出された .mdl は末尾にこのデータが無い。
		// そのときは要素数が読めず 0 のままなので、LOD 無しとして読まれる
		size_t _levelCount = a_ar.IsLoading() ? 0 : a_levels.size();
		if (!a_ar.BeginArray("LodLevels", _levelCount)) return;

		// 壊れたデータで要素数が膨らんでいても、上限までしか持たない
		const size_t _keepCount = (std::min)(_levelCount, MAX_MODEL_LOD_LEVELS);
		if (a_ar.IsLoading()) a_levels.resize(_keepCount);

		for (size_t _i = 0; _i < _levelCount; ++_i)
		{
			if (!a_ar.BeginObject(_i)) continue;

			ModelLodLevel _dummy = {};
			auto& _level = (_i < a_levels.size()) ? a_levels[_i] : _dummy;
			a_ar.GUIDField("ModelGUID", _level.modelGUID);
			a_ar.Field("ScreenSize", _level.screenSize);

			a_ar.EndObject();
		}
		a_ar.EndArray();
	}
}
