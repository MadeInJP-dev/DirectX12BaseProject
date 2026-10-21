#pragma once

namespace Engine::ECS
{
	class World;
	struct EngineServices;
}

namespace Engine::GameObject
{
	class GameObjectManager;
	class BaseObject;

	/// <summary>
	/// 引数で持たせる
	///
	/// オブジェクト側からシングルトンを名指ししなくて済むように、
	/// シーン生成時にマネージャーへ差し込んだものをここで配る。
	/// (システム側の SystemContext と同じ考え方)
	/// </summary>
	struct ObjectContext
	{
		float dt = 0.0f;

		// 自分が属するシーンのECSワールド
		Engine::ECS::World* pWorld = nullptr;

		// アプリ寿命のサービス群(グラフィックス・リソース・オプションなど)
		Engine::ECS::EngineServices* pServices = nullptr;

		// 自分を持っているマネージャー。
		// オブジェクト同士を GUID で参照する(FindByGUID)ときに使う。
		// シングルトンを名指ししないための経路なので、必ずここから引くこと
		GameObjectManager* pObjectManager = nullptr;

		/// <summary>
		/// フレームの番号(GameObjectManager::PreUpdate を通るたびに1つ進む)
		/// </summary>
		/// <remarks>
		/// PreUpdate で全員から集めて Update で答えを見る、といった
		/// 「1フレームだけ有効な集計」を持つ側が、作り直すきっかけに使う。
		/// (UI のカーソルの取り合いは App 側の UICursorResource がこれを見ている。
		///  以前はその集計そのものをここに持っていたが、UI の都合なのでエンジンから外した)
		/// </remarks>
		uint64_t frameIndex = 0;
	};

	/// <summary>
	/// シーンビューのギズモ編集用にエディターから渡す情報。
	/// (エディター側の型に依存させたくないので、ここでは生の行列とビューポート情報だけを持つ)
	/// </summary>
	struct ObjectGizmoContext
	{
		Math::Matrix viewMat = {};		// カメラのビュー行列
		Math::Matrix projMat = {};		// カメラのプロジェクション行列

		Math::Vector2 viewportPos = {};			// シーンビュー画像の左上(スクリーン絶対座標, px)
		Math::Vector2 viewportSize = {};		// シーンビュー画像の表示サイズ(px)
	};

	/// <summary>
	/// 初期化フェーズ
	/// </summary>
	/// <remarks>
	/// ECSのシステム分類(PostDeserialize / Awake / Start)と同じ並び。
	/// 生成したオブジェクトはこの順に1つずつ通り、通し終えたものが Active になる。
	/// 進めるのは GameObjectManager::RunInitPhases で、フェーズごとに全員を回すので
	/// 「相手がまだ前のフェーズを通っていない」状態で次に入ることがない。
	/// </remarks>
	enum class EObjectInitPhase : uint8_t
	{
		PostDeserialize,	// 生成直後 : まだ1つも通っていない
		Awake,
		Start,
		Active,				// 3つとも通し終えた(以降は Update だけ)
	};

	//=======================================================================
	// 全オブジェクトに継承させる基底クラス
	// 
	// 以下のものをもっている
	//	オブジェクトの名前
	//	存在フラグ : trueにしたら次フレームの初めにオブジェクトが消去される
	//	インスタンスGUID(シーン保存時に発行し、読み込み時の参照解決に使う)
	//	親と子(子は親の行列に追従する)。親が居なければ根
	//	自身の処理ステート
	//=======================================================================
	class BaseObject
	{
	public:

		// 実体の型を辿るための連鎖の根(Core::TypeInfo)
		ENGINE_TYPE_CHAIN_ROOT(BaseObject);

		BaseObject() = default;
		virtual ~BaseObject() = default;

		//----------------------------------------------------------------------------
		// 初期化 : 3つのフェーズに分けてある
		//			生成したオブジェクトは PostDeserialize -> Awake -> Start の順
		//----------------------------------------------------------------------------
		virtual void PostDeserialize(ObjectContext& /*a_context*/) {}	// 自分のデータだけで完結する整え直しを置く場所
		virtual void Awake(ObjectContext& /*a_context*/) {}				// リソースの要求、コンテキストから受け取る
		virtual void Start(ObjectContext& /*a_context*/) {}				// 他のオブジェクトを当てにしてよいのはここから

		//----------------------------------------------------------------------------
		// 解放
		//----------------------------------------------------------------------------
		virtual void Release(ObjectContext& a_context);

		//----------------------------------------------------------------------------
		// 更新
		//----------------------------------------------------------------------------
		virtual void PreUpdate(ObjectContext& /*a_context*/) {}		// 全員の申告が揃ってからでないと決められないものを置く場所
		virtual void Update(ObjectContext& a_context);
		virtual void Draw(ObjectContext& a_context);

		//----------------------------------------------------------------------------
		// シリアライズ
		//----------------------------------------------------------------------------
		virtual void Archive(Persistence::Archive& /*a_ar*/, ObjectContext& /*a_context*/) {}

		//----------------------------------------------------------------------------
		// エディター
		//----------------------------------------------------------------------------
		virtual const char* GetEditorName() const { return "GameObject"; }
		virtual void DrawInspector(ObjectContext& /*a_context*/) {}
		virtual bool DrawGizmo(const ObjectGizmoContext& /*a_ctx*/, ObjectContext& /*a_context*/) { return false; }

		//=======================================================================
		// 表示の切り替え
		//=======================================================================

		/// <summary>
		/// 表示するか
		/// </summary>
		/// <remarks>
		/// 出し入れに対応していないオブジェクトでは何も起きない(既定は常に表示)。
		///
		/// ここに置いてあるのは、進行役(HomeSequence など)が
		/// 「このGUIDのものを出す/隠す」と束ねるときに、
		/// 相手が UI なのか別の進行役なのかを知らずに済ませるため。
		/// 型ごとに dynamic_cast を並べると、出し入れできる種類を増やすたびに
		/// 束ねる側を直すことになる。
		/// </remarks>
		virtual bool IsVisible() const { return true; }
		virtual void SetVisible(bool /*a_isVisible*/) {}

		//=======================================================================

		/// <summary>
		/// 次フレームの初めにこのオブジェクトを破棄するよう要求する。
		/// </summary>
		void ReserveDestroy() { m_isExpired = true; }

		bool IsExpired() const { return m_isExpired; }

		//=======================================================================
		// GUID : インスタンスの一意識別子(シーン内でこのGUIDから実体を引ける)
		//=======================================================================
		void SetGUID(const Core::GUID& a_guid) { m_guid = a_guid; }
		const Core::GUID& GetGUID() const { return m_guid; }

		//=======================================================================
		// 名前 : ヒエラルキーに出す名前(保存される)
		//=======================================================================
		const std::string& GetName() const { return m_name; }
		void SetName(const std::string& a_name) { m_name = a_name; }

		// 一覧に出す名前 : 名前を付けていなければクラス名(GetEditorName)
		const char* GetDisplayName() const { return m_name.empty() ? GetEditorName() : m_name.c_str(); }

		//=======================================================================
		// 初期化フェーズ : 進めるのは GameObjectManager
		//=======================================================================
		EObjectInitPhase GetInitPhase() const { return m_initPhase; }
		void SetInitPhase(EObjectInitPhase a_phase) { m_initPhase = a_phase; }

		//=======================================================================
		// 親子
		//
		// 子は親の行列に追従する(ワールド行列 = 自分のローカル行列 × 親のワールド行列)。
		// 親はポインタで持ち、保存するときだけ親の GUID に直す(GameObjectManager::Archive)。
		// 親が消えると子も一緒に消える(GameObjectManager::PreUpdate)ので、
		// 親・子のポインタが消えた相手を指したまま残ることはない
		//=======================================================================

		// 親(居なければ nullptr = 根)
		BaseObject* GetParent() const { return m_pParent; }

		// 子の一覧(付けた順)
		const std::vector<BaseObject*>& GetChildren() const { return m_childVec; }

		/// <summary>
		/// 親を付け替える(nullptr で根へ戻す)
		/// </summary>
		/// <param name="a_isKeepWorld">
		/// true なら、見た目の置き場所が変わらないように自分のローカルを直す(エディターでの付け替え)。
		/// false ならローカルの値はそのままで、新しい親の分だけ動く(読み込み)
		/// </param>
		/// <returns>自分自身・自分の子孫を親にしようとした(輪ができる)ときは付け替えずに false</returns>
		bool SetParent(BaseObject* a_pParent, bool a_isKeepWorld = true);

		// a_pAncestor の下(子・孫)に居るか。自分自身は含まない
		bool IsDescendantOf(const BaseObject* a_pAncestor) const;

		/// <summary>
		/// 親子のつながりを全部切る : 親の子一覧から抜け、子は全員根になる
		/// </summary>
		/// <remarks>消える直前に GameObjectManager が呼ぶ。子のローカルは直さない</remarks>
		void DetachFromHierarchy();

		//=======================================================================
		// 行列
		//=======================================================================

		/// <summary>
		/// ローカル行列 : 親から見た自分の置き場所
		/// </summary>
		/// <remarks>
		/// 置き場所を持たないもの(進行役など)は単位行列のままで、子へは親の行列がそのまま伝わる。
		/// 置き場所を持つ派生(UI のアンカーなど)が自分の値から組み立てて返す
		/// </remarks>
		virtual Math::Matrix CalcLocalMatrix() const { return Math::Matrix::Identity(); }

		/// <summary>
		/// ワールド行列 : 自分のローカル行列に、根までの親の行列を順に掛けたもの
		/// </summary>
		/// <remarks>
		/// 持ち越さずに呼ぶたびに組み立てる。UI は Update の中で置き場所を差し替える
		/// (一覧の並べ直しなど)ので、覚えておくと同じフレームの子が1フレーム遅れて付いてくるため。
		/// 親子の深さは数段なので、毎回辿っても軽い
		/// </remarks>
		Math::Matrix CalcWorldMatrix() const;

		/// <summary>
		/// 親のワールド行列が変わるとき、ワールドでの置き場所が変わらないよう自分のローカルを直す
		/// </summary>
		/// <param name="a_oldParentWorld">これまでの親のワールド行列(親が居なければ単位行列)</param>
		/// <param name="a_newParentWorld">これからの親のワールド行列(親が居なければ単位行列)</param>
		/// <remarks>
		/// 付け替え(SetParent)と、ローカルで持つ前の古いシーンの読み込み(GameObjectManager)から呼ばれる。
		/// 置き場所を持たないものは何もしなくてよい
		/// </remarks>
		virtual void RebaseLocal(const Math::Matrix& /*a_oldParentWorld*/, const Math::Matrix& /*a_newParentWorld*/) {}

		//=======================================================================
		// 実体の型
		//
		// RTTI を使わないので、生成したときに実体の型を刻んでおく(CreateObject)。
		// 型の変換は TypeInfo::Cast<T>(pObject) で行う
		//=======================================================================
		const TypeInfo::TypeChain* GetTypeChain() const { return m_pTypeChain; }

	private:

		template<typename T>
		friend std::unique_ptr<T> CreateObject();

		// 実体の型の連鎖。刻むのは CreateObject だけ
		const TypeInfo::TypeChain* m_pTypeChain = TypeInfo::GetTypeChain<BaseObject>();

		// 親(持ち主ではない)。nullptr なら根。書き換えるのは SetParent / DetachFromHierarchy だけ
		BaseObject* m_pParent = nullptr;

		// 子(持ち主ではない)。並びはヒエラルキーに出す順
		std::vector<BaseObject*> m_childVec = {};

	protected:

		// オブジェクトの名前(保存される)。空ならヒエラルキーにはクラス名を出す
		std::string m_name;

		// 存在フラグ : trueにしたら次フレームの初めにオブジェクトが消去される
		bool m_isExpired = false;

		// インスタンスGUID(シーン保存時に発行し、読み込み時の参照解決に使う)
		Core::GUID m_guid = {};

		// どこまで初期化を通したか。生成直後は何も通っていない
		EObjectInitPhase m_initPhase = EObjectInitPhase::PostDeserialize;
	};

	/// <summary>
	/// オブジェクトを生成する。実体はかならずここで作ること
	/// </summary>
	/// <remarks>
	/// 実体の型を刻むのはここだけ。make_unique で直接作ると
	/// 実体の型が BaseObject のままになり、保存も TypeInfo::Cast も効かなくなる
	/// </remarks>
	template<typename T>
	std::unique_ptr<T> CreateObject()
	{
		static_assert(std::is_base_of_v<BaseObject, T>, "T は BaseObject を継承している必要があります");

		auto _upObject = std::make_unique<T>();
		static_cast<BaseObject*>(_upObject.get())->m_pTypeChain = TypeInfo::GetTypeChain<T>();
		return _upObject;
	}
}
