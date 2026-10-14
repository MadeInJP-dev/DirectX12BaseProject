#include "PrefabSpawnHelper.h"

#include <cstring>

#include "Engine/ECS/World/World.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/Prefab/Prefab.h"

#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Core/SpawnerComponent.h"
#include "Application/Components/Camera/FollowTargetComponent.h"
#include "Application/Components/Transform/HierarchyComponent.h"
#include "Application/Components/Attachment/AttachmentSlotsComponent.h"
#include "Engine/ECS/Component/TransientTag.h"
#include "Engine/ECS/Component/GUIDComponent.h"

namespace App::Utility
{
	namespace
	{
		//----------------------------------------------------------------------
		// データマップに指定コンポーネントの領域を用意する。
		// プレハブが持っていなければシグネチャへ足し、既定構築しておく。
		// 返すのはその書き込み先(確保できなければ nullptr)。
		//----------------------------------------------------------------------
		uint8_t* EnsureComponentBuffer(
			Engine::ECS::World& a_world,
			Engine::ECS::Signature& a_sig,
			std::unordered_map<Engine::ECS::ComponentTypeID, std::vector<uint8_t>>& a_data,
			Engine::ECS::ComponentTypeID a_typeID)
		{
			if (a_typeID == Engine::ECS::Limits::INVALID_COMPONENTTYPEID) return nullptr;

			if (!a_sig.test(a_typeID))
			{
				a_sig.set(a_typeID);
				auto& _newBuf = a_data[a_typeID];
				_newBuf.assign(a_world.GetComponentMetaData(a_typeID).compAlignSize, 0);
				auto _ctor = a_world.GetCompFunc(a_typeID).construct;
				if (_ctor) _ctor(_newBuf.data());
			}

			auto& _buf = a_data[a_typeID];
			return _buf.empty() ? nullptr : _buf.data();
		}

		//----------------------------------------------------------------------
		// 親子をエンティティIDで付ける(a_index の材料を生成する前に書く)
		//
		// 親が GUIDComponent を持たないなら parentGUID は空にする。
		// プレハブが振った GUID が残っていると、HierarchyLinkSystem が
		// 誰も持っていない GUID から引き直して、付けた親を外してしまう。
		//----------------------------------------------------------------------
		void LinkParentByIndex(
			Engine::ECS::World& a_world,
			std::vector<Engine::Resource::PrefabInstanceData>& a_instanceVec,
			const std::vector<Engine::ECS::Entity>& a_entityVec,
			size_t a_index)
		{
			Engine::Resource::PrefabInstanceData& _data = a_instanceVec[a_index];

			// 親は必ず先に並んでいる(作り終えている)
			if (_data.parentIndex < 0 || static_cast<size_t>(_data.parentIndex) >= a_index) return;

			const Engine::ECS::Entity _parent = a_entityVec[_data.parentIndex];
			if (_parent == Engine::ECS::Limits::INVALID_ENTITY) return;

			auto _it = _data.dataMap.find(a_world.GetCompTypeID<Component::HierarchyComponent>());
			if (_it == _data.dataMap.end() || _it->second.size() < sizeof(Component::HierarchyComponent)) return;

			const Engine::Resource::PrefabInstanceData& _parentData = a_instanceVec[_data.parentIndex];
			const bool _isParentHasGUID = _parentData.sig.test(a_world.GetCompTypeID<Engine::ECS::GUIDComponent>());

			Component::HierarchyComponent _hierarchy = {};
			std::memcpy(&_hierarchy, _it->second.data(), sizeof(_hierarchy));
			_hierarchy.parentID   = _parent;
			_hierarchy.parentGUID = _isParentHasGUID ? _parentData.guid : Core::DEFAULT_GUID;
			std::memcpy(_it->second.data(), &_hierarchy, sizeof(_hierarchy));
		}

		//----------------------------------------------------------------------
		// アタッチメントのスロットをエンティティIDで付ける(全員を作り終えてから)
		//
		// スロットの GUID はプレハブが振った GUID へ張り替わっているので、
		// それを鍵に同じインスタンスの中から相手を引く。
		// 相手が GUIDComponent を持たないなら GUID は空にする(上の親子と同じ理由で、
		// AttachmentSlotLinkSystem に引き直させない)。
		//----------------------------------------------------------------------
		void LinkAttachmentSlots(
			Engine::ECS::World& a_world,
			const std::vector<Engine::Resource::PrefabInstanceData>& a_instanceVec,
			const std::vector<Engine::ECS::Entity>& a_entityVec)
		{
			std::unordered_map<Core::GUID, size_t> _indexMap = {};
			for (size_t _i = 0; _i < a_instanceVec.size(); ++_i)
			{
				if (a_instanceVec[_i].guid.IsValid()) _indexMap[a_instanceVec[_i].guid] = _i;
			}

			const Engine::ECS::ComponentTypeID _guidTypeID = a_world.GetCompTypeID<Engine::ECS::GUIDComponent>();

			for (const Engine::ECS::Entity _entity : a_entityVec)
			{
				if (_entity == Engine::ECS::Limits::INVALID_ENTITY) continue;
				if (!a_world.HasComponent<Component::AttachmentSlotsComponent>(_entity)) continue;

				Component::ForEachAttachmentSlot(*a_world.RefData<Component::AttachmentSlotsComponent>(_entity),
					[&](Component::AttachmentSlot& a_slot)
					{
						auto _it = _indexMap.find(a_slot.guid);
						if (_it == _indexMap.end()) return;

						a_slot.id = a_entityVec[_it->second];
						if (!a_instanceVec[_it->second].sig.test(_guidTypeID)) a_slot.guid = Core::DEFAULT_GUID;
					}
				);
			}
		}
	}

	bool SpawnPrefab(
		Engine::ECS::World& a_world,
		Engine::Resource::ResourceManager& a_resourceManager,
		const Core::GUID& a_prefabGUID,
		Engine::Handle<Engine::Resource::Prefab>& a_refHandle,
		const SpawnParams& a_params)
	{
		if (a_prefabGUID == Core::DEFAULT_GUID) return false;

		// ハンドルを解決(未ロードならロード)して、参照を1つ取る。
		// 返すのはこのハンドルを持っているコンポーネントの解放フック
		if (!a_resourceManager.IsValid(a_refHandle))
		{
			a_resourceManager.AcquireImmediate(a_refHandle, a_prefabGUID);
		}

		auto* _pPrefab = a_resourceManager.Ref(a_refHandle);
		if (!_pPrefab) return false;

		std::vector<Engine::Resource::PrefabInstanceData> _instanceVec = {};
		if (!BuildSpawnInstanceData(a_world, *_pPrefab, a_params, _instanceVec)) return false;

		// 反復中なので即時生成せず、遅延生成コマンドに積む
		for (auto& _data : _instanceVec)
		{
			a_world.ReserveCreateEntityWithData(_data.sig, std::move(_data.dataMap));
		}
		return true;
	}

	bool SpawnPrefabAt(
		Engine::ECS::World& a_world,
		Engine::Resource::ResourceManager& a_resourceManager,
		const Core::GUID& a_prefabGUID,
		Engine::Handle<Engine::Resource::Prefab>& a_refHandle,
		const Math::Vector3& a_pos)
	{
		// 向きも印も付けない(プレハブの保存値のまま出す)
		SpawnParams _params = {};
		_params.pos = a_pos;

		return SpawnPrefab(a_world, a_resourceManager, a_prefabGUID, a_refHandle, _params);
	}

	//======================================================================================
	// 即時生成
	//======================================================================================
	bool BuildSpawnInstanceData(
		Engine::ECS::World& a_world,
		const Engine::Resource::Prefab& a_prefab,
		const SpawnParams& a_params,
		std::vector<Engine::Resource::PrefabInstanceData>& a_outInstanceVec)
	{
		//------------------------------------------------------------------
		// ルート + 子ぶんの材料を作る
		//------------------------------------------------------------------
		// GUID の振り直しと親子リンクの張り替えはプレハブ側で済んでいる。
		// 先頭がルートで、親が子より前に並んでいる。
		// 子は保存された姿のまま出す(位置と向きは親に追従する)ので、上書きはルートだけ
		//------------------------------------------------------------------
		a_outInstanceVec = a_prefab.BuildInstanceData(&a_world);
		if (a_outInstanceVec.empty()) return false;

		//------------------------------------------------------------------
		// 出したものはシーンに保存しない(子も含めて全部)
		//------------------------------------------------------------------
		// エディターは遊びながら編集するので、印が無いと保存したときに
		// 出したものまでシーンへ書き込まれ、次に開いたときに二重になる
		//------------------------------------------------------------------
		const Engine::ECS::ComponentTypeID _transientTypeID = a_world.GetCompTypeID<Engine::ECS::TransientTag>();
		for (Engine::Resource::PrefabInstanceData& _data : a_outInstanceVec)
		{
			EnsureInstanceComponent(a_world, _data, _transientTypeID);
		}

		Engine::Resource::PrefabInstanceData& _root = a_outInstanceVec[0];

		// 位置と向きを入れる。LocalTransform が無ければ足す
		if (uint8_t* _pBuf = EnsureInstanceComponent(
			a_world, _root, a_world.GetCompTypeID<Component::LocalTransformComponent>()))
		{
			Component::LocalTransformComponent _lt = {};
			std::memcpy(&_lt, _pBuf, sizeof(_lt));
			_lt.pos     = a_params.pos;
			if (a_params.isOverrideRotation) _lt.quat = a_params.quat;
			_lt.isDirty = true;
			std::memcpy(_pBuf, &_lt, sizeof(_lt));
		}

		// 生成元の印。出した側が「自分が出した生存エンティティ」を数えるのに使う
		if (a_params.spawnerGUID.IsValid())
		{
			if (uint8_t* _pBuf = EnsureInstanceComponent(
				a_world, _root, a_world.GetCompTypeID<Component::SpawnerComponent>()))
			{
				Component::SpawnerComponent _spawner = {};
				std::memcpy(&_spawner, _pBuf, sizeof(_spawner));
				_spawner.spawnerGUID = a_params.spawnerGUID;
				_spawner.waveIndex   = a_params.waveIndex;
				std::memcpy(_pBuf, &_spawner, sizeof(_spawner));
			}
		}

		// 追従先(エンティティIDだけで持つ)
		if (a_params.followTarget != Engine::ECS::Limits::INVALID_ENTITY)
		{
			if (uint8_t* _pBuf = EnsureInstanceComponent(
				a_world, _root, a_world.GetCompTypeID<Component::FollowTargetComponent>()))
			{
				Component::FollowTargetComponent _follow = {};
				std::memcpy(&_follow, _pBuf, sizeof(_follow));
				_follow.target = a_params.followTarget;
				std::memcpy(_pBuf, &_follow, sizeof(_follow));
			}
		}

		return true;
	}

	uint8_t* EnsureInstanceComponent(
		Engine::ECS::World& a_world,
		Engine::Resource::PrefabInstanceData& a_data,
		Engine::ECS::ComponentTypeID a_typeID)
	{
		return EnsureComponentBuffer(a_world, a_data.sig, a_data.dataMap, a_typeID);
	}

	Engine::ECS::Entity CreateInstanceNow(
		Engine::ECS::World& a_world,
		std::vector<Engine::Resource::PrefabInstanceData>& a_instanceVec,
		std::vector<Engine::ECS::Entity>* a_pOutEntityVec)
	{
		// 作ったエンティティ(材料と同じ並び)。親子とスロットをIDで付けるのに使う
		std::vector<Engine::ECS::Entity> _entityVec(a_instanceVec.size(), Engine::ECS::Limits::INVALID_ENTITY);

		for (size_t _i = 0; _i < a_instanceVec.size(); ++_i)
		{
			Engine::Resource::PrefabInstanceData& _data = a_instanceVec[_i];

			// 親は先に作り終えているので、ここでIDを入れておく
			LinkParentByIndex(a_world, a_instanceVec, _entityVec, _i);

			// シグネチャで実体を生成(各コンポーネントは既定構築される)
			const Engine::ECS::Entity _entity = a_world.CreateEntity(_data.sig);
			if (_entity == Engine::ECS::Limits::INVALID_ENTITY) continue;

			_entityVec[_i] = _entity;

			// 既定構築済みのところへ、材料の初期値を上書きする
			for (const auto& [_typeID, _buffer] : _data.dataMap)
			{
				if (_buffer.empty()) continue;
				if (!_data.sig.test(_typeID)) continue;

				uint8_t* _pDst = a_world.NRefData(_entity, _typeID);
				if (!_pDst) continue;

				const size_t _size = a_world.GetComponentMetaData(_typeID).compSize;
				std::memcpy(_pDst, _buffer.data(), _size);
			}
		}

		// スロットの相手は自分より後ろに並ぶ(子)ことがあるので、全員を作ってから付ける
		LinkAttachmentSlots(a_world, a_instanceVec, _entityVec);

		if (a_pOutEntityVec) *a_pOutEntityVec = _entityVec;

		return _entityVec.empty() ? Engine::ECS::Limits::INVALID_ENTITY : _entityVec[0];
	}

	Engine::ECS::Entity CreateChildInstanceNow(
		Engine::ECS::World& a_world,
		const Engine::Resource::Prefab& a_prefab,
		Engine::ECS::Entity a_parent)
	{
		if (a_parent == Engine::ECS::Limits::INVALID_ENTITY || !a_world.IsAliveEntity(a_parent))
		{
			return Engine::ECS::Limits::INVALID_ENTITY;
		}

		std::vector<Engine::Resource::PrefabInstanceData> _instanceVec = a_prefab.BuildInstanceData(&a_world);
		if (_instanceVec.empty()) return Engine::ECS::Limits::INVALID_ENTITY;

		const Engine::ECS::ComponentTypeID _transientTypeID = a_world.GetCompTypeID<Engine::ECS::TransientTag>();
		const Engine::ECS::ComponentTypeID _hierarchyTypeID = a_world.GetCompTypeID<Component::HierarchyComponent>();

		// 親の深さ。子の深さはここからの相対で並べ直す
		UINT _parentDepth = 0;
		if (const auto* _pParentHierarchy = a_world.RefData<Component::HierarchyComponent>(a_parent))
		{
			_parentDepth = _pParentHierarchy->depth;
		}

		Core::GUID _parentGUID = Core::DEFAULT_GUID;
		if (const auto* _pParentGUID = a_world.RefData<Engine::ECS::GUIDComponent>(a_parent))
		{
			_parentGUID = _pParentGUID->guid;
		}

		for (size_t _i = 0; _i < _instanceVec.size(); ++_i)
		{
			Engine::Resource::PrefabInstanceData& _data = _instanceVec[_i];

			EnsureInstanceComponent(a_world, _data, _transientTypeID);

			// ルートは親が要るので階層を必ず持たせる。子はプレハブに入っているものをそのまま使う
			uint8_t* _pBuf = (_i == 0)
				? EnsureInstanceComponent(a_world, _data, _hierarchyTypeID)
				: ((_data.sig.test(_hierarchyTypeID) && _data.dataMap.count(_hierarchyTypeID)) ? _data.dataMap[_hierarchyTypeID].data() : nullptr);
			if (!_pBuf) continue;

			Component::HierarchyComponent _hierarchy = {};
			std::memcpy(&_hierarchy, _pBuf, sizeof(_hierarchy));

			if (_i == 0)
			{
				_hierarchy.parentID   = a_parent;
				_hierarchy.parentGUID = _parentGUID;
				_hierarchy.depth      = _parentDepth + 1;
			}
			else
			{
				_hierarchy.depth += _parentDepth + 1;
			}

			std::memcpy(_pBuf, &_hierarchy, sizeof(_hierarchy));
		}

		// 中の親子とスロットは ID で付く(ルートの親は上で入れてある)
		return CreateInstanceNow(a_world, _instanceVec);
	}
}
