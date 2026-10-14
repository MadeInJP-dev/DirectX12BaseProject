#pragma once

#include "Core/Debug/DebugLog.h"

// アセットデータベース
#include "../AssetDatabase/AssetDatabase.h"

// 各リソース
#include "../../Data/AnimatorAsset/AnimatorAsset.h"
#include "../../Data/Particles/ParticlesAsset.h"

// ローダー
#include "../../Loader/DefaultLoader.h"

// 非同期ロードの実行先
#include "Engine/JobSystem/JobSystem.h"

namespace Engine::Resource
{
	//==========================================================================================
	// リソース1件の状態
	//
	// スロット(プールの添え字)ごとに1つ持つ。
	//
	// ロードの流れ :
	//   1) 先着1本が空の実体でスロットを押さえ、Loading にしてハンドルを確定させる
	//   2) 中身ができる前でもハンドルは呼び出し元へ返せる
	//   3) ビルドし終えたところで Ready / Failed に切り替わる
	//
	// 2) があるため、ハンドルを持っていても中身が使えるとは限らない。
	// 触る前に必ず IsReady() を見ること。
	//
	// スイープ(GC)は Loading のスロットを触らないので、
	// 参照カウントが立つ前にビルド先を引き抜かれることはない。
	//==========================================================================================
	enum class EResourceState
	{
		Empty,			// リソースが空
		Loading,		// 読込中
		Ready,			// 使用可能
		Failed			// 読込失敗,使用不可
	};

	//==========================================================================================
	// スロット1件分の付随情報
	//
	// 中身をすべてアトミックにしてあるので、スロットの本数が変わらない限り
	// 共有ロックのまま複数スレッドから更新できる。
	// 実体は個別に確保して配列には unique_ptr で並べる :
	// こうしておくと本数を増やしても既存スロットのアドレスが動かないため、
	// 一度取り出したスロットへのポインタが他スレッドの追加で無効にならない
	//==========================================================================================
	struct ResourceSlot
	{
		std::atomic<EResourceState>	state = EResourceState::Empty;	// リソースの状態
		std::atomic<uint16_t>		refCount = 0;					// 参照カウント : 持ち主の数
	};

	//==========================================================================================
	// 型ごとの管理データ
	//
	// ワーカースレッドからロードを流す前提のため、4つの領域それぞれに守りを入れてある。
	//
	//   pool          : AtomicItemPool が自前でロックを持つ
	//   cache         : cacheMutex で保護 (loadingCondition も同じロック)
	//   slots         : slotMutex は「本数」だけを守る。中身の更新はアトミックに任せる
	//
	// 「いま読み込み中か」はスロットの状態(Loading)がそのまま印になるので、
	// 別途 in-flight の一覧は持たない。
	//
	// ロック順序は cacheMutex -> (pool / slotMutex) の一方向のみ。
	// 逆向きに取る経路を作らないこと
	//==========================================================================================
	template<typename T>
	struct ResourceData
	{
		Pool::AtomicItemPool<T>						pool;					// リソース

		std::unordered_map<Core::GUID, Handle<T>>	cache = {};				// GUID to Handle
		mutable std::mutex							cacheMutex;				// cache を守る
		std::condition_variable						loadingCondition;		// 読み込み完了の通知

		std::vector<std::unique_ptr<ResourceSlot>>	slots = {};				// スロットごとの状態
		mutable std::shared_mutex					slotMutex;				// slots の本数を守る
	};

	// リソースの管理のみ
	class ResourceManager
	{
	public:

		// 解放
		void Release();

		// アセットデータベース : 持ち主はこのクラス。
		// 使う側はここを直接呼ばず、コンテキストに載ったポインタから引くこと
		AssetDatabase& RefAssetDatabase() { return *m_upAssetDatabase; }
		const AssetDatabase& GetAssetDatabase() const { return *m_upAssetDatabase; }

		/// <summary>
		/// 非同期ロードの実行先を登録する
		///
		/// ResourceManager 側からエンジンのシングルトンを直接引かないよう、
		/// 起動時に外から渡す。ジョブシステムを止める前に必ず nullptr を入れ直すこと
		/// </summary>
		void SetJobSystem(Thread::JobSystem* a_pJobSystem)
		{
			m_pJobSystem.store(a_pJobSystem, std::memory_order_release);
		}
		Thread::JobSystem* GetJobSystem() const
		{
			return m_pJobSystem.load(std::memory_order_acquire);
		}

		/// <summary>
		/// サウンドの読み込み先(オーディオエンジン)を登録する
		///
		/// SoundEffect はオーディオエンジンから作るので、ローダーへコンテキストで渡す。
		/// オーディオエンジンを壊す前に nullptr を入れ直すこと
		/// </summary>
		void SetAudioEngine(DirectX::AudioEngine* a_pAudioEngine)
		{
			m_pAudioEngine.store(a_pAudioEngine, std::memory_order_release);
		}

		/// <summary>
		/// プレハブの読み込みが型を引くワールドの持ち主を登録する
		///
		/// プレハブはコンポーネント名から型を引き直すので、今のシーンのワールドが要る。
		/// シーンを全部解放した後は nullptr を入れ直すこと
		/// </summary>
		void SetSceneManager(Scene::SceneManager* a_pSceneManager)
		{
			m_pSceneManager.store(a_pSceneManager, std::memory_order_release);
		}

		/// <summary>
		/// リソースの読み込みを要求する : 呼び出しスレッドは待たない
		///
		/// 空ならスロットだけ押さえてジョブへ流し、読込中・読込済みなら何もしない。
		/// どの場合もハンドルは即座に返る。
		///
		/// 返ったハンドルは「まだ中身が空」の可能性があるため、
		/// 実体を触る前に必ず IsReady() を見ること。
		///
		/// ジョブシステムが未登録の場合のみ、その場で同期的に読む
		/// </summary>
		/// <param name="a_guid">アセットのGUID</param>
		template<typename T>
		inline ResourceRef<T> RequestLoad(const Core::GUID& a_guid);

		/// <summary>
		/// リソースの読み込み : 実体ができるまで呼び出しスレッドを待たせる
		///
		/// 同じGUIDを複数スレッドが同時に要求した場合、実際に読むのは先着1本だけで、
		/// 後続は完了を待って同じハンドルを受け取る。
		/// 違うGUIDどうしは並行して読まれる。
		///
		/// 返った時点で必ず Ready(または Failed)になっているので、
		/// 「今すぐ実体が要る」経路はこちらを使う。
		/// メインスレッドを止めたくない経路は RequestLoad() へ移すこと
		/// </summary>
		/// <param name="a_guid">アセットのGUID</param>
		/// <param name="a_pBuildContext">
		/// ビルドコンテキスト。
		/// モデルのように複数のリソースをまとめて読むときは、呼び出し元でバッチを開いて渡すこと。
		/// 省略した場合はロード側がその場でバッチを開くため、リソース1個ごとにキューへの実行が走る。
		/// </param>
		template<typename T>
		inline ResourceRef<T> LoadImmediate(const Core::GUID& a_guid, const ResourceBuildContext* a_pBuildContext = nullptr);

		/// <summary>
		/// 型を知らないまま読み込みを要求する : 型はアセットデータベースの種別から引く
		/// </summary>
		/// <remarks>
		/// シーンの先読みのように、GUIDの並びだけを持っている側のための入口。
		/// 返る参照は持たないので、読んだものはキャッシュに残るだけ(次のスイープまで)。
		///
		/// 待つかどうかは種別で決まる。ほかの場所で非同期に読まれている種別
		/// (Model / Texture / EffectAsset / Font)だけジョブへ流し、残りはその場で読み切る。
		/// プレハブはワールドを引きながら読むので、ワーカーへ流すと
		/// 呼び出し元とは別のシーンのワールドを掴みかねない。
		/// </remarks>
		/// <returns>読み込める種別だったら true(読み込みの成否ではない)</returns>
		bool RequestLoadByGUID(const Core::GUID& a_guid);

		/// <summary>
		/// 型を知らないまま状態を取得する : 型はアセットデータベースの種別から引く
		/// </summary>
		/// <remarks>
		/// 先読みの進み具合を数えるためのもの。まだ要求していなければ Empty。
		/// 読めない種別・消えたアセットは、もう届かないので Failed を返す
		/// </remarks>
		EResourceState GetStateByGUID(const Core::GUID& a_guid);

		/// <summary>
		/// ジョブへ流して、まだ読み終わっていないリソースの数
		/// </summary>
		/// <remarks>
		/// シーンの読み込みの進み具合に使う(モデルの到着待ちの間もバーを進めるため)。
		/// どのシーンが頼んだものかは区別しない
		/// </remarks>
		uint32_t GetInFlightLoadCount() const { return m_inFlightLoadCount.load(std::memory_order_acquire); }

		//------------------------------------------------------------------------------------------
		// 読み込み要求の記録
		//
		// 記録している間に要求されたGUIDを、初めて要求された順に重複なく溜める。
		// シーンの先読み一覧を作るためのもの(SceneManager が開いてから閉じるまで回す)。
		//
		// ・読込済みのものを要求し直した場合も数える
		//     前のシーンから持ち越したものも「このシーンが使った」ものなので
		// ・ローダーの中から読まれたもの(モデルの中のメッシュなど)は数えない
		//     持ち主を先読みすれば一緒に読まれるため。一覧には持ち主だけが並ぶ
		//
		// 記録は同時に1つだけ。ワーカースレッドからの要求も拾う
		//------------------------------------------------------------------------------------------
		void BeginRecordRequests();							// 記録を始める(前回の残りは捨てる)
		std::vector<Core::GUID> EndRecordRequests();		// 記録を止めて、溜まったGUIDを受け取る
		bool IsRecordingRequests() const { return m_isRecordingRequests.load(std::memory_order_acquire); }

		// リソースの追加
		template<typename T>
		ResourceRef<T> Add(T&& a_resource);

		template<typename T>
		void AddRef(const Handle<T>& a_handle);

		/// <summary>
		/// キャッシュにも追加されるアセットのデータ追加用関数
		/// </summary>
		/// <typeparam name="T">型</typeparam>
		/// <param name="a_resource">リソースの実態(std::move()必須)</param>
		/// <param name="a_guid">アセットの読み込み時GUID</param>
		/// <returns>保存されたラインタイムハンドル</returns>
		template<typename T>
		Handle<T> AddResourceAndGUID(T&& a_resource,const Core::GUID& a_guid);

		// リソースの削除
		template<typename T>
		void Remove(const Handle<T>& a_handle);

		template<typename T>
		void ReleaseRef(const Handle<T>& a_handle);

		//------------------------------------------------------------------------------------------
		// 生ハンドルの取得と返却 (ECSコンポーネント用)
		//------------------------------------------------------------------------------------------
		//
		// ECSのコンポーネントは trivially copyable でなければならない(チャンク間を memcpy で
		// 動かすため)ので、RAIIの ResourceRef を持てない。代わりに
		//
		//   取るとき : Acquire〜 を通す(参照カウントを1つ取り、ハンドルを書き込む)
		//   返すとき : ComponentTraits<T>::Release から ReleaseHandle を呼ぶ
		//
		// という決まりで数える。ECSは
		//   ・エンティティを消すとき
		//   ・コンポーネントを外すとき
		//   ・PostDeserialize へ入り直すとき(直後に fixup が取り直す)
		// に必ず解放フックを呼ぶので、取りっぱなしにはならない。
		//
		// Acquire〜 は「今入っているハンドルは持ち主ではない」前提で必ず1つ取る。
		// 上の決まりで、取り直す前には必ず返してあるため。
		//------------------------------------------------------------------------------------------

		/// <summary>
		/// 参照を1つ取って、ハンドルを書き込む : 実体の到着は待たない
		/// </summary>
		template<typename T>
		void AcquireRequest(Handle<T>& a_inoutHandle, const Core::GUID& a_guid);

		/// <summary>
		/// 参照を1つ取って、ハンドルを書き込む : 実体ができるまで待つ
		/// </summary>
		template<typename T>
		void AcquireImmediate(Handle<T>& a_inoutHandle, const Core::GUID& a_guid,
			const ResourceBuildContext* a_pBuildContext = nullptr);

		/// <summary>
		/// 取った参照を返して、ハンドルを空にする
		/// </summary>
		/// <remarks>
		/// ハンドルを空にするのは、返した後のものを持ち主のふりで持ち続けないため。
		/// 使う側は無効ハンドルを見て取り直せばよい。
		/// </remarks>
		template<typename T>
		void ReleaseHandle(Handle<T>& a_inoutHandle);

		// リソースの取得
		//
		// 取得はハンドル経由に限定している。
		// 添え字だけで引く口を用意すると、スロットが再利用されていたときに
		// 世代の食い違いを検出できず、別のリソースを黙って掴んでしまう。
		//
		// 返るポインタは他スレッドの追加では壊れないが、
		// 同じリソースの Remove / スイープには勝てない。
		// 使う瞬間にここで引き直し、ローカルの外へ持ち出さないこと
		template<typename T>
		const T* Get(const Handle<T>& a_handle)const;
		template<typename T>
		const T* Get(const ResourceRef<T>& a_handle)const;
		template<typename T>
		T* Ref(const Handle<T>& a_handle);
		template<typename T>
		T* Ref(const ResourceRef<T>& a_handle);

		//------------------------------------------------------------------------------------------
		// リソースの状態
		//------------------------------------------------------------------------------------------

		/// <summary>
		/// ハンドルが指すリソースの状態を取得する
		/// 無効ハンドル(未登録・世代違い)は Empty を返す
		/// </summary>
		template<typename T>
		EResourceState GetState(const Handle<T>& a_handle);

		/// <summary>
		/// GUIDからリソースの状態を取得する : まだ読んでいないものは Empty
		/// </summary>
		template<typename T>
		EResourceState GetState(const Core::GUID& a_guid);

		/// <summary>
		/// 状態を書き換える
		/// ロード側が Loading -> Ready / Failed を通知するのに使う
		/// </summary>
		template<typename T>
		void SetState(const Handle<T>& a_handle, EResourceState a_state);

		/// <summary>
		/// 使用可能か : ハンドルが有効で、かつ読み込みが終わっているか
		/// 中身を触る前にこれを見ること(読込中/失敗のリソースを掴まないため)
		/// </summary>
		template<typename T>
		bool IsReady(const Handle<T>& a_handle);
		template<typename T>
		bool IsReady(const ResourceRef<T>& a_ref);

		/// <summary>
		/// リソースの解放
		/// 実体を破棄するため、他スレッドがポインタを握っていない状態で呼ぶこと
		/// </summary>
		template<typename T>
		void SweepUnused();

		/// <summary>
		/// 型ごとの管理データを丸ごと破棄する
		/// プールを空にしただけだとキャッシュと参照カウントが残り、
		/// 同じインデックス・世代が再利用されたときに別リソースを掴んでしまうため、
		/// 必ずまとめて捨てる
		/// </summary>
		template<typename T>
		void ReleaseData();

		/// <summary>
		/// 使われなくなったリソースを全プールから片付ける
		/// </summary>
		/// <remarks>
		/// 参照カウントが 0 のものだけを破棄する。呼ぶのはシーンの切れ目。
		/// 参照が 0 になった瞬間に捨てないのは、同じシーンの中で出し直すたびに
		/// 読み直しが走るのを避けるため。
		/// </remarks>
		void SweepUnusedAll();

		// キャッシュアクセス
		//
		// 参照ではなく値で返す :
		// マップの中身への参照を返すと、他スレッドの登録・削除で足元が崩れる
		template<typename T>
		Handle<T> GetCache(const Core::GUID& a_guid);
		template<typename T>
		Core::GUID GetCache(const Handle<T>& a_handle);

		// キャッシュから削除
		template<typename T>
		void RemoveCache(const Handle<T>& a_handle);
		template<typename T>
		void RemoveCache(const Core::GUID& a_guid);

		// すでに読み込まれているかのチェック
		template<typename T>
		bool Has(const Core::GUID& a_guid);

		// ハンドルの有効チェック
		template<typename T>
		bool IsValid(const Handle<T>& a_handle);

		/// <summary>
		/// 実体が生存しているか
		/// 持ち主(MainEngine)より後に壊れるもの・破棄の途中で参照を返しに来るもの
		/// (ResourceRef のデストラクタなど) は、必ずこれで生存確認してからアクセスすること。
		/// 自分のプールの中身が ResourceRef を持っているので、自分の破棄中にも呼ばれる
		/// </summary>
		static bool IsAlive() noexcept { return AliveFlag(); }

	private:

		/// <summary>
		/// 生存フラグ : bool は自明なデストラクタを持つためデストラクタが登録されず、
		/// プロセス終了までこの参照自体は安全に読める
		/// </summary>
		static bool& AliveFlag() noexcept
		{
			static bool _alive = false;
			return _alive;
		}

		/// <summary>
		/// このスレッドでいま入っているビルド(BuildIntoSlot)の深さ
		/// </summary>
		/// <remarks>
		/// 0 でなければローダーの中からの要求。読み込み要求の記録で、
		/// 持ち主に付いて読まれるもの(モデルの中のメッシュなど)を外すのに使う
		/// </remarks>
		static int& BuildDepth() noexcept
		{
			thread_local int _depth = 0;
			return _depth;
		}

		/// <summary>
		/// アセットデータベースの種別から型を引き、a_func(std::type_identity&lt;T&gt;) を呼ぶ
		/// </summary>
		/// <returns>このマネージャーが持つ種別だったら true</returns>
		/// <remarks>.cpp の中でだけ使うので定義も .cpp に置く</remarks>
		template<typename Func>
		bool VisitAssetType(const Core::GUID& a_guid, Func&& a_func);

		// 読み込み要求を1件記録する(記録中のときだけ呼ばれる)
		void RecordRequest(const Core::GUID& a_guid);

		/// <summary>
		/// スロットを、指定の添え字まで伸ばす
		///
		/// 伸ばす必要がないときは共有ロックだけで抜ける。
		/// 実体は個別確保なので、伸ばしても既に配られたスロットのアドレスは動かない
		/// </summary>
		template<typename T>
		void EnsureSlot(uint16_t a_index);

		/// <summary>
		/// スロットの取り出し
		/// 返るポインタは ReleaseData / Release まで有効
		/// </summary>
		/// <returns>範囲外なら nullptr</returns>
		template<typename T>
		ResourceSlot* RefSlot(uint16_t a_index);

		/// <summary>
		/// GUIDに対応するスロットを押さえる
		///
		/// まだ無ければ空の実体で新規に確保し、Loading にしてからキャッシュへ載せる。
		/// 状態を先に立ててから公開しないと、中身も状態もできていないハンドルが
		/// 他スレッドから見えてしまう
		/// </summary>
		/// <param name="a_outIsOwner">このスロットを新規に押さえた(=ビルド担当)なら true</param>
		template<typename T>
		Handle<T> RequestSlot(const Core::GUID& a_guid, bool& a_outIsOwner);

		/// <summary>
		/// 押さえておいたスロットへ実体をビルドして流し込む
		/// 終わったところで Ready / Failed に切り替え、待っているスレッドを起こす
		/// </summary>
		template<typename T>
		void BuildIntoSlot(const Core::GUID& a_guid, const Handle<T>& a_handle, const ResourceBuildContext* a_pBuildContext);

		/// <summary>
		/// 対象スロットが Loading でなくなるまで待つ
		/// </summary>
		template<typename T>
		void WaitUntilLoaded(const Handle<T>& a_handle);

		// キャッシュ追加
		template<typename T>
		void RegisterCache(const Handle<T>& a_handle, const Core::GUID& a_guid);

		/// <summary>
		/// テンプレート特殊化された内部データを参照するための関数
		/// </summary>
		/// <typeparam name="T">型情報</typeparam>
		/// <returns>型に紐づく情報</returns>
		template<typename T>
		const ResourceData<T>& GetData() const;

		/// <summary>
		/// テンプレート特殊化された内部データを参照するための関数
		/// </summary>
		/// <typeparam name="T">型情報</typeparam>
		/// <returns>型に紐づく情報</returns>
		template<typename T>
		ResourceData<T>& RefData();

		// プールの取得
		template<typename T>
		const Pool::AtomicItemPool<T>& GetPool() const;
		template<typename T>
		Pool::AtomicItemPool<T>& RefPool();

	private:

		// 各リソースの実体プール
		ResourceData<Model> m_modelData;										// モデル
		ResourceData<Material> m_materialData;									// マテリアル
		ResourceData<Mesh> m_meshData;											// メッシュ
		ResourceData<AnimationData> m_animationData;							// アニメーション
		ResourceData<Texture> m_textureData;									// テクスチャ
		ResourceData<Shader> m_shaderData;										// シェーダー
		ResourceData<AnimatorAsset> m_animatorAssetData;						// アニメーターアセット
		ResourceData<ParticlesAsset> m_particleAssetData;						// パーティクル
		ResourceData<Prefab> m_prefabData;										// プレハブデータ
		ResourceData<Sound> m_soundData;										// サウンド
		ResourceData<AudioBehavior> m_audioBehaviorData;						// サウンドの流れ(始動/継続/終了)
		ResourceData<EffectPrefab> m_effectPrefabData;							// エフェクトプレハブ(炊いたら時間で消える大きな演出)
		ResourceData<EffectAsset> m_effectAssetData;							// エフェクト(パーティクル+メッシュ)
		ResourceData<Font> m_fontData;											// フォント(.ttf/.otf/.ttc)
		ResourceData<UIMaterial> m_uiMaterialData;								// UIマテリアル(UIに掛ける効果)
		ResourceData<Graphics::Pipeline::RenderingPipelineAsset> m_renderingPipelineData;	// レンダリングパイプライン(レンダーグラフの設計図)

		// 非同期ロードの実行先 : 未登録なら同期で読む
		std::atomic<Thread::JobSystem*> m_pJobSystem = nullptr;

		// サウンドの読み込み先 : 未登録ならサウンドは空のまま
		std::atomic<DirectX::AudioEngine*> m_pAudioEngine = nullptr;

		// プレハブの読み込みが型を引くワールドの持ち主 : 未登録ならプレハブは空のまま
		std::atomic<Scene::SceneManager*> m_pSceneManager = nullptr;

		// ジョブへ流して読み終わっていない数(RequestLoad で増え、ジョブの終わりで減る)
		std::atomic<uint32_t> m_inFlightLoadCount = 0;

		// 読み込み要求の記録 : 記録中かどうかだけはロック無しで見る(記録していない間の要求を遅くしない)
		std::atomic<bool>				m_isRecordingRequests = false;
		std::mutex						m_recordMutex;							// 下の2つを守る
		std::vector<Core::GUID>			m_recordedGUIDVec = {};					// 初めて要求された順
		std::unordered_set<Core::GUID>	m_recordedGUIDSet = {};					// 重複を弾く

		// GUIDとファイルパスの対応表。
		// ロードはGUIDから実ファイルを引くので、引く側(このクラス)が持ち主になる
		std::unique_ptr<AssetDatabase> m_upAssetDatabase = nullptr;

	//------------------------------------------------------------------------------------------
	// 生成と所有
	//
	// 持ち主は MainEngine(unique_ptr)。1プロセスに1つだけ作る。
	//
	// Instance() は「MainEngine が作った実体」を指す静的な入口で、自分では作らない。
	// 残してあるのは ResourceRef<T> のため : 値として資産の中に埋まっていて、
	// コピーや破棄のたびに参照カウントを触るので、引数でマネージャーを渡せない。
	// それ以外の場所はコンテキスト(EngineServices / PassContext / ResourceBuildContext)
	// のポインタから引くこと
	//------------------------------------------------------------------------------------------
	public:

		ResourceManager();
		~ResourceManager();
		NON_COPYABLE_NON_MOVABLE(ResourceManager);

		static ResourceManager& Instance()
		{
			ENGINE_ERRLOG(s_pInstance, "ResourceManager がまだ作られていないか、すでに壊れています");
			return *s_pInstance;
		}

	private:

		// MainEngine が作った実体 : コンストラクタで入り、デストラクタで外れる
		static inline ResourceManager* s_pInstance = nullptr;
	};
	// リソースの読み込み要求 : 待たない
	template<typename T>
	inline ResourceRef<T> ResourceManager::RequestLoad(const Core::GUID& a_guid)
	{
		bool _isOwner = false;
		const Handle<T> _handle = RequestSlot<T>(a_guid, _isOwner);

		// 既に読込中 / 読込済み : 何もしない
		if (!_isOwner) return ResourceRef<T>(_handle);

		auto* _pJobSystem = m_pJobSystem.load(std::memory_order_acquire);
		if (_pJobSystem == nullptr)
		{
			// ジョブシステムが未登録(起動直後・ツール実行など)ならその場で読む
			BuildIntoSlot<T>(a_guid, _handle, nullptr);
			return ResourceRef<T>(_handle);
		}

		// ジョブが持つ分の参照を先に立てておく。
		// 呼び出し元がすぐ参照を捨てても、ビルド中にGCで実体を引き抜かれないようにする
		AddRef(_handle);

		// 読み終わるまで数えておく : 積めなかったときは下で戻す
		m_inFlightLoadCount.fetch_add(1, std::memory_order_acq_rel);

		Thread::Job* _pJob = _pJobSystem->PushJob(
			[this, a_guid, _handle]()
			{
				BuildIntoSlot<T>(a_guid, _handle, nullptr);
				ReleaseRef(_handle);
				m_inFlightLoadCount.fetch_sub(1, std::memory_order_acq_rel);
			}
		);

		// 積めなかった(ジョブシステムが止まっている) : その場で読む。
		// 放っておくと Loading のまま誰も読まず、待つ側が永久に起きてこない
		if (_pJob == nullptr)
		{
			BuildIntoSlot<T>(a_guid, _handle, nullptr);
			ReleaseRef(_handle);
			m_inFlightLoadCount.fetch_sub(1, std::memory_order_acq_rel);
		}

		return ResourceRef<T>(_handle);
	}

	// リソースのロード : 実体ができるまで待つ
	template<typename T>
	inline ResourceRef<T> ResourceManager::LoadImmediate(const Core::GUID& a_guid, const ResourceBuildContext* a_pBuildContext)
	{
		bool _isOwner = false;
		const Handle<T> _handle = RequestSlot<T>(a_guid, _isOwner);

		if (_isOwner)
		{
			// 自分がビルド担当 : 呼び出しスレッドで読み切る
			BuildIntoSlot<T>(a_guid, _handle, a_pBuildContext);
		}
		else
		{
			// ほかの誰か(ジョブ含む)が読込中なら、実体ができるまで待つ
			WaitUntilLoaded<T>(_handle);
		}

		return ResourceRef<T>(_handle);
	}

	// スロットの確保
	template<typename T>
	inline Handle<T> ResourceManager::RequestSlot(const Core::GUID& a_guid, bool& a_outIsOwner)
	{
		auto& _data = RefData<T>();

		a_outIsOwner = false;

		// 読み込み要求の記録 : 読込済みでも数えるので、キャッシュを見る前に通す
		if (m_isRecordingRequests.load(std::memory_order_acquire) && BuildDepth() == 0)
		{
			RecordRequest(a_guid);
		}

		std::lock_guard _lock(_data.cacheMutex);

		auto _it = _data.cache.find(a_guid);
		if (_it != _data.cache.end())
		{
			// 実体が生きているならそれを使う(読込中でも同じハンドルを返す)
			if (_data.pool.IsValid(_it->second)) return _it->second;

			// 実体だけ消えた古い登録が残っている : 捨てて取り直す
			_data.cache.erase(_it);
		}

		// 空の実体でスロットを押さえる。
		// ここでハンドルを確定させておくことで、中身ができる前に呼び出し元へ返せる
		const Handle<T> _handle = _data.pool.Add(T());

		// 状態を Loading にしてからキャッシュへ載せる。
		// 逆にすると、中身も状態もできていないハンドルが他スレッドから見えてしまう
		EnsureSlot<T>(_handle.GetIndex());
		if (auto* _pSlot = RefSlot<T>(_handle.GetIndex()))
		{
			_pSlot->state.store(EResourceState::Loading, std::memory_order_release);
		}

		_data.cache[a_guid] = _handle;

		a_outIsOwner = true;
		return _handle;
	}

	// 実体のビルドと流し込み
	template<typename T>
	inline void ResourceManager::BuildIntoSlot(const Core::GUID& a_guid, const Handle<T>& a_handle, const ResourceBuildContext* a_pBuildContext)
	{
		auto& _data = RefData<T>();

		// ここから先の要求はローダーの中からのもの(読み込み要求の記録で外す)。
		// 例外は下で受け止めるので、抜けるところで必ず戻る
		++BuildDepth();

		// 途中で何が起きても Loading のまま放置しない。
		// 放置すると、このリソースを待っているスレッドが永久に起きてこない
		try
		{
			std::string _filePath = m_upAssetDatabase->GetFilePathFromGUID(a_guid);	// パス取得

			// アセットの読み込みをログ出力する(実際に読み込む時=キャッシュミス時のみ)。
			// テクスチャやモデルなど Archive を通らないアセットもここで拾えるようにする。
			if (const auto* _prop = m_upAssetDatabase->GetAssetProperty(a_guid))
			{
				ENGINE_LOG("[Resource] ロード : %s \"%s\"", _prop->type.c_str(), _prop->fileName.c_str());
			}
			else
			{
				ENGINE_LOG("[Resource] ロード : %s", _filePath.c_str());
			}

			//----------------------------------------------------------------------------------
			// ローダーへは必ずコンテキストを渡す
			//
			// 呼び出し元がバッチを持っていない(非同期ロードなど)ときも、
			// 登録先のマネージャー(自分)とアセットデータベースだけは載せておく。
			// ローダーの中で他のリソースを読む(パーティクル→テクスチャなど)ときの行き先になる
			//----------------------------------------------------------------------------------
			ResourceBuildContext _context = a_pBuildContext ? *a_pBuildContext : ResourceBuildContext{};
			if (!_context.pResourceManager) _context.pResourceManager = this;
			if (!_context.pAssetDatabase) _context.pAssetDatabase = m_upAssetDatabase.get();
			if (!_context.pAudioEngine) _context.pAudioEngine = m_pAudioEngine.load(std::memory_order_acquire);
			if (!_context.pSceneManager) _context.pSceneManager = m_pSceneManager.load(std::memory_order_acquire);

			T _resourceData = DefaultLoader<T>::LoadFromFile(_filePath, &_context);	// リソースのビルド

			// 押さえておいたスロットへ流し込む
			_data.pool.Write(a_handle, [&_resourceData](T& a_dst) { a_dst = std::move(_resourceData); });

			// パスを引けなかった場合、ビルドされたのは空のリソース。
			// 実体は登録したまま使用不可の印だけ付けておく
			// (毎フレーム読み直しに行かないよう、キャッシュには残す)
			if (_filePath.empty())
			{
				ENGINE_WARNING("[Resource] パスを解決できませんでした : %s", a_guid.String().c_str());
				SetState(a_handle, EResourceState::Failed);
			}
			else
			{
				SetState(a_handle, EResourceState::Ready);
			}
		}
		catch (const std::exception& _e)
		{
			ENGINE_WARNING("[Resource] ビルド中に例外が発生しました : %s", _e.what());
			SetState(a_handle, EResourceState::Failed);
		}
		catch (...)
		{
			ENGINE_WARNING("[Resource] ビルド中に不明な例外が発生しました");
			SetState(a_handle, EResourceState::Failed);
		}

		--BuildDepth();

		// 完了を待っているスレッドを起こす。
		// 待機側と同じロックを一度通してから通知しないと、起こし損ねる
		{
			std::lock_guard _lock(_data.cacheMutex);
		}
		_data.loadingCondition.notify_all();
	}

	// 読み込み完了待ち
	template<typename T>
	inline void ResourceManager::WaitUntilLoaded(const Handle<T>& a_handle)
	{
		const auto* _pSlot = RefSlot<T>(a_handle.GetIndex());
		if (_pSlot == nullptr) return;

		auto& _data = RefData<T>();

		std::unique_lock _lock(_data.cacheMutex);

		_data.loadingCondition.wait(
			_lock,
			[_pSlot]()
			{
				return _pSlot->state.load(std::memory_order_acquire) != EResourceState::Loading;
			}
		);
	}
	// リソースの追加
	template<typename T>
	inline ResourceRef<T> ResourceManager::Add(T&& a_resource)
	{
		auto _handle = RefPool<T>().Add(std::move(a_resource));

		// 出来上がったものを渡されているので、登録した時点で使用可能
		EnsureSlot<T>(_handle.GetIndex());
		if (auto* _pSlot = RefSlot<T>(_handle.GetIndex()))
		{
			_pSlot->state.store(EResourceState::Ready, std::memory_order_release);
		}

		return ResourceRef<T>(_handle);
	}
	template<typename T>
	inline void ResourceManager::AddRef(const Handle<T>& a_handle)
	{
		EnsureSlot<T>(a_handle.GetIndex());

		if (auto* _pSlot = RefSlot<T>(a_handle.GetIndex()))
		{
			_pSlot->refCount.fetch_add(1, std::memory_order_acq_rel);
		}
	}
	//======================================================================================
	// 生ハンドルの取得と返却
	//======================================================================================
	template<typename T>
	inline void ResourceManager::AcquireRequest(Handle<T>& a_inoutHandle, const Core::GUID& a_guid)
	{
		if (!a_guid.IsValid())
		{
			a_inoutHandle = {};
			return;
		}

		// ResourceRef は寿命が尽きると参照を返してしまうので、
		// ハンドルだけ取り出してから自前で1つ取る
		Handle<T> _handle = RequestLoad<T>(a_guid).GetRaw();
		if (!IsValid(_handle))
		{
			a_inoutHandle = {};
			return;
		}

		AddRef(_handle);
		a_inoutHandle = _handle;
	}

	template<typename T>
	inline void ResourceManager::AcquireImmediate(Handle<T>& a_inoutHandle, const Core::GUID& a_guid,
		const ResourceBuildContext* a_pBuildContext)
	{
		if (!a_guid.IsValid())
		{
			a_inoutHandle = {};
			return;
		}

		Handle<T> _handle = LoadImmediate<T>(a_guid, a_pBuildContext).GetRaw();
		if (!IsValid(_handle))
		{
			a_inoutHandle = {};
			return;
		}

		AddRef(_handle);
		a_inoutHandle = _handle;
	}

	template<typename T>
	inline void ResourceManager::ReleaseHandle(Handle<T>& a_inoutHandle)
	{
		if (IsValid(a_inoutHandle))
		{
			ReleaseRef(a_inoutHandle);
		}

		a_inoutHandle = {};
	}

	template<typename T>
	inline Handle<T> ResourceManager::AddResourceAndGUID(T&& a_resource, const Core::GUID& a_guid)
	{
		auto _handle = RefPool<T>().Add(std::move(a_resource));
		RegisterCache<T>(_handle, a_guid); // キャッシュにも登録

		// 出来上がったものを渡されているので、登録した時点で使用可能
		EnsureSlot<T>(_handle.GetIndex());
		if (auto* _pSlot = RefSlot<T>(_handle.GetIndex()))
		{
			_pSlot->state.store(EResourceState::Ready, std::memory_order_release);
		}

		return _handle;                    // ハンドルを返す
	}
	template<typename T>
	inline void ResourceManager::Remove(const Handle<T>& a_handle)
	{
		// スロットが空くので状態も戻す。
		// 同じ添え字が別のリソースに再利用されたときに、前の状態を引き継がないため
		SetState(a_handle, EResourceState::Empty);

		RefPool<T>().Remove(a_handle);
	}
	template<typename T>
	inline void ResourceManager::ReleaseRef(const Handle<T>& a_handle)
	{
		auto* _pSlot = RefSlot<T>(a_handle.GetIndex());
		if (!_pSlot) return;

		// 0 を下回らせない。
		// 単純な fetch_sub だと、同時に呼ばれたときに 0 を通り越して巻き上がる
		uint16_t _current = _pSlot->refCount.load(std::memory_order_acquire);
		while (_current > 0)
		{
			if (_pSlot->refCount.compare_exchange_weak(
					_current, static_cast<uint16_t>(_current - 1),
					std::memory_order_acq_rel, std::memory_order_acquire))
			{
				break;
			}
		}
	}
	template<typename T>
	inline const T* ResourceManager::Get(const Handle<T>&a_handle) const
	{
		return GetPool<T>().Get(a_handle);
	}
	template<typename T>
	inline const T* ResourceManager::Get(const ResourceRef<T>& a_handle) const
	{
		return Get(a_handle.GetRaw());
	}
	template<typename T>
	inline T* ResourceManager::Ref(const Handle<T>& a_handle)
	{
		return RefPool<T>().Ref(a_handle);
	}

	template<typename T>
	inline T* ResourceManager::Ref(const ResourceRef<T>& a_handle)
	{
		return Ref(a_handle.GetRaw());
	}

	template<typename T>
	inline void ResourceManager::SweepUnused()
	{
		auto& _data = RefData<T>();
		const size_t _poolSize = _data.pool.Size();
		if (_poolSize == 0) return;

		// スロットはプールの大きさまで揃えてから回す
		EnsureSlot<T>(static_cast<uint16_t>(_poolSize - 1));

		for (size_t _i = 0; _i < _poolSize; ++_i)
		{
			const uint16_t _index = static_cast<uint16_t>(_i);

			// 存在チェック
			if (!_data.pool.IsOccupied(_index)) continue;

			auto* _pSlot = RefSlot<T>(_index);
			if (_pSlot == nullptr) continue;

			// 読込中のスロットは参照カウントが立つ前なので、
			// ここで消すとビルド中の書き込み先を引き抜いてしまう
			if (_pSlot->state.load(std::memory_order_acquire) == EResourceState::Loading) continue;

			// 誰も持っていないものだけ片付ける
			if (_pSlot->refCount.load(std::memory_order_acquire) != 0) continue;

			// インデックスと世代から正しいハンドルを復元する
			const uint16_t _generation = _data.pool.GetGeneration(_index);
			Handle<T> _targetHandle(_index, _generation);

			// キャッシュ (GUIDマップ) から削除
			{
				std::lock_guard _lock(_data.cacheMutex);
				for (auto _it = _data.cache.begin(); _it != _data.cache.end(); )
				{
					if (_it->second == _targetHandle)
					{
						_it = _data.cache.erase(_it);
					}
					else
					{
						++_it;
					}
				}
			}

			// 実体の後始末はプールのロックの中で行う
			// (自分で後始末を持つ型だけ。持たない型はプールから外すだけでよい)
			if constexpr (requires (T& a_resource) { a_resource.Release(); })
			{
				_data.pool.Write(_targetHandle, [](T& a_resource) { a_resource.Release(); });
			}
			_data.pool.Remove(_targetHandle);

			// 空いたスロットとして状態を戻す
			_pSlot->state.store(EResourceState::Empty, std::memory_order_release);
		}
	}

	template<typename T>
	inline void ResourceManager::ReleaseData()
	{
		auto& _data = RefData<T>();

		//------------------------------------------------------------------
		// まだ誰かが持っているものも、プールごと捨てる前に後始末を通す。
		//
		// プールを空にするだけだとデストラクタしか走らない。
		// メッシュなら BLAS が遅延解放を通らず、GPU が最後のフレームで
		// 使っている最中に最終解放される(デバッグレイヤーが CORRUPTION で止まる)。
		// やり方は SweepUnused と同じ(自分で後始末を持つ型だけ)
		//------------------------------------------------------------------
		if constexpr (requires (T& a_resource) { a_resource.Release(); })
		{
			const size_t _poolSize = _data.pool.Size();
			for (size_t _i = 0; _i < _poolSize; ++_i)
			{
				const uint16_t _index = static_cast<uint16_t>(_i);
				if (!_data.pool.IsOccupied(_index)) continue;

				Handle<T> _handle(_index, _data.pool.GetGeneration(_index));
				_data.pool.Write(_handle, [](T& a_resource) { a_resource.Release(); });
			}
		}

		_data.pool.Release();

		{
			std::lock_guard _lock(_data.cacheMutex);
			_data.cache.clear();
		}
		// 読み込み待ちで寝ているスレッドを取り残さない
		_data.loadingCondition.notify_all();

		{
			std::unique_lock _lock(_data.slotMutex);
			_data.slots.clear();
		}
	}

	//==========================================================================================
	// リソースの状態
	//==========================================================================================
	template<typename T>
	inline EResourceState ResourceManager::GetState(const Handle<T>& a_handle)
	{
		// 未登録・世代違いのハンドルは状態を持たない扱い
		if (!IsValid(a_handle)) return EResourceState::Empty;

		const auto* _pSlot = RefSlot<T>(a_handle.GetIndex());
		if (_pSlot == nullptr) return EResourceState::Empty;

		return _pSlot->state.load(std::memory_order_acquire);
	}

	template<typename T>
	inline EResourceState ResourceManager::GetState(const Core::GUID& a_guid)
	{
		return GetState<T>(GetCache<T>(a_guid));
	}

	template<typename T>
	inline void ResourceManager::SetState(const Handle<T>& a_handle, EResourceState a_state)
	{
		if (!IsValid(a_handle)) return;

		EnsureSlot<T>(a_handle.GetIndex());

		if (auto* _pSlot = RefSlot<T>(a_handle.GetIndex()))
		{
			_pSlot->state.store(a_state, std::memory_order_release);
		}
	}

	template<typename T>
	inline bool ResourceManager::IsReady(const Handle<T>& a_handle)
	{
		return GetState<T>(a_handle) == EResourceState::Ready;
	}

	template<typename T>
	inline bool ResourceManager::IsReady(const ResourceRef<T>& a_ref)
	{
		return IsReady<T>(a_ref.GetRaw());
	}

	// スロットを必要な本数まで伸ばす
	template<typename T>
	inline void ResourceManager::EnsureSlot(uint16_t a_index)
	{
		auto& _data = RefData<T>();
		const size_t _needSize = static_cast<size_t>(a_index) + 1;

		// 足りているなら共有ロックだけで抜ける(ここが大半)
		{
			std::shared_lock _lock(_data.slotMutex);
			if (_data.slots.size() >= _needSize) return;
		}

		std::unique_lock _lock(_data.slotMutex);

		// ロックを取り直す間に他スレッドが伸ばしている可能性があるため、改めて見る
		while (_data.slots.size() < _needSize)
		{
			_data.slots.push_back(std::make_unique<ResourceSlot>());
		}
	}

	// スロットの取り出し
	template<typename T>
	inline ResourceSlot* ResourceManager::RefSlot(uint16_t a_index)
	{
		auto& _data = RefData<T>();

		std::shared_lock _lock(_data.slotMutex);
		if (a_index >= _data.slots.size()) return nullptr;

		// 実体は個別確保しているので、ロックを抜けてもこのポインタは動かない
		return _data.slots[a_index].get();
	}

	// プールの取得
	template<typename T>
	inline const Pool::AtomicItemPool<T>& ResourceManager::GetPool() const
	{
		return GetData<T>().pool;
	}

	// プールの参照
	template<typename T>
	inline Pool::AtomicItemPool<T>& ResourceManager::RefPool()
	{
		return RefData<T>().pool;
	}

	// キャッシュアクセス
	template<typename T>
	inline Handle<T> ResourceManager::GetCache(const Core::GUID& a_guid)
	{
		auto& _data = RefData<T>();

		std::lock_guard _lock(_data.cacheMutex);

		auto _it = _data.cache.find(a_guid);
		if (_it != _data.cache.end())
		{
			return _it->second;
		}
		return Handle<T>();
	}
	template<typename T>
	inline Core::GUID ResourceManager::GetCache(const Handle<T>& a_handle)
	{
		auto& _data = RefData<T>();

		std::lock_guard _lock(_data.cacheMutex);

		for (const auto& [_guid, _h] : _data.cache)
		{
			if (_h == a_handle)
			{
				return _guid;
			}
		}
		ENGINE_LOG("登録されていないハンドルです");
		return Core::GUID();
	}
	// キャッシュ削除
	template<typename T>
	inline void ResourceManager::RemoveCache(const Handle<T>& a_handle)
	{
		auto& _data = RefData<T>();

		std::lock_guard _lock(_data.cacheMutex);

		for (auto _it = _data.cache.begin(); _it != _data.cache.end(); ++_it)
		{
			if (_it->second == a_handle)
			{
				_data.cache.erase(_it); // ハンドルが一致したものを消す
				break;
			}
		}
	}

	template<typename T>
	inline void ResourceManager::RemoveCache(const Core::GUID& a_guid)
	{
		auto& _data = RefData<T>();

		std::lock_guard _lock(_data.cacheMutex);
		_data.cache.erase(a_guid); // GUIDから一発で削除
	}

	template<typename T>
	inline bool ResourceManager::Has(const Core::GUID& a_guid)
	{
		auto& _data = RefData<T>();

		std::lock_guard _lock(_data.cacheMutex);
		return _data.cache.contains(a_guid);
	}

	// ハンドルが使用可能かどうか
	template<typename T>
	inline bool ResourceManager::IsValid(const Handle<T>& a_handle)
	{
		return GetPool<T>().IsValid(a_handle);
	}

	// 型ごとにキャッシュに登録
	template<typename T>
	inline void ResourceManager::RegisterCache(const Handle<T>& a_handle, const Core::GUID& a_guid)
	{
		auto& _data = RefData<T>();

		std::lock_guard _lock(_data.cacheMutex);
		_data.cache[a_guid] = a_handle;
	}

	// プールの参照
	template<> inline ResourceData<Model>& ResourceManager::RefData<Model>() { return  m_modelData; }
	template<> inline ResourceData<Material>& ResourceManager::RefData<Material>() { return m_materialData; }
	template<> inline ResourceData<Mesh>& ResourceManager::RefData<Mesh>() { return m_meshData; }
	template<> inline ResourceData<AnimationData>& ResourceManager::RefData<AnimationData>() { return m_animationData; }
	template<> inline ResourceData<Texture>& ResourceManager::RefData<Texture>() { return m_textureData; }
	template<> inline ResourceData<Shader>& ResourceManager::RefData<Shader>() { return m_shaderData; }
	template<> inline ResourceData<AnimatorAsset>& ResourceManager::RefData<AnimatorAsset>() { return m_animatorAssetData; }
	template<> inline ResourceData<ParticlesAsset>& ResourceManager::RefData<ParticlesAsset>() { return m_particleAssetData; }
	template<> inline ResourceData<Prefab>& ResourceManager::RefData<Prefab>() { return m_prefabData; }
	template<> inline ResourceData<Sound>& ResourceManager::RefData<Sound>() { return m_soundData; }
	template<> inline ResourceData<AudioBehavior>& ResourceManager::RefData<AudioBehavior>() { return m_audioBehaviorData; }
	template<> inline ResourceData<EffectAsset>& ResourceManager::RefData<EffectAsset>() { return m_effectAssetData; }
	template<> inline ResourceData<EffectPrefab>& ResourceManager::RefData<EffectPrefab>() { return m_effectPrefabData; }
	template<> inline ResourceData<Font>& ResourceManager::RefData<Font>() { return m_fontData; }
	template<> inline ResourceData<UIMaterial>& ResourceManager::RefData<UIMaterial>() { return m_uiMaterialData; }
	template<> inline ResourceData<Graphics::Pipeline::RenderingPipelineAsset>& ResourceManager::RefData<Graphics::Pipeline::RenderingPipelineAsset>() { return m_renderingPipelineData; }

	// プールの取得
	template<> inline const ResourceData<Model>& ResourceManager::GetData<Model>() const { return  m_modelData; }
	template<> inline const ResourceData<Material>& ResourceManager::GetData<Material>() const { return m_materialData; }
	template<> inline const ResourceData<Mesh>& ResourceManager::GetData<Mesh>() const { return m_meshData; }
	template<> inline const ResourceData<AnimationData>& ResourceManager::GetData<AnimationData>() const { return m_animationData; }
	template<> inline const ResourceData<Texture>& ResourceManager::GetData<Texture>() const { return m_textureData; }
	template<> inline const ResourceData<Shader>& ResourceManager::GetData<Shader>() const { return m_shaderData; }
	template<> inline const ResourceData<AnimatorAsset>& ResourceManager::GetData<AnimatorAsset>() const { return m_animatorAssetData; }
	template<> inline const ResourceData<ParticlesAsset>& ResourceManager::GetData<ParticlesAsset>() const { return m_particleAssetData; }
	template<> inline const ResourceData<Prefab>& ResourceManager::GetData<Prefab>() const { return m_prefabData; }
	template<> inline const ResourceData<Sound>& ResourceManager::GetData<Sound>() const { return m_soundData; }
	template<> inline const ResourceData<AudioBehavior>& ResourceManager::GetData<AudioBehavior>() const { return m_audioBehaviorData; }
	template<> inline const ResourceData<EffectAsset>& ResourceManager::GetData<EffectAsset>() const { return m_effectAssetData; }
	template<> inline const ResourceData<EffectPrefab>& ResourceManager::GetData<EffectPrefab>() const { return m_effectPrefabData; }
	template<> inline const ResourceData<Font>& ResourceManager::GetData<Font>() const { return m_fontData; }
	template<> inline const ResourceData<UIMaterial>& ResourceManager::GetData<UIMaterial>() const { return m_uiMaterialData; }
	template<> inline const ResourceData<Graphics::Pipeline::RenderingPipelineAsset>& ResourceManager::GetData<Graphics::Pipeline::RenderingPipelineAsset>() const { return m_renderingPipelineData; }
}

namespace Engine
{
	// =========================================================
	// ResourceRef の中身を実装
	// リソースマネージャーの実装が見えている必要があるため
	// =========================================================

	// コンストラクタ
	template<typename T>
	inline ResourceRef<T>::ResourceRef(Handle<T> a_h) : m_handle(a_h)
	{
		if (Resource::ResourceManager::Instance().IsValid(m_handle))
		{
			Resource::ResourceManager::Instance().AddRef(m_handle);
		}
	}

	// デストラクタ
	template<typename T>
	inline ResourceRef<T>::~ResourceRef() {
		// ResourceRef が MainEngine の持ち物(AudioManager のサウンドインスタンスなど)に
		// 保持されている場合、このデストラクタは MainEngine の破棄(静的変数の破棄フェーズ)で走ることがある。
		// そのとき ResourceManager が先に壊されていると Instance() は
		// 破棄済みオブジェクトへの参照を返すため、必ず生存確認する
		if (!Resource::ResourceManager::IsAlive()) return;

		if (Resource::ResourceManager::Instance().IsValid(m_handle))
		{
			Resource::ResourceManager::Instance().ReleaseRef(m_handle);
		}
	}

	// コピーコンストラクタ
	template<typename T>
	inline ResourceRef<T>::ResourceRef(const ResourceRef& a_other) : m_handle(a_other.m_handle)
	{
		if (Resource::ResourceManager::Instance().IsValid(m_handle))
		{
			Resource::ResourceManager::Instance().AddRef(m_handle);
		}
	}

	// コピー代入演算子
	template<typename T>
	inline ResourceRef<T>& ResourceRef<T>::operator=(const ResourceRef& a_other)
	{
		if (this != &a_other) {
			// 古いもののカウントを減らし、新しいものを増やす
			if (Resource::ResourceManager::Instance().IsValid(m_handle))
			{
				Resource::ResourceManager::Instance().ReleaseRef(m_handle);
			}
			m_handle = a_other.m_handle;
			if (Resource::ResourceManager::Instance().IsValid(m_handle))
			{
				Resource::ResourceManager::Instance().AddRef(m_handle);
			}
		}
		return *this;
	}

	// ムーブコンストラクタ
	template<typename T>
	inline ResourceRef<T>::ResourceRef(ResourceRef&& a_other) noexcept : m_handle(a_other.m_handle)
	{
		a_other.m_handle = {}; // 元のハンドルを空にする
	}

	// ムーブ代入演算子
	template<typename T>
	inline ResourceRef<T>& ResourceRef<T>::operator=(ResourceRef&& a_other) noexcept
	{
		if (this != &a_other) {
			if (Resource::ResourceManager::Instance().IsValid(m_handle))
			{
				Resource::ResourceManager::Instance().ReleaseRef(m_handle);
			}
			m_handle = a_other.m_handle;
			a_other.m_handle = {};
		}
		return *this;
	}
}
