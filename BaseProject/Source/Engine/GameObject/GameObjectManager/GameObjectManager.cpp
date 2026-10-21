#include "GameObjectManager.h"

#include "../../ECS/World/World.h"

namespace Engine::GameObject
{
	namespace
	{
		// 読み込んだ1体ぶんの、つなぐ前の親子の情報
		// (親は全員を読み終えてからつなぐ。親が配列で後ろに居ることがあるため)
		struct LoadedLink
		{
			BaseObject* pObject = nullptr;
			Core::GUID parentGUID = {};
			bool isLocalSpace = false;	// 置き場所を親から見た値(ローカル)で保存してあるか
		};
	}

	GameObjectManager::GameObjectManager(Engine::ECS::World* a_pWorld)
	{
		// 以降の初期化フェーズ/Update/Draw/Archive へ渡すコンテキストに載せておく。
		// サービス群はワールドが持っているものをそのまま使う
		// (シーン側で組んだ1つの束を、システムとオブジェクトで共有する)
		m_objContext.pWorld = a_pWorld;
		m_objContext.pServices = a_pWorld ? a_pWorld->RefEngineServices() : nullptr;

		// オブジェクトから他のオブジェクトを引けるようにする
		m_objContext.pObjectManager = this;
	}
	GameObjectManager::~GameObjectManager()
	{
		// 借りているもの(サウンドインスタンス等)を返してから破棄する。
		// プールはアプリ寿命なので、返さないとシーンを跨いで溜まり続ける
		for (auto& _upObject : m_upObjectVec)
		{
			if (_upObject) _upObject->Release(m_objContext);
		}
	}
	void GameObjectManager::PreUpdate()
	{
		// 親が消えるなら子も一緒に消す。
		// 子だけ残すと、親の行列に追従していた置き場所が急に変わってしまうため
		for (const auto& _upObject : m_upObjectVec)
		{
			if (_upObject->IsExpired()) ReserveDestroyDescendants(_upObject.get());
		}

		// 削除処理 : オブジェクト自身が削除命令を出した場合に配列上から削除する
		for (size_t _idx = 0; _idx < m_upObjectVec.size(); ++_idx)
		{
			if (m_upObjectVec[_idx]->IsExpired())
			{
				// 消える前に借りているものを返させる
				m_upObjectVec[_idx]->Release(m_objContext);

				// 親・子から自分を指すポインタを外す(消えた相手を踏ませないため)
				m_upObjectVec[_idx]->DetachFromHierarchy();

				// GUID対応表からも取り除く
				m_guidMap.erase(m_upObjectVec[_idx]->GetGUID());

				std::swap(m_upObjectVec[_idx],m_upObjectVec.back());
				m_upObjectVec.pop_back();
				--_idx;	// swap してきた要素を再チェックする
			}
		}

		// まだ初期化を通していないものをここで通しきる。
		// 消えるものを外した後に呼ぶので、追加してすぐ消されたものへは手を出さない
		RunInitPhases();

		// フレームの番号を進める。
		// 「このフレームのぶんか」を見分けたい側(UI のカーソルの取り合いなど)は、
		// これが変わったのを見て自分の集計を作り直す
		++m_objContext.frameIndex;

		for (auto& _upObject : m_upObjectVec)
		{
			_upObject->PreUpdate(m_objContext);
		}
	}
	void GameObjectManager::Update(float a_dt)
	{
		m_objContext.dt = a_dt;

		for (auto& _object : m_upObjectVec)
		{
			_object->Update(m_objContext);
		}
	}

	void GameObjectManager::Draw(float a_dt)
	{
		m_objContext.dt = a_dt;

		for (auto& _object : m_upObjectVec)
		{
			_object->Draw(m_objContext);
		}
	}

	void GameObjectManager::ReserveDestroyDescendants(BaseObject* a_pObject)
	{
		if (!a_pObject) return;

		for (BaseObject* _pChild : a_pObject->GetChildren())
		{
			if (!_pChild) continue;

			_pChild->ReserveDestroy();
			ReserveDestroyDescendants(_pChild);
		}
	}

	BaseObject* GameObjectManager::Register(std::unique_ptr<BaseObject> a_upObject)
	{
		if (!a_upObject) return nullptr;

		BaseObject* _pObject = a_upObject.get();

		// GUIDが有効なら対応表へ登録
		if (_pObject->GetGUID().IsValid())
		{
			m_guidMap[_pObject->GetGUID()] = _pObject;
		}

		m_upObjectVec.push_back(std::move(a_upObject));

		// 生成直後は1つもフェーズを通っていない
		m_isPendingInit = true;

		return _pObject;
	}

	//======================================================================================
	// 初期化フェーズ
	//======================================================================================
	void GameObjectManager::RunInitPhases()
	{
		// 通していないものが居なければ何もしない
		if (!m_isPendingInit) return;
		m_isPendingInit = false;

		RunInitPhase(EObjectInitPhase::PostDeserialize);
		RunInitPhase(EObjectInitPhase::Awake);
		RunInitPhase(EObjectInitPhase::Start);
	}

	void GameObjectManager::RunInitPhase(EObjectInitPhase a_phase)
	{
		// フェーズの中で新しいオブジェクトが足されることがあるので、
		// 参照を持ち越さず添字で回す(push_back の再確保で壊れるため)。
		// 後ろに積まれたものは、まだ前のフェーズに居れば素通りし、
		// Register が札を立て直しているので次の RunInitPhases で拾われる
		for (size_t _idx = 0; _idx < m_upObjectVec.size(); ++_idx)
		{
			BaseObject* _pObject = m_upObjectVec[_idx].get();
			if (!_pObject) continue;
			if (_pObject->GetInitPhase() != a_phase) continue;

			switch (a_phase)
			{
			case EObjectInitPhase::PostDeserialize:
				_pObject->PostDeserialize(m_objContext);
				_pObject->SetInitPhase(EObjectInitPhase::Awake);
				break;

			case EObjectInitPhase::Awake:
				_pObject->Awake(m_objContext);
				_pObject->SetInitPhase(EObjectInitPhase::Start);
				break;

			case EObjectInitPhase::Start:
				_pObject->Start(m_objContext);
				_pObject->SetInitPhase(EObjectInitPhase::Active);
				break;

			default:
				break;
			}
		}
	}

	BaseObject* GameObjectManager::AddObjectByTypeID(ObjectTypeID a_typeID)
	{
		// クラスメタマネージャーからファクトリで生成
		auto* _pRegistry = m_objContext.pServices ? m_objContext.pServices->pObjectRegistry : nullptr;
		if (!_pRegistry) return nullptr;

		auto _upObject = _pRegistry->Create(a_typeID);
		if (!_upObject)
		{
			return nullptr;
		}

		// 新規GUIDを発行
		Core::GUID _guid = {};
		_guid.Create();
		_upObject->SetGUID(_guid);

		// 追加して初期化(この場で3つのフェーズを通しきる)
		BaseObject* _pObject = Register(std::move(_upObject));
		RunInitPhases();
		return _pObject;
	}

	BaseObject* GameObjectManager::FindByGUID(const Core::GUID& a_guid) const
	{
		auto _it = m_guidMap.find(a_guid);
		if (_it != m_guidMap.end())
		{
			return _it->second;
		}
		return nullptr;
	}

	bool GameObjectManager::IsManaged(const BaseObject* a_pObject) const
	{
		if (!a_pObject) return false;

		// 実体は触らず、持っているポインタと同じアドレスかどうかだけを見る
		for (const auto& _upObject : m_upObjectVec)
		{
			if (_upObject.get() == a_pObject) return true;
		}
		return false;
	}

	void GameObjectManager::Archive(Persistence::Archive& a_ar)
	{
		// クラス情報はワールドのサービスから引く(持ち主は MainEngine)
		auto* _pRegistry = m_objContext.pServices ? m_objContext.pServices->pObjectRegistry : nullptr;
		if (!_pRegistry)
		{
			ENGINE_WARNING("[GameObjectManager] ObjectMetaRegistry が無いため保存・読み込みできません");
			return;
		}
		auto& _registry = *_pRegistry;

		// 保存 : 親の GUID を書くので、先に全員へ GUID を行き渡らせておく。
		// 1体ずつ発行していると、配列で子より後ろに居る親がまだ GUID を持っておらず、
		// 子が空の親を書いてしまう
		if (a_ar.IsSaving())
		{
			for (const auto& _upObject : m_upObjectVec)
			{
				if (_upObject->GetGUID().IsValid()) continue;

				Core::GUID _guid = {};
				_guid.Create();
				_upObject->SetGUID(_guid);
				m_guidMap[_guid] = _upObject.get();
			}
		}

		// 読み込み : 親は全員を読み終えてからつなぐ(親が配列で後ろに居ることがあるため)
		std::vector<LoadedLink> _loadedLinkVec;

		// ------------------------------------------------------------------
		// 配列サイズ : 保存時は現在の個数、読み込み時はファイルから取得
		// ------------------------------------------------------------------
		size_t _count = m_upObjectVec.size();
		if (a_ar.BeginArray("GameObjects", _count))
		{
			for (size_t _i = 0; _i < _count; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;

				if (a_ar.IsSaving())
				{
					// --------------------------------------------------
					// 保存 : タイプID / GUID / データ を書き出す
					// --------------------------------------------------
					BaseObject* _pObject = m_upObjectVec[_i].get();

					// C++型から登録済みタイプIDを引く
					// (登録名のハッシュなので、GameManager::Init の登録順を変えても値は動かない)
					const TypeInfo::TypeChain* _pTypeChain = _pObject->GetTypeChain();
					ObjectTypeID _typeID = _registry.GetTypeID(_pTypeChain->key);

					// 未登録のクラスは読み込み時に復元できない。
					// 黙って書き出すとシーンから消えたようにしか見えないので、ここで気付かせる
					if (_typeID == INVALID_OBJECT_TYPE_ID)
					{
						ENGINE_WARNING("[GameObjectManager] 未登録のクラスを保存しようとしました : %s", std::string(_pTypeChain->name).c_str());
					}

					// GUID は上で全員に発行済み
					Core::GUID _guid = _pObject->GetGUID();

					a_ar.Field("TypeIndex", _typeID);
					a_ar.GUIDField("GUID", _guid);

					// 親。ポインタは保存できないので GUID に直して書く。
					// 派生の Archive を1つずつ直さずに済むよう、ここでまとめて面倒を見る
					Core::GUID _parentGUID = _pObject->GetParent() ? _pObject->GetParent()->GetGUID() : Core::GUID{};
					a_ar.GUIDField("ParentGUID", _parentGUID);

					// 個別データ。1体ぶんを区切りで囲む(読み込み側を参照)
					if (a_ar.BeginGroup("Data"))
					{
						{
							Persistence::ArchiveSection _section(a_ar, "ObjectData");
							_pObject->Archive(a_ar, m_objContext);
						}
						a_ar.EndGroup();
					}

					// 基底のぶん(名前など)
					std::string _name = _pObject->GetName();
					bool _isLocalSpace = true;
					ArchiveBase(a_ar, _name, _isLocalSpace);
				}
				else
				{
					// --------------------------------------------------
					// 読み込み : タイプIDからクラスを復元する
					// --------------------------------------------------
					// シーンに書かれている値(登録名のハッシュ)。
					// 登録名を変えたクラスもあるので、引くのは ResolveTypeID を通す
					uint32_t _savedTypeID = INVALID_OBJECT_TYPE_ID;
					Core::GUID _guid = {};
					Core::GUID _parentGUID = {};
					a_ar.Field("TypeIndex", _savedTypeID);
					a_ar.GUIDField("GUID", _guid);
					a_ar.GUIDField("ParentGUID", _parentGUID);

					ObjectTypeID _typeID = _registry.ResolveTypeID(_savedTypeID);

					// ファクトリで実体を生成
					auto _upObject = _registry.Create(_typeID);
					BaseObject* _pObject = nullptr;
					if (!_upObject)
					{
						ENGINE_WARNING("[GameObjectManager] タイプIDからクラスを復元できませんでした : %u", _savedTypeID);
					}
					else
					{
						// GUIDを復元して登録
						_upObject->SetGUID(_guid);
						_pObject = Register(std::move(_upObject));
					}

					// 保存データの復元。
					// 初期化フェーズは全員を読み終えてから下でまとめて回す
					// (PostDeserialize が既定値で保存値を潰さず、
					//  Start では他のオブジェクトが出揃っているようにするため)
					//
					// 1体ぶんを区切りで囲んであるので(バイナリ)、
					//   ・復元できなかったクラスのぶんは読み飛ばせる
					//   ・1体の読み出しが保存時とずれても、次のオブジェクトへは響かない
					// 区切りを持たない古い .ob* では従来どおり並び順で読む
					if (a_ar.BeginGroup("Data"))
					{
						{
							Persistence::ArchiveSection _section(a_ar, "ObjectData");
							if (_pObject) _pObject->Archive(a_ar, m_objContext);
						}
						a_ar.EndGroup();
					}

					// 基底のぶん(名前など)。持たない古いシーンでは空の名前・ワールドの置き場所のまま
					std::string _name = {};
					bool _isLocalSpace = false;
					ArchiveBase(a_ar, _name, _isLocalSpace);

					if (_pObject)
					{
						_pObject->SetName(_name);
						_loadedLinkVec.push_back({ _pObject, _parentGUID, _isLocalSpace });
					}
				}

				a_ar.EndObject();
			}
			a_ar.EndArray();
		}

		if (a_ar.IsLoading())
		{
			// 全員を並べ終えたので親をつなぐ。
			// 置き場所の値は保存したまま(ローカル)で使うので、ここでは直さない
			for (const LoadedLink& _link : _loadedLinkVec)
			{
				if (!_link.parentGUID.IsValid()) continue;

				BaseObject* _pParent = FindByGUID(_link.parentGUID);
				if (!_pParent)
				{
					ENGINE_WARNING("[GameObjectManager] 親が見つからないので根に置きます : %s (親 %s)",
						_link.pObject->GetDisplayName(), _link.parentGUID.String().c_str());
					continue;
				}
				if (!_link.pObject->SetParent(_pParent, false))
				{
					ENGINE_WARNING("[GameObjectManager] 親子が輪になるので根に置きます : %s", _link.pObject->GetDisplayName());
				}
			}

			//--------------------------------------------------------------
			// 親子が座標を伝える前に保存されたもの(古いシーン)は、置き場所がワールドの値で入っている。
			// そのまま使うと親の分だけずれるので、親から見た値へ直す。
			// 直したものは次の保存からローカルで書かれるので、通るのは一度だけ
			//--------------------------------------------------------------
			std::unordered_set<const BaseObject*> _legacySet;
			for (const LoadedLink& _link : _loadedLinkVec)
			{
				if (!_link.isLocalSpace) _legacySet.insert(_link.pObject);
			}

			if (!_legacySet.empty())
			{
				for (const auto& _upObject : m_upObjectVec)
				{
					if (_upObject->GetParent() == nullptr) RebaseLegacyDescendants(_upObject.get(), _legacySet);
				}
			}

			// 初期化フェーズは全員ぶんを並べ終えてから回す。
			// ここまで来れば FindByGUID で互いを引けるので、Start でのつなぎが成立する
			RunInitPhases();
		}
	}

	//======================================================================================
	// 基底のぶんの保存
	//--------------------------------------------------------------------------------------
	// 後から足したので、1体ぶんの個別データ(Data)の後ろへ区切りで囲んで置く。
	// 区切りを持たない古い .ob* では、ここに次のものの頭が来る。目印が無いので
	// IsLegacyLayout が立ち、何も読まずに既定値のまま残る
	//======================================================================================
	void GameObjectManager::ArchiveBase(Persistence::Archive& a_ar, std::string& a_name, bool& a_isLocalSpace)
	{
		if (!a_ar.BeginGroup("Base")) return;
		{
			Persistence::ArchiveSection _section(a_ar, "BaseObject");
			if (!a_ar.IsLegacyLayout())
			{
				a_ar.StringField("Name", a_name);

				// 置き場所を親から見た値で書いたか。これを持たないシーンはワールドの値で入っている
				a_ar.Field("IsLocalSpace", a_isLocalSpace);
			}
		}
		a_ar.EndGroup();
	}

	//======================================================================================
	// 古いシーンの置き場所を、親から見た値へ直す
	//--------------------------------------------------------------------------------------
	// 根から順に下りる。子を直すときには親がもう直してあるので、
	// 親の CalcWorldMatrix がこれからの(直した後の)ワールド行列になる。
	// 古い値はワールドそのものなので、「これまでの親」は単位行列として渡す
	//======================================================================================
	void GameObjectManager::RebaseLegacyDescendants(BaseObject* a_pObject, const std::unordered_set<const BaseObject*>& a_legacySet)
	{
		if (!a_pObject) return;

		const Math::Matrix _parentWorld = a_pObject->CalcWorldMatrix();

		for (BaseObject* _pChild : a_pObject->GetChildren())
		{
			if (!_pChild) continue;

			if (a_legacySet.contains(_pChild)) _pChild->RebaseLocal(Math::Matrix::Identity(), _parentWorld);

			RebaseLegacyDescendants(_pChild, a_legacySet);
		}
	}
}
