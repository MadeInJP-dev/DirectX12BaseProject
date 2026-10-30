#include "World.h"

#include "Profile/ECSWorldProfiler.h"

namespace Engine::ECS
{
	World::World()
	{}

	World::~World()
	{}

	//==============================================================================================
	// ワールドの寿命
	//==============================================================================================

	void World::Init(ComponentMetaRegistry* a_pComponentRegistry)
	{
		ENGINE_ERRLOG(a_pComponentRegistry, "型情報(ComponentMetaRegistry)が渡されていません");
		m_pComponentRegistry = a_pComponentRegistry;

		m_storage.Init(m_pComponentRegistry, &m_engineServices);
		m_systemManager.Init();

		m_isInit = true;
	}

	bool World::IsInit()
	{
		return m_isInit;
	}

	//----------------------------------------------------------------------------------------------
	// フレームの先頭処理
	//
	// 基盤がやるのは、1フレームの間に積まれた予約を捌くところまで。
	//   システムのソート → 生成 → 引っ越し → 削除 → 作り直し
	// 初期化フェーズの進行などは、それを持つ層(App::ECS::APPWorld)が override して差し込む
	//----------------------------------------------------------------------------------------------
	void World::BeginFrame()
	{
		m_systemManager.Sort();

		ApplyReservedCreate();
		ApplyReservedChange();
		ApplyReservedRemove();

		// 作り直しはシグネチャの変更として積まれるので、続けて流す
		ApplyReservedRefresh();
		ApplyReservedChange();
	}

	//----------------------------------------------------------------------------------------------
	// 解放
	//
	// 借りているものは削除時に解放フック(ComponentTraits<T>::Release)が返すので、
	// ここでリソースを数え直す必要はない。
	// 参照が 0 になった実体を捨てるのはシーンの切れ目(ResourceManager::SweepUnusedAll)
	//----------------------------------------------------------------------------------------------
	void World::Release()
	{
		// 積まれたままの引っ越しを片付けてから消す
		ApplyReservedChange();

		for (const auto& _loca : m_storage.GetAllEntityLocation())
		{
			if (!_loca.pChunk) continue;
			ReserveRemoveEntity(_loca.pChunk->entityData[_loca.chunkIndex]);
		}
		ApplyReservedRemove();

		ENGINE_LOG("Worldの解放");
	}

	//==============================================================================================
	// エンティティ : 参照
	//==============================================================================================

	const std::vector<EntityLocation>& World::GetEntityList() const
	{
		return m_storage.GetAllEntityLocation();
	}

	UINT World::GetAliveEntityCount() const
	{
		return m_storage.GetAliveEntityCount();
	}

	bool World::IsAliveEntity(const Entity& a_entity)
	{
		return m_storage.IsAlive(a_entity);
	}

	const EntityLocation& World::GetLocation(const Entity& a_entity) const
	{
		return m_storage.GetLocation(a_entity);
	}

	const Entity& World::GetEntity(const EntityLocation& a_location) const
	{
		if (!a_location.pChunk) return Limits::INVALID_ENTITY;

		return a_location.pChunk->entityData[a_location.chunkIndex];
	}

	Entity World::GetEntity(const Core::GUID& a_guid)
	{
		// 基盤のエンティティは番号でしかなく、保存をまたいで残る識別子は持たない
		(void)a_guid;
		return Limits::INVALID_ENTITY;
	}

	Signature World::GetSignature(const Entity& a_entity) const
	{
		return m_storage.GetSignature(a_entity);
	}

	bool World::HasComponent(const Entity& a_entity, const ComponentTypeID& a_comptype)
	{
		// 未登録の型(INVALID)のまま test するとシグネチャの範囲外で例外になる
		if (!IsValidTypeID(a_comptype)) return false;

		return m_storage.GetSignature(a_entity).test(a_comptype);
	}

	//==============================================================================================
	// エンティティ : 生成・削除
	//==============================================================================================

	void World::ReserveCreateEntity(const Signature& a_sig)
	{
		m_commandBuffer.ReserveCreate(a_sig);
	}

	void World::ReserveCreateEntityWithData(const Signature& a_sig, ComponentDataMap a_dataMap)
	{
		m_commandBuffer.ReserveCreateWithData({
			.sig = a_sig,
			.dataMap = std::move(a_dataMap),
		});
	}

	Entity World::CreateEntity(const Signature& a_sig)
	{
		// 必ず一緒に持たせるもの(ComponentTraits<T>::Requires)を足す
		Signature _sig = a_sig;
		m_pComponentRegistry->ExpandRequired(_sig);

		// 生まれた直後に何を載せるかは派生が決める(初期化フェーズのタグなど)
		OnCreateEntitySignature(_sig);

		return m_storage.Create(_sig);
	}

	void World::ReserveReleaseEntity(const Entity& a_entity)
	{
		if (a_entity == Limits::INVALID_ENTITY) return;

		// 基盤には後始末の工程が無いので、そのまま削除を予約する
		ReserveRemoveEntity(a_entity);
	}

	//==============================================================================================
	// エンティティ : 構成の変更
	//==============================================================================================

	void World::ReserveAddComponent(ComponentTypeID a_typeID, Entity a_entity, uint8_t* a_pData)
	{
		Signature _toSig = m_storage.GetSignature(a_entity);
		if (_toSig.test(a_typeID)) return;		// すでに持っている
		_toSig.set(a_typeID);

		// 構成が変わったので初期化からやり直させる(判断と中身は派生が持つ)
		OnReenterInitSignature(_toSig);

		ChangeEntityCmd _cmd = {};
		_cmd.entity = a_entity;
		_cmd.toSig = _toSig;

		// 初期値はディープコピーして持つ
		const size_t _size = m_pComponentRegistry->GetMetaData(a_typeID).compSize;
		if (a_pData)
		{
			_cmd.dataMap[a_typeID] = std::vector<uint8_t>(a_pData, a_pData + _size);
		}
		else
		{
			// 渡されなければ既定値で構築しておく。
			// 空のままだとチャンクの生メモリ(ゼロ)で始まり、メンバ初期化子が効かない。
			// インスペクタからの追加はデータを渡さないので必ずここを通る
			const auto& _construct = GetCompFunc(a_typeID).construct;
			if (_construct)
			{
				std::vector<uint8_t> _buffer(_size);
				_construct(_buffer.data());
				_cmd.dataMap[a_typeID] = std::move(_buffer);
			}
		}

		m_commandBuffer.ReserveChange(std::move(_cmd));
	}

	void World::ReserveRemoveComponent(ComponentTypeID a_typeID, Entity a_entity)
	{
		Signature _toSig = m_storage.GetSignature(a_entity);
		if (!_toSig.test(a_typeID)) return;		// 持っていない
		_toSig.reset(a_typeID);

		ReserveChangeSignature({
			.entity = a_entity,
			.toSig = _toSig,
		});
	}

	void World::ReserveChangeSignature(ChangeEntityCmd a_cmd)
	{
		m_commandBuffer.ReserveChange(std::move(a_cmd));
	}

	void World::ReserveRefreshEntity(const Entity& a_entity)
	{
		// 実体の無い呼び出し(プレハブ編集など)は、反映時に範囲外を引くので弾く
		if (a_entity == Limits::INVALID_ENTITY) return;

		m_commandBuffer.ReserveRefresh(a_entity);
	}

	void World::ApplyReservedChange()
	{
		if (!m_commandBuffer.HasChange()) return;

		const auto _cmdVec = m_commandBuffer.TakeChange();
		for (const auto& _cmd : _cmdVec)
		{
			ChangeSignature(_cmd);
		}
		if (m_upProfiler) m_upProfiler->RecordChanged(_cmdVec.size());

		OnEntityStructureChanged();
	}

	//==============================================================================================
	// コンポーネント : 型情報
	//==============================================================================================

	ComponentTypeID World::GetCompTypeID(const std::string& a_name) const
	{
		return m_pComponentRegistry->GetTypeID(a_name);
	}

	const ComponentMeta& World::GetComponentMetaData(const ComponentTypeID& a_typeID) const
	{
		return m_pComponentRegistry->GetMetaData(a_typeID);
	}

	const std::vector<ComponentMeta>& World::GetAllComponentMetaData() const
	{
		return m_pComponentRegistry->GetAllMetaData();
	}

	std::vector<std::string> World::GetComponentNames(const Signature& a_sig) const
	{
		// タイプIDの順(= 登録順)に並ぶ。読み込みは名前で引き直すので順番に意味は無い
		const auto& _metaVec = m_pComponentRegistry->GetAllMetaData();

		std::vector<std::string> _names = {};
		for (ComponentTypeID _typeID = 0; _typeID < _metaVec.size(); ++_typeID)
		{
			if (a_sig.test(_typeID))
			{
				_names.push_back(_metaVec[_typeID].name);
			}
		}
		return _names;
	}

	const ComponentFunc& World::GetCompFunc(const ComponentTypeID& a_typeID) const
	{
		return m_pComponentRegistry->GetFunc(a_typeID);
	}

	//==============================================================================================
	// コンポーネント : データ
	//==============================================================================================

	uint8_t* World::NRefData(const Entity& a_entity, const ComponentTypeID& a_typeID)
	{
		return m_storage.RefComponent(a_entity, a_typeID);
	}

	//==============================================================================================
	// システム
	//==============================================================================================

	void World::RunSystem(ESystemType a_type, float a_dt)
	{
		// システムは無捕獲なので、必要な参照はすべてこのコンテキストから取る
		SystemContext _context = {};
		_context.pWorld = this;
		_context.pServices = &m_engineServices;
		_context.dt = a_dt;

		m_systemManager.RunSystem(a_type, _context, m_upProfiler.get());
	}

	//==============================================================================================
	// プロファイラ
	//==============================================================================================

	void World::EnableProfiler()
	{
		if (m_upProfiler) return;
		m_upProfiler = std::make_unique<ECSWorldProfiler>(this);
	}

	void World::DisableProfiler()
	{
		m_upProfiler.reset();
	}

	const std::unordered_map<ESystemType, std::vector<SystemTask*>>& World::GetCompileTaskMap() const
	{
		return m_systemManager.GetCompileTaskMap();
	}

	//==============================================================================================
	// 派生へのフック
	//==============================================================================================

	void World::ApplyReservedRefresh()
	{
		// 基盤には作り直しの工程が無いので捨てるだけ。
		// 初期化フェーズを持つ層が override して、後始末を通してから初期化へ戻す
		m_commandBuffer.TakeRefresh();
	}

	//==============================================================================================
	// 予約の反映・即時操作
	//==============================================================================================

	void World::ApplyReservedCreate()
	{
		// 遅いフレームの記録 : 一度にたくさん作ったフレームを見分ける
		size_t _createdNum = 0;
		ENGINE_HITCH_SCOPE("ECS", 0.3, "予約の生成を " + std::to_string(_createdNum) + " 体反映");

		const auto _sigVec = m_commandBuffer.TakeCreate();
		for (const auto& _sig : _sigVec)
		{
			CreateEntity(_sig);
			OnEntityStructureChanged();
		}

		const auto _cmdVec = m_commandBuffer.TakeCreateWithData();
		_createdNum = _sigVec.size() + _cmdVec.size();
		if (m_upProfiler) m_upProfiler->RecordCreated(_createdNum);

		for (const auto& _cmd : _cmdVec)
		{
			const Entity _entity = CreateEntity(_cmd.sig);
			if (_entity == Limits::INVALID_ENTITY) continue;

			m_storage.WriteComponentData(_entity, _cmd.dataMap);
			OnEntityStructureChanged();
		}
	}

	void World::ApplyReservedRemove()
	{
		const auto _entityVec = m_commandBuffer.TakeRemove();
		if (m_upProfiler) m_upProfiler->RecordRemoved(_entityVec.size());

		// 遅いフレームの記録 : 一度にたくさん消したフレーム(ウェーブの終わりなど)を見分ける
		ENGINE_HITCH_SCOPE("ECS", 0.3, "予約の削除を " + std::to_string(_entityVec.size()) + " 体反映");

		for (const auto& _entity : _entityVec)
		{
			RemoveEntity(_entity);
			OnEntityStructureChanged();
		}
	}

	void World::ReserveRemoveEntity(const Entity& a_entity)
	{
		m_commandBuffer.ReserveRemove(a_entity);
	}

	void World::RemoveEntity(const Entity& a_entity)
	{
		// 借りているものは EntityStorage が解放フックを呼んで返す
		m_storage.Destroy(a_entity);
	}

	void World::ChangeSignature(const ChangeEntityCmd& a_cmd)
	{
		// 予約した後に消えたエンティティ(古いID)は動かしようがない
		if (!m_storage.IsAlive(a_cmd.entity)) return;

		// 必ず一緒に持たせるもの(ComponentTraits<T>::Requires)を足す。
		// 追加で増えたものは既定値で構築される(RestoreComponents)
		Signature _toSig = a_cmd.toSig;
		m_pComponentRegistry->ExpandRequired(_toSig);

		// 初期化へ入り直すなら、直後に取り直されるので今持っているものは全部返させる。
		// 入り直すかどうかの判断は派生が持つ(基盤はフェーズを知らない)
		const bool _isReenteringInit = IsReenteringInit(m_storage.GetSignature(a_cmd.entity), _toSig);

		m_storage.Move(a_cmd.entity, _toSig, a_cmd.dataMap, _isReenteringInit);
	}
}
