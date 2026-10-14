#include "Prefab.h"


#include "../../../ECS/World/World.h"
#include "../../../Scene/SceneManager/SceneManager.h"

#include "../../Manager/AssetDatabase/AssetDatabase.h"
#include "../../Manager/ResourceManager/ResourceManager.h"

// 実体化のたびにGUIDを振り直すために触る。
// (World.h がフェーズタグを取り込んでいるのと同じで、ここも土台側のコンポーネント)
#include "Engine/ECS/Component/GUIDComponent.h"

namespace Engine::Resource
{
	Prefab::Prefab()
	{
		m_sigunature = {};
		m_dataMap.clear();
	}

	//======================================================================================
	// コンポーネント操作
	//======================================================================================
	bool Prefab::ResolveNode(
		int a_nodeIndex,
		ECS::Signature*& a_outSig,
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>*& a_outDataMap)
	{
		if (a_nodeIndex == ROOT_NODE)
		{
			a_outSig     = &m_sigunature;
			a_outDataMap = &m_dataMap;
			return true;
		}

		if (!IsValidNode(a_nodeIndex)) return false;

		PrefabChild& _child = m_children[static_cast<size_t>(a_nodeIndex)];
		a_outSig     = &_child.sig;
		a_outDataMap = &_child.dataMap;
		return true;
	}

	bool Prefab::IsValidNode(int a_nodeIndex) const
	{
		if (a_nodeIndex == ROOT_NODE) return true;
		return a_nodeIndex >= 0 && static_cast<size_t>(a_nodeIndex) < m_children.size();
	}

	void Prefab::AddComponentDefault(ECS::World* a_pWorld, ECS::ComponentTypeID a_compTypeID, int a_nodeIndex)
	{
		if (!a_pWorld) return;

		ECS::Signature* _pSig = nullptr;
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>* _pDataMap = nullptr;
		if (!ResolveNode(a_nodeIndex, _pSig, _pDataMap)) return;

		// コンポーネントのサイズ分バッファを確保して既定値で構築する
		const auto& _meta = a_pWorld->GetComponentMetaData(a_compTypeID);

		auto& _buffer = (*_pDataMap)[a_compTypeID];
		_buffer.assign(_meta.compAlignSize, 0);

		auto _construct = a_pWorld->GetCompFunc(a_compTypeID).construct;
		if (_construct) _construct(_buffer.data());

		_pSig->set(a_compTypeID);
	}

	void Prefab::AddComponentData(ECS::World* a_pWorld, ECS::ComponentTypeID a_compTypeID, const uint8_t* a_pSrc)
	{
		if (!a_pWorld || !a_pSrc) return;

		// コンポーネント全体をコピーして保持する
		const auto& _meta = a_pWorld->GetComponentMetaData(a_compTypeID);

		auto& _buffer = m_dataMap[a_compTypeID];
		_buffer.assign(a_pSrc, a_pSrc + _meta.compAlignSize);

		m_sigunature.set(a_compTypeID);
	}

	void Prefab::RemoveComponent(ECS::ComponentTypeID a_compTypeID, int a_nodeIndex)
	{
		ECS::Signature* _pSig = nullptr;
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>* _pDataMap = nullptr;
		if (!ResolveNode(a_nodeIndex, _pSig, _pDataMap)) return;

		_pSig->reset(a_compTypeID);
		_pDataMap->erase(a_compTypeID);
	}

	void Prefab::PasteSignatureAndData(
		const ECS::Signature& a_sig,
		const std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>& a_dataMap)
	{
		// まるごと差し替える
		m_sigunature = a_sig;

		// データはシグネチャに載っているものだけを引き継ぐ。
		// (コピー元で削除済みのバッファが残っていても持ち込まないようにする)
		m_dataMap.clear();
		for (const auto& [_typeID, _buffer] : a_dataMap)
		{
			if (!a_sig.test(_typeID)) continue;
			if (_buffer.empty()) continue;

			m_dataMap[_typeID] = _buffer;
		}
	}

	bool Prefab::Has(ECS::ComponentTypeID a_compTypeID, int a_nodeIndex) const
	{
		return GetSignature(a_nodeIndex).test(a_compTypeID);
	}

	uint8_t* Prefab::RefData(ECS::ComponentTypeID a_compTypeID, int a_nodeIndex)
	{
		ECS::Signature* _pSig = nullptr;
		std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>* _pDataMap = nullptr;
		if (!ResolveNode(a_nodeIndex, _pSig, _pDataMap)) return nullptr;

		auto _it = _pDataMap->find(a_compTypeID);
		if (_it == _pDataMap->end() || _it->second.empty()) return nullptr;
		return _it->second.data();
	}

	const ECS::Signature& Prefab::GetSignature(int a_nodeIndex) const
	{
		static const ECS::Signature EMPTY_SIG = {};

		if (a_nodeIndex == ROOT_NODE) return m_sigunature;
		if (!IsValidNode(a_nodeIndex)) return EMPTY_SIG;
		return m_children[static_cast<size_t>(a_nodeIndex)].sig;
	}

	const std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>>& Prefab::GetDataMap(int a_nodeIndex) const
	{
		static const std::unordered_map<ECS::ComponentTypeID, std::vector<uint8_t>> EMPTY_DATA_MAP = {};

		if (a_nodeIndex == ROOT_NODE) return m_dataMap;
		if (!IsValidNode(a_nodeIndex)) return EMPTY_DATA_MAP;
		return m_children[static_cast<size_t>(a_nodeIndex)].dataMap;
	}

	//======================================================================================
	// ノード(子)の操作
	//======================================================================================
	const Core::GUID& Prefab::RequestNodeGUID(int a_nodeIndex)
	{
		static const Core::GUID INVALID_GUID = {};

		if (!IsValidNode(a_nodeIndex)) return INVALID_GUID;

		Core::GUID& _guid = (a_nodeIndex == ROOT_NODE)
			? m_savedGUID
			: m_children[static_cast<size_t>(a_nodeIndex)].savedGUID;

		if (!_guid.IsValid()) _guid.Create();
		return _guid;
	}

	//--------------------------------------------------------------------------------------
	// 子孫ごと取り除く
	//
	// 親は子より前に並んでいるので、消す印は前から一度舐めれば子孫まで伝わる。
	// 並び順はそのまま詰めるので「親が子より前」は崩れない。
	//--------------------------------------------------------------------------------------
	size_t Prefab::RemoveChildTree(int a_childIndex)
	{
		if (a_childIndex == ROOT_NODE || !IsValidNode(a_childIndex)) return 0;

		const size_t _count = m_children.size();
		std::vector<bool> _isRemoveVec(_count, false);
		_isRemoveVec[static_cast<size_t>(a_childIndex)] = true;

		for (size_t _i = static_cast<size_t>(a_childIndex) + 1; _i < _count; ++_i)
		{
			const int _parent = m_children[_i].parentIndex;
			if (_parent >= 0 && _isRemoveVec[static_cast<size_t>(_parent)]) _isRemoveVec[_i] = true;
		}

		// 古い添え字 → 詰めた後の添え字
		std::vector<int> _newIndexVec(_count, ROOT_NODE);
		std::vector<PrefabChild> _remainVec = {};
		_remainVec.reserve(_count);

		for (size_t _i = 0; _i < _count; ++_i)
		{
			if (_isRemoveVec[_i]) continue;

			_newIndexVec[_i] = static_cast<int>(_remainVec.size());
			_remainVec.push_back(std::move(m_children[_i]));
		}

		// 残ったものの親は消えていない(消える親の子は一緒に消えている)
		for (PrefabChild& _child : _remainVec)
		{
			if (_child.parentIndex >= 0) _child.parentIndex = _newIndexVec[static_cast<size_t>(_child.parentIndex)];
		}

		const size_t _removeCount = _count - _remainVec.size();
		m_children = std::move(_remainVec);
		return _removeCount;
	}

	//======================================================================================
	// 保存 / 読み込み
	//======================================================================================
	void Prefab::Save(ECS::World* a_pWorld, const std::string& a_savePath)
	{
		//------------------------------------------------------------------------------
		// ワールドが無いと何も書けない
		//
		// コンポーネント名はワールドのメタ情報から引くので、ワールドが無いと
		// 名前が1つも出ず「コンポーネントを持たないプレハブ」を書き出してしまう。
		// 書き出し先は既存のアセットなので、それは中身の消去になる。
		//------------------------------------------------------------------------------
		if (!a_pWorld)
		{
			ENGINE_WARNING("[Prefab] ワールドが無いので保存しません(中身が消えるため) : %s", a_savePath.c_str());
			return;
		}

		auto _dir = Core::File::GetDirFromPath(a_savePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_savePath);

		// 保存先ディレクトリが無ければ作成しておく
		if (!_dir.empty())
		{
			std::error_code _ec;
			std::filesystem::create_directories(_dir, _ec);
		}

		Persistence::Archive _arch(Persistence::Archive::EMode::Save, _dir, _fileName, "prfb");
		Archive(_arch, a_pWorld);
	}

	void Prefab::Load(ECS::World* a_pWorld, const std::string& a_filePath)
	{
		// 一旦クリアしてから読み込む。
		// 子と鍵も空にする : 子を持たないファイルは Children を読まないので、
		// 読み直し(エディターの Revert)で前の子が残ってしまう
		m_sigunature = {};
		m_dataMap.clear();
		m_savedGUID = {};
		m_children.clear();

		auto _dir = Core::File::GetDirFromPath(a_filePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_filePath);

		// 形式はビルドモード任せ(Auto)。
		// Development までは .ojprfb があればそちらを読むので、コンポーネントに
		// フィールドを足しても保存済みのプレハブが壊れない。Shipping は .obprfb のみ
		Persistence::Archive _arch(
			Persistence::Archive::EMode::Load, _dir, _fileName, "prfb");

		Archive(_arch, a_pWorld);
	}

	//======================================================================================
	// 実体化の材料を組み立てる
	//--------------------------------------------------------------------------------------
	// ノードごとに新しいGUIDを振り、保存時GUIDへの参照を全部そちらへ張り替える。
	//
	// 張り替えはバイト列を GUID(16バイト)単位で走査して行う。
	// 参照を持つコンポーネント(親リンク・アタッチメント・ターゲット指定など)を
	// ここで型として列挙すると、参照を持つコンポーネントが増えるたびに
	// このリソースを直す羽目になる。GUID は乱数なので、保存したエンティティの
	// GUID と偶然一致するバイト列が別の意味で入っていることは実質ない。
	// アセットのGUID(モデルや音)は対応表に載っていないので触られない。
	//======================================================================================
	std::vector<PrefabInstanceData> Prefab::BuildInstanceData(ECS::World* a_pWorld) const
	{
		std::vector<PrefabInstanceData> _instanceVec = {};
		if (!a_pWorld) return _instanceVec;

		_instanceVec.reserve(m_children.size() + 1);

		// ---- ルート ----
		PrefabInstanceData _root = {};
		_root.sig     = m_sigunature;
		_root.dataMap = m_dataMap;
		_instanceVec.push_back(std::move(_root));

		// ---- 子 ----
		// 親の位置はプレハブ内の添え字(-1 = ルート)から、この並びの添え字へ直す
		for (const PrefabChild& _child : m_children)
		{
			PrefabInstanceData _data = {};
			_data.sig         = _child.sig;
			_data.dataMap     = _child.dataMap;
			_data.parentIndex = _child.parentIndex + 1;
			_instanceVec.push_back(std::move(_data));
		}

		//----------------------------------------------------------------------
		// 保存時GUID → 新しいGUID の対応表を作る
		//----------------------------------------------------------------------
		std::unordered_map<Core::GUID, Core::GUID> _guidMap = {};

		std::vector<Core::GUID> _newGUIDVec(_instanceVec.size());
		for (size_t _i = 0; _i < _newGUIDVec.size(); ++_i)
		{
			_newGUIDVec[_i].Create();
			_instanceVec[_i].guid = _newGUIDVec[_i];
		}

		if (m_savedGUID.IsValid()) _guidMap[m_savedGUID] = _newGUIDVec[0];

		for (size_t _i = 0; _i < m_children.size(); ++_i)
		{
			const Core::GUID& _savedGUID = m_children[_i].savedGUID;
			if (!_savedGUID.IsValid()) continue;

			_guidMap[_savedGUID] = _newGUIDVec[_i + 1];
		}

		//----------------------------------------------------------------------
		// 参照の張り替えと、自分自身のGUIDの書き込み
		//----------------------------------------------------------------------
		const ECS::ComponentTypeID _guidTypeID = a_pWorld->GetCompTypeID<Engine::ECS::GUIDComponent>();

		for (size_t _i = 0; _i < _instanceVec.size(); ++_i)
		{
			for (auto& [_typeID, _buffer] : _instanceVec[_i].dataMap)
			{
				if (_buffer.empty()) continue;
				RemapGUIDs(_buffer.data(), _buffer.size(), _guidMap);
			}

			// 自分のGUIDは対応表に載っていない場合(ルートは空で保存している)があるので、
			// ここで確実に新しいものを入れておく。子から親を引けるようにするため
			if (_guidTypeID == ECS::Limits::INVALID_COMPONENTTYPEID) continue;
			if (!_instanceVec[_i].sig.test(_guidTypeID)) continue;

			auto _it = _instanceVec[_i].dataMap.find(_guidTypeID);
			if (_it == _instanceVec[_i].dataMap.end() || _it->second.size() < sizeof(Engine::ECS::GUIDComponent))
			{
				continue;
			}

			Engine::ECS::GUIDComponent _guidComp = {};
			std::memcpy(&_guidComp, _it->second.data(), sizeof(_guidComp));
			_guidComp.guid = _newGUIDVec[_i];
			std::memcpy(_it->second.data(), &_guidComp, sizeof(_guidComp));
		}

		return _instanceVec;
	}

	//======================================================================================
	// バイト列に残っている GUID を張り替える
	//--------------------------------------------------------------------------------------
	// GUID(UUID)の先頭は 4 バイト境界に載るので、4 バイト刻みで見れば取りこぼさない。
	//======================================================================================
	void Prefab::RemapGUIDs(
		uint8_t* a_pData, size_t a_size,
		const std::unordered_map<Core::GUID, Core::GUID>& a_guidMap)
	{
		if (!a_pData || a_guidMap.empty()) return;
		if (a_size < sizeof(Core::GUID)) return;

		constexpr size_t STRIDE = 4;
		const size_t _end = a_size - sizeof(Core::GUID);

		for (size_t _offset = 0; _offset <= _end; _offset += STRIDE)
		{
			Core::GUID _guid = {};
			std::memcpy(&_guid, a_pData + _offset, sizeof(_guid));

			// 未設定(全ゼロ)は「参照なし」の意味なので触らない
			if (!_guid.IsValid()) continue;

			auto _it = a_guidMap.find(_guid);
			if (_it == a_guidMap.end()) continue;

			std::memcpy(a_pData + _offset, &_it->second, sizeof(_it->second));
		}
	}

	//======================================================================================
	// 実体化
	//======================================================================================
	ECS::Entity Prefab::Instantiate(ECS::World* a_pWorld)
	{
		if (!a_pWorld) return ECS::Limits::INVALID_ENTITY;

		// ルート + 子ぶんの材料(GUIDは振り直し済み)
		std::vector<PrefabInstanceData> _instanceVec = BuildInstanceData(a_pWorld);
		if (_instanceVec.empty()) return ECS::Limits::INVALID_ENTITY;

		ECS::Entity _rootEntity = ECS::Limits::INVALID_ENTITY;

		for (size_t _i = 0; _i < _instanceVec.size(); ++_i)
		{
			PrefabInstanceData& _data = _instanceVec[_i];

			// シグネチャで実体を生成(各コンポーネントは既定構築される)
			ECS::Entity _entity = a_pWorld->CreateEntity(_data.sig);
			if (_entity == ECS::Limits::INVALID_ENTITY) continue;

			if (_i == 0) _rootEntity = _entity;

			// 保存済みの初期値を各コンポーネントへ流し込む
			// (ハンドル等のランタイム値は既定のまま。GUID から PostDeserialize 系システムが復元する。
			//  親子リンクも parentGUID を張り替えてあるので HierarchyLinkSystem が繋ぎ直す)
			for (auto& [_typeID, _buffer] : _data.dataMap)
			{
				if (_buffer.empty()) continue;
				if (!_data.sig.test(_typeID)) continue;

				uint8_t* _dst = a_pWorld->NRefData(_entity, _typeID);
				if (!_dst) continue;

				size_t _size = a_pWorld->GetComponentMetaData(_typeID).compSize;
				std::memcpy(_dst, _buffer.data(), _size);
			}
		}

		return _rootEntity;
	}

	//======================================================================================
	// ローダー / 生成
	//======================================================================================
	Prefab Prefab::LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
	{
		Prefab _prefab;

		// コンポーネントのメタ情報が必要なので World を取得する
		auto* _pSceneManager = a_pContext ? a_pContext->pSceneManager : nullptr;
		ECS::World* _pWorld = _pSceneManager ? _pSceneManager->RefWorld() : nullptr;
		if (_pWorld && _pWorld->IsInit())
		{
			_prefab.Load(_pWorld, a_path);
			return _prefab;
		}

		//----------------------------------------------------------------------------------
		// ワールドが引けないと中身を読めない(空のプレハブが返る)
		//
		// 返した空の実体はキャッシュに残るので、そのままだと
		//   ・実体化しても何も乗っていないエンティティが出る
		//   ・エディターのインスペクターにも空のまま出るので、保存するとアセットが消える
		// 黙って空を返すのがいちばん危ないため、必ず知らせる。
		//----------------------------------------------------------------------------------
		ENGINE_WARNING("[Prefab] ワールドが無いので読み込めませんでした : %s", a_path.c_str());

		return _prefab;
	}

	void Prefab::Create(AssetDatabase& a_assetDB, ECS::World* a_pWorld, const std::string& a_path, const std::string& a_name)
	{
		static const std::string ASSET_DIR = "Asset/Prefab/";
		auto _basePath = ASSET_DIR + a_path + "/" + a_name;

		// すでに存在するなら作らない
		Core::GUID _checkGUID = a_assetDB.GetGUIDFromFilePath(_basePath);
		if (_checkGUID != Core::DEFAULT_GUID)
		{
			ENGINE_LOG("すでに作成されたプレハブです : %s", _basePath.c_str());
			return;
		}

		// 書き出すだけでよい。
		// メタファイルとGUIDは、AssetDatabase の監視が新しいファイルを見つけて用意する
		// 空のプレハブを保存する
		Prefab _prefab;
		_prefab.Save(a_pWorld, _basePath);
	}

	//======================================================================================
	// シリアライズ処理
	//======================================================================================
	void Prefab::Archive(Persistence::Archive& a_ar, ECS::World* a_pWorld)
	{
		std::vector<std::string> _compNames = {};

		// 【セーブ時のみ】シグネチャからコンポーネント名リストを作成
		if (a_ar.GetMode() == Persistence::Archive::EMode::Save)
		{
			if (a_pWorld)
			{
				_compNames = a_pWorld->GetComponentNames(m_sigunature);
			}
		}

		// コンポーネント名リストのアーカイブ
		a_ar.VectorField("ComponentNames", _compNames);

		// 【ロード時のみ】読み込んだ名前からシグネチャを作り、既定値バッファを確保
		if (a_ar.GetMode() == Persistence::Archive::EMode::Load)
		{
			if (!a_pWorld) return;

			for (const std::string& _name : _compNames)
			{
				ECS::ComponentTypeID _typeID = a_pWorld->GetCompTypeID(_name);
				if (_typeID == ECS::Limits::INVALID_COMPONENTTYPEID)
				{
					// 名前を引けないものは落ちる。そのまま保存すると
					// プレハブからそのコンポーネントが消えるので知らせておく
					// (コンポーネントの登録漏れ / 登録名の変更)
					ENGINE_WARNING("[Prefab] 未登録のコンポーネントを読み飛ばしました : %s", _name.c_str());
					continue;
				}

				// バッファ確保 + 既定構築(このあと各データで上書きされる)
				AddComponentDefault(a_pWorld, _typeID);
			}
		}

		// ---------------------------------------------------------
		// 各コンポーネントデータのシリアライズ
		// ---------------------------------------------------------
		if (!a_pWorld) return;

		for (const std::string& _name : _compNames)
		{
			ECS::ComponentTypeID _typeID = a_pWorld->GetCompTypeID(_name);
			if (_typeID == ECS::Limits::INVALID_COMPONENTTYPEID) continue;

			auto _it = m_dataMap.find(_typeID);
			if (_it == m_dataMap.end() || _it->second.empty()) continue;

			auto _func = a_pWorld->GetCompFunc(_typeID).archive;
			if (_func)
			{
				// セーブもロードも同じグループ構造で実行
				if (a_ar.BeginGroup(_name))
				{
					_func(a_ar, _it->second.data());
					a_ar.EndGroup();
				}
			}
		}

		//----------------------------------------------------------------------
		// ルートのGUIDと子エンティティ
		//----------------------------------------------------------------------
		// 既存のプレハブにはこれらのキーが無い。読み込み時は BeginArray が false を返し、
		// GUID も既定のまま残るので、子なしのプレハブとして今までどおり動く。
		//----------------------------------------------------------------------
		a_ar.GUIDField("SavedGUID", m_savedGUID);

		size_t _childCount = m_children.size();
		if (!a_ar.BeginArray("Children", _childCount)) return;

		m_children.resize(_childCount);

		for (size_t _i = 0; _i < _childCount; ++_i)
		{
			if (!a_ar.BeginObject(_i)) continue;

			PrefabChild& _child = m_children[_i];

			a_ar.GUIDField("SavedGUID", _child.savedGUID);
			a_ar.Field("ParentIndex", _child.parentIndex);

			// この子が持つコンポーネント名
			std::vector<std::string> _childCompNames = {};
			if (a_ar.GetMode() == Persistence::Archive::EMode::Save)
			{
				_childCompNames = a_pWorld->GetComponentNames(_child.sig);
			}
			a_ar.VectorField("ComponentNames", _childCompNames);

			// 【ロード時のみ】名前からシグネチャを作り、既定値バッファを確保する
			if (a_ar.GetMode() == Persistence::Archive::EMode::Load)
			{
				_child.sig = {};
				_child.dataMap.clear();

				for (const std::string& _name : _childCompNames)
				{
					ECS::ComponentTypeID _typeID = a_pWorld->GetCompTypeID(_name);
					if (_typeID == ECS::Limits::INVALID_COMPONENTTYPEID) continue;

					_child.sig.set(_typeID);

					auto& _buffer = _child.dataMap[_typeID];
					_buffer.assign(a_pWorld->GetComponentMetaData(_typeID).compAlignSize, 0);

					auto _ctor = a_pWorld->GetCompFunc(_typeID).construct;
					if (_ctor) _ctor(_buffer.data());
				}
			}

			// 各コンポーネントデータ(ルートと同じ形)
			for (const std::string& _name : _childCompNames)
			{
				ECS::ComponentTypeID _typeID = a_pWorld->GetCompTypeID(_name);
				if (_typeID == ECS::Limits::INVALID_COMPONENTTYPEID) continue;

				auto _dataIt = _child.dataMap.find(_typeID);
				if (_dataIt == _child.dataMap.end() || _dataIt->second.empty()) continue;

				auto _func = a_pWorld->GetCompFunc(_typeID).archive;
				if (!_func) continue;

				if (a_ar.BeginGroup(_name))
				{
					_func(a_ar, _dataIt->second.data());
					a_ar.EndGroup();
				}
			}

			a_ar.EndObject();
		}
		a_ar.EndArray();
	}
}
