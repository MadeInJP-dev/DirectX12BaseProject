#pragma once

#include "../BaseObject/BaseObject.h"
#include "../ObjectMetaRegistry/ObjectMetaRegistry.h"

namespace Engine::GameObject
{
	/// <summary>
	/// オブジェクト指向でゲーム内オブジェクトのスクリーンUIなど、
	/// ECSと相性の悪いオブジェクトを管理するクラス。
	/// シーンに持たせる
	/// </summary>
	class GameObjectManager
	{
	public:

		/// <summary>
		/// コンストラクタ
		/// </summary>
		/// <param name="a_pWorld">
		/// 同じシーンのECSワールド。ここで受け取ったものを ObjectContext に載せて
		/// 各オブジェクトへ配るので、オブジェクト側はシングルトンを触らずに済む。
		/// </param>
		explicit GameObjectManager(Engine::ECS::World* a_pWorld);
		~GameObjectManager();
		NON_COPYABLE_NON_MOVABLE(GameObjectManager);

		/// <summary>
		/// オブジェクトの追加
		/// </summary>
		/// <typeparam name="T">クラス型</typeparam>
		/// <returns>追加した際のポインタ</returns>
		template<typename T>
		T* AddObject();

		/// <summary>
		/// クラスメタマネージャーに登録済みのタイプインデックスから実体を生成して追加する。
		/// (エディターの AddObject ボタン用)。新規GUIDを発行する。
		/// </summary>
		/// <param name="a_typeID">ObjectMetaRegistry に登録されたタイプID</param>
		/// <returns>追加した実体(失敗時 nullptr)</returns>
		BaseObject* AddObjectByTypeID(ObjectTypeID a_typeID);

		/// <summary>
		/// シーン保存・読み込み。
		/// 保存時 : 各オブジェクトの [タイプインデックス / GUID / データ] を書き出す。
		/// 読み込み時 : タイプインデックスからクラスを復元し、GUIDとデータを流し込む。
		/// </summary>
		void Archive(Persistence::Archive& a_ar);

		//----------------------------------------------------------------------------------
		// プレハブ(エディターで使う)
		//
		// オブジェクト1つとその下(子・孫)を1つのアセットに書き、置くときに写す。
		// 置いたものは元のアセットとつながらない(アセットを直しても、置いた分は変わらない)
		//----------------------------------------------------------------------------------

		/// <summary>
		/// a_pRoot とその下を、シーンと同じ並びで書き出す
		/// </summary>
		/// <remarks>根の親は書かない(置いた先の親へつなぐため)</remarks>
		void SavePrefab(BaseObject* a_pRoot, Persistence::Archive& a_ar);

		/// <summary>
		/// プレハブを置く
		/// </summary>
		/// <param name="a_prefabJson">SavePrefab で書いた中身</param>
		/// <param name="a_pParent">根を付ける親(nullptr なら根のまま)</param>
		/// <returns>置いた根(置けなければ nullptr)</returns>
		/// <remarks>中に居る全員の GUID を振り直す。中どうしの参照も新しい GUID へ付け替わる</remarks>
		BaseObject* InstantiatePrefab(const nlohmann::json& a_prefabJson, BaseObject* a_pParent);

		/// <summary>
		/// GUIDからインスタンスを引く(参照解決用)。無ければ nullptr。
		/// </summary>
		BaseObject* FindByGUID(const Core::GUID& a_guid) const;

		/// <summary>
		/// そのポインタが今このマネージャーの管理下にあるか。
		///
		/// エディターは選択中のオブジェクトを生ポインタで覚えているので、
		/// 破棄やシーン切り替えの後に触ると解放済みメモリを読む。
		/// 参照する前にここでアドレスの一致だけを見て確かめる
		/// (実体を触らないので、相手が死んでいても安全に判定できる)。
		/// </summary>
		bool IsManaged(const BaseObject* a_pObject) const;

		/// <summary>
		/// 全オブジェクトの更新前処理
		/// </summary>
		/// <remarks>
		/// 消える指示が出ているものを配列から外し、まだ初期化を通していないものを
		/// 通しきってから、全員の PreUpdate を回す。
		/// フレームの番号(ObjectContext::frameIndex)はここで進む。
		/// PreUpdate で集めたものは、必ず Update より前に揃う
		/// </remarks>
		void PreUpdate();

		/// <summary>
		/// 全オブジェクトの更新
		/// </summary>
		/// <param name="a_dt">デルタタイム</param>
		void Update(float a_dt);

		/// <summary>
		/// 全オブジェクトの描画
		/// </summary>
		/// <param name="a_dt">デルタタイム</param>
		void Draw(float a_dt);

		/// <summary>
		/// 管理中のオブジェクト一覧を取得(エディターのヒエラルキー表示用)。
		/// </summary>
		const std::vector<std::unique_ptr<BaseObject>>& GetObjects() const { return m_upObjectVec; }

		/// <summary>
		/// オブジェクトへ配っている実行コンテキストを取得。
		/// エディターから DrawInspector / DrawGizmo を呼ぶときに渡す。
		/// </summary>
		ObjectContext& RefObjectContext() { return m_objContext; }

	private:

		// GUID→実体の対応表を更新しつつ末尾に追加する共通処理
		BaseObject* Register(std::unique_ptr<BaseObject> a_upObject);

		/// <summary>
		/// まだ初期化を通していないものを PostDeserialize -> Awake -> Start と進める
		/// </summary>
		/// <remarks>
		/// ECS の World::BeginFrame と同じで、3つのフェーズをこの1回で通しきる。
		/// 1フェーズごとに全員を回すのが要で、こうしておくと
		/// 「相手がまだ前のフェーズを通っていない」状態で次に入ることがない。
		/// (シーン読み込みの直後・エディターでの追加直後・毎フレームの頭で呼ばれる)
		/// </remarks>
		void RunInitPhases();

		// 指定フェーズに居るものだけを1回ずつ呼んで、次のフェーズへ送る
		void RunInitPhase(EObjectInitPhase a_phase);

		// 子・孫を全部消える予定にする(親が消えるときに一緒に消すため)
		static void ReserveDestroyDescendants(BaseObject* a_pObject);

		//----------------------------------------------------------------------------------
		// 1体ぶんの並びの保存・読み込み(シーンとプレハブで同じものを使う)
		//----------------------------------------------------------------------------------

		// 読み込んだ1体ぶんの、つなぐ前の親子の情報
		// (親は全員を読み終えてからつなぐ。親が配列で後ろに居ることがあるため)
		struct LoadedLink
		{
			BaseObject* pObject = nullptr;
			Core::GUID parentGUID = {};
			bool isLocalSpace = false;	// 置き場所を親から見た値(ローカル)で保存してあるか
		};

		// GUID を持たないものへ発行する(親の GUID を書く前に全員へ行き渡らせる)
		void IssueMissingGUIDs();

		// a_pObject とその下を、根から下りる順に集める
		static void CollectSubtree(BaseObject* a_pObject, std::vector<BaseObject*>& a_outVec);

		/// <summary>
		/// オブジェクトを GameObjects の配列として書く
		/// </summary>
		/// <param name="a_pRootWithoutParent">親を書かないもの(プレハブの根)。シーンでは nullptr</param>
		void SaveRecords(
			Persistence::Archive& a_ar,
			const ObjectMetaRegistry& a_registry,
			const std::vector<BaseObject*>& a_objectVec,
			const BaseObject* a_pRootWithoutParent);

		// GameObjects の配列を読んで登録する(親はまだつながない・初期化フェーズは回さない)
		std::vector<LoadedLink> LoadRecords(Persistence::Archive& a_ar, const ObjectMetaRegistry& a_registry);

		// 読んだものを親へつなぐ。親を持たずに書かれていたものは a_pRootParent へ付ける(nullptr なら根)
		void LinkLoadedParents(const std::vector<LoadedLink>& a_loadedLinkVec, BaseObject* a_pRootParent);

		/// <summary>
		/// 1体ぶんの基底の値(名前・置き場所をローカルで持っているか)を保存・読み込む
		/// </summary>
		/// <remarks>持たない古いシーンでは何も読まず、渡した値のまま返る</remarks>
		static void ArchiveBase(Persistence::Archive& a_ar, std::string& a_name, bool& a_isLocalSpace);

		/// <summary>
		/// 古いシーン(置き場所をワールドの値で持っていたもの)の子を、親から見た値へ直す
		/// </summary>
		/// <param name="a_pObject">ここから下を直す(a_pObject 自身は直さない)</param>
		/// <param name="a_legacySet">直す相手 : 今回読み込んだ古い形式のもの</param>
		static void RebaseLegacyDescendants(BaseObject* a_pObject, const std::unordered_set<const BaseObject*>& a_legacySet);

	private:

		ObjectContext m_objContext = {};

		// 初期化を通していないものが居るか。
		// 居ないときに毎フレーム配列を舐めないようにするための札
		bool m_isPendingInit = false;

		std::vector<std::unique_ptr<BaseObject>> m_upObjectVec = {};

		// GUID からインスタンスを引くための対応表
		std::unordered_map<Core::GUID, BaseObject*> m_guidMap = {};
	};


	template<typename T>
	inline T* GameObjectManager::AddObject()
	{
		// ベースオブジェクトの継承がされているかのチェック
		static_assert(std::is_base_of_v<BaseObject,T>);

		// オブジェクトの生成(実体の型を刻むため CreateObject を通す)
		auto _upObject = CreateObject<T>();

		// 新規GUIDを発行(まだ持っていなければ)
		Core::GUID _guid = {};
		_guid.Create();
		_upObject->SetGUID(_guid);

		// 追加して初期化(この場で3つのフェーズを通しきる)
		T* _pObject = static_cast<T*>(Register(std::move(_upObject)));
		RunInitPhases();
		return _pObject;
	}
}