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
	//	ヒエラルキー上の親(エディターの並びだけに効く)。無効なら根
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
		// 初期化フェーズ : 進めるのは GameObjectManager
		//=======================================================================
		EObjectInitPhase GetInitPhase() const { return m_initPhase; }
		void SetInitPhase(EObjectInitPhase a_phase) { m_initPhase = a_phase; }

		//=======================================================================
		// ヒエラルキー上の親 : エディターで並びをまとめるためだけのもの
		//=======================================================================

		/// <summary>
		/// ヒエラルキーでの親
		/// </summary>
		/// <remarks>
		/// 効くのは**エディターの並びだけ**。座標も回転も表示状態も親から伝わらない。
		/// 「この確認ボックスは MissionSelect のもの」といったまとまりを付けて、
		/// オブジェクトが増えたときに一覧から探せるようにするために持たせてある。
		///
		/// 親が見つからないもの(消された・まだ読み込んでいない)は根として扱われるので、
		/// 参照が切れても一覧から消えることはない。
		///
		/// 実際の親子(座標が伝わるもの)が要るなら ECS の HierarchyComponent を使うこと。
		/// (例外 : UIPanel の下に置いた UI は、パネルの表示状態に従う。App 側の UIBase を参照)
		/// </remarks>
		const Core::GUID& GetParentGUID() const { return m_parentGUID; }
		void SetParentGUID(const Core::GUID& a_guid) { m_parentGUID = a_guid; }

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

	protected:

		// オブジェクトの名前
		std::string m_name;

		// 存在フラグ : trueにしたら次フレームの初めにオブジェクトが消去される
		bool m_isExpired = false;

		// インスタンスGUID(シーン保存時に発行し、読み込み時の参照解決に使う)
		Core::GUID m_guid = {};

		// ヒエラルキー上の親(エディターの並びだけに効く)。無効なら根
		Core::GUID m_parentGUID = {};

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
