#pragma once

#include "Core/Debug/DebugLog.h"

//==========================================================================================
//
// ObjectMetaRegistry (クラスメタマネージャー)
//
// ECS外の GameObject(BaseObject派生) の「クラス情報」を一元管理するレジストリ。
// ComponentMetaRegistry のオブジェクト版にあたる。
//
//   - 実体は MainEngine が1つだけ持ち、EngineServices::pObjectRegistry で配る
//     (ComponentMetaRegistry と同じ扱い)。
//   - GameManager::Init で各クラスを RegisterType で登録する。
//   - 登録したクラスは「タイプID(ObjectTypeID)」で引ける。
//   - シーン保存時は各オブジェクトの ObjectTypeID / GUID / データ を書き出し、
//     読み込み時は ObjectTypeID からクラス情報(ファクトリ)を引いてインスタンスを復元する。
//
//------------------------------------------------------------------------------------------
// タイプIDは「登録名のハッシュ」で決まる
//
// 以前は登録順(0,1,2...)で振っていたため、GameManager::Init の途中へ RegisterType を
// 1行挟むだけで、それ以降のクラスのIDが全部ずれて既存シーンが壊れていた。
// 「必ず末尾へ足すこと」というコメント運用で凌いでいたが、破ったときに
// 何も言わずに別クラスが復元されるため、事故ったときの原因が非常に追いにくい。
//
// 現在は名前(RegisterType の第1引数)から作るので、
//   ・登録順を入れ替えても、間に挿しても、既存シーンに影響しない
//   ・逆に「登録名」を変えるとIDが変わるので、名前はシーンの一部だと思って扱うこと
// という性質になる。名前を変えたいときは MigrateName で旧名を引き継がせる。
//
//==========================================================================================

#include "../BaseObject/BaseObject.h"

namespace Engine::GameObject
{
	// ランタイム用のクラスID(登録名のFNV-1aハッシュ)。
	// シーンファイルへはこの値を書き出す。
	using ObjectTypeID = uint32_t;

	// 無効値
	inline constexpr ObjectTypeID INVALID_OBJECT_TYPE_ID = 0xFFFFFFFF;

	/// <summary>登録名からタイプIDを作る(登録・参照の両方でここを通す)</summary>
	inline ObjectTypeID MakeObjectTypeID(const std::string& a_name)
	{
		return static_cast<ObjectTypeID>(Core::String::ToHash(a_name));
	}

	// クラスのメタ情報
	struct ObjectMeta
	{
		std::string name = "none";	// 表示名 / タイプIDの元になる文字列
	};

	// クラスに付随する処理
	struct ObjectFunc
	{
		// 既定コンストラクタでインスタンスを1つ生成するファクトリ
		std::function<std::unique_ptr<BaseObject>()> create;
	};

	class ObjectMetaRegistry
	{
	public:

		ObjectMetaRegistry() = default;
		~ObjectMetaRegistry() = default;
		NON_COPYABLE_NON_MOVABLE(ObjectMetaRegistry);

		// クラスの登録 : メタ情報とファクトリを同時に登録し、タイプIDを返す
		template<typename T>
		ObjectTypeID RegisterType(const std::string& a_name);

		//----------------------------------------------------------------------------------
		// 旧名の引き継ぎ
		//
		// 登録名を変えるとタイプIDも変わるので、そのままでは旧名で保存された
		// シーンのオブジェクトが復元できなくなる。RegisterType の直後にこれを呼ぶと、
		// 旧名のIDも同じクラスへ流れるようになる。
		//
		// a_onCreate を渡すと、旧名から作ったときだけ、読み込みの前に呼ばれる。
		// 複数のクラスを1つへまとめたときに、元のクラスごとの既定値を入れるのに使う
		// (保存値を持たない古いデータは、ここで入れた値のまま残る)
		//----------------------------------------------------------------------------------
		using MigrateFunc = std::function<void(BaseObject&)>;
		void MigrateName(const std::string& a_oldName, const std::string& a_currentName, MigrateFunc a_onCreate = nullptr);

		// タイプIDの取得
		template<typename T>
		ObjectTypeID GetTypeID() const;								// C++型から
		ObjectTypeID GetTypeID(const std::string& a_name) const;	// クラス名から
		ObjectTypeID GetTypeID(TypeInfo::TypeKey a_key) const;		// 型キーから(実体は GetTypeChain()->key)

		//----------------------------------------------------------------------------------
		// シーンから読んだ値をタイプIDへ解決する
		//
		//   1. そのまま登録済みIDとして引く
		//   2. 駄目なら旧名のIDとして引き直す(MigrateName で張った別名)
		//----------------------------------------------------------------------------------
		ObjectTypeID ResolveTypeID(uint32_t a_savedValue) const;

		// メタ情報取得
		const ObjectMeta& GetMeta(ObjectTypeID a_id) const;

		// 登録したファクトリで実体を生成する(未登録なら nullptr)
		std::unique_ptr<BaseObject> Create(ObjectTypeID a_id) const;

		/// <summary>
		/// シーンから読んだ値で実体を生成する(解決できなければ nullptr)
		/// </summary>
		/// <remarks>旧名の値なら、MigrateName で渡した a_onCreate も通す</remarks>
		std::unique_ptr<BaseObject> CreateFromSaved(uint32_t a_savedValue) const;

		// 全クラス情報(エディターの AddObject 一覧用)
		const std::unordered_map<ObjectTypeID, ObjectMeta>& GetAllMeta() const { return m_metaMap; }

		// 登録済みか
		bool IsValid(ObjectTypeID a_id) const { return m_metaMap.find(a_id) != m_metaMap.end(); }

	private:

		// C++型 / 名前 → タイプID
		std::unordered_map<TypeInfo::TypeKey, ObjectTypeID>	m_typeKeyMap;
		std::unordered_map<std::string, ObjectTypeID>		m_nameMap;

		// タイプID → 情報
		// AddObject の一覧はこの表をそのまま舐めるので、別名(MigrateName)は入れないこと
		std::unordered_map<ObjectTypeID, ObjectMeta> m_metaMap;
		std::unordered_map<ObjectTypeID, ObjectFunc> m_funcMap;

		// 旧名のタイプID → 現行のタイプID(MigrateName で張る)
		std::unordered_map<ObjectTypeID, ObjectTypeID> m_aliasMap;

		// 旧名のタイプID → 旧名から作ったときに通す処理(渡されたものだけ)
		std::unordered_map<ObjectTypeID, MigrateFunc> m_aliasCreateMap;
	};

	template<typename T>
	inline ObjectTypeID ObjectMetaRegistry::GetTypeID() const
	{
		return GetTypeID(TypeInfo::GetTypeKey<T>());
	}

	template<typename T>
	inline ObjectTypeID ObjectMetaRegistry::RegisterType(const std::string& a_name)
	{
		// BaseObject 派生であることを保証
		static_assert(std::is_base_of_v<BaseObject, T>, "T は BaseObject を継承している必要があります");

		// すでに登録済みなら既存IDを返す
		const TypeInfo::TypeKey _typeKey = TypeInfo::GetTypeKey<T>();
		if (ObjectTypeID _existing = GetTypeID(_typeKey); _existing != INVALID_OBJECT_TYPE_ID)
		{
			return _existing;
		}

		// 登録名からタイプIDを作る(登録順には一切依存しない)
		const ObjectTypeID _typeID = MakeObjectTypeID(a_name);

		//----------------------------------------------------------------------------------
		// 名前のチェック
		//
		// ここで弾かれるのは「別クラスが同じIDになる」ケースだけで、放っておくと
		// シーンの復元時に静かに別クラスが生えてくる。起動時に必ず気付けるよう、
		// ログを出して登録自体を諦める(そのクラスはシーンに出てこなくなる)。
		//----------------------------------------------------------------------------------
		// 0 は未初期化のメモリと、INVALID は「引けなかった」と区別が付かない
		if (_typeID == 0 || _typeID == INVALID_OBJECT_TYPE_ID)
		{
			ENGINE_WARNING("[ObjectMetaRegistry] タイプIDが無効値になりました。登録名を変えてください : %s", a_name.c_str());
			ENGINE_ERRLOG(false, "ObjectTypeID が無効値 : 登録名を変えること");
			return INVALID_OBJECT_TYPE_ID;
		}
		if (auto _it = m_metaMap.find(_typeID); _it != m_metaMap.end())
		{
			ENGINE_WARNING("[ObjectMetaRegistry] タイプIDが衝突しました : %s <-> %s", a_name.c_str(), _it->second.name.c_str());
			ENGINE_ERRLOG(false, "ObjectTypeID の衝突 : どちらかの登録名を変えること");
			return INVALID_OBJECT_TYPE_ID;
		}

		// メタ情報
		ObjectMeta _meta = {};
		_meta.name = a_name;

		// ファクトリ
		ObjectFunc _func = {};
		_func.create = []() -> std::unique_ptr<BaseObject> { return CreateObject<T>(); };

		// 各対応表へ登録
		m_typeKeyMap.emplace(_typeKey, _typeID);
		m_nameMap.emplace(a_name, _typeID);
		m_metaMap.emplace(_typeID, _meta);
		m_funcMap.emplace(_typeID, _func);

		return _typeID;
	}
}
