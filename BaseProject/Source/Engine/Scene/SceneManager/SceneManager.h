#pragma once

#include "Engine/Scene/SceneAmbient/SceneAmbientMood.h"

namespace Engine
{
	class MainEngine;

	namespace ECS
	{
		class World;
	}
	namespace Graphics
	{
		class RenderContext;
		struct DirectionalLight;
	}
	namespace GameObject
	{
		class GameObjectManager;
	}
	namespace Resource
	{
		class AssetDatabase;
		class ResourceManager;
	}
}

namespace Engine::Scene
{
	class BaseScene;

	//==================================================================================================
	// シーンの切り替え操作指定
	//==================================================================================================
	enum class ESceneChangeType
	{
		Push,		// 重ねる
		Pop,		// 一つ消去
		Replace,	// 切り替え
		Clear,		// 全消去
		ReplaceAll	// 全部外して切り替え(重ねたシーンの下まで消す。リザルトをステージに重ねたときの戻り道)
	};

	//==================================================================================================
	// シーンの状態、遷移を管理するクラス
	//
	// 実体は MainEngine が持ち、EngineServices::pSceneManager で配る。
	// 解放(Release)はアプリ側がゲームより先に呼ぶ
	//==================================================================================================
	class SceneManager
	{
	public:

		// a_pEngine : 持ち主。描画・リソース・開発ツールへの通知はここから引く
		explicit SceneManager(MainEngine* a_pEngine);
		~SceneManager();
		NON_COPYABLE_NON_MOVABLE(SceneManager);

		//------------------------------------------------------------------------------------------
		// メイン処理
		//------------------------------------------------------------------------------------------
		void Release();										// 解放
		// 更新 : シーンの切り替え命令もここで消化する。
		// 行き先のファイルをGUIDから引き、シーンを外したときに使われなくなったリソースを捨てるので、
		// リソースマネージャーを受け取る(アセットデータベースもこの中)
		void Update(Resource::ResourceManager& a_resourceManager, float a_dt);
		void Draw();										// 描画

		//------------------------------------------------------------------------------------------
		// ワールドの作り手
		//
		// シーンが持つワールドの「種類」を決めるのは上位層(App::ECS::APPWorld)。
		// エンジンは基盤の Engine::ECS::World としてしか触らないので、
		// 実体を作るところだけ差し込んでもらう。
		//
		// 差し込み忘れるとシーンにワールドが無い状態になるため、CreateWorld はログを出す。
		//------------------------------------------------------------------------------------------
		using WorldFactory = std::function<std::unique_ptr<Engine::ECS::World>()>;

		void SetWorldFactory(WorldFactory a_factory);			// セット
		std::unique_ptr<Engine::ECS::World> CreateWorld();		// 呼び出し

		//------------------------------------------------------------------------------------------
		// シーンの新規作成
		//------------------------------------------------------------------------------------------

		/// <summary>
		/// 空のシーンをアセットとして作る
		/// </summary>
		/// <param name="a_path">Asset/Scenes/ からの相対フォルダ。空でもよい</param>
		/// <param name="a_name">シーン名。フォルダ名にもファイル名にも使う</param>
		/// <returns>作ったシーンのGUID。失敗したら無効なGUID</returns>
		/// <remarks>
		/// 中身は空のまま書き出すだけで、今開いているシーンには触らない。
		/// 開きたい場合は返ったGUIDで ReserveChangeScene を呼ぶこと。
		///
		/// 書き出しは実物の BaseScene を1つ作って Archive へ流す。
		/// 「空のシーンファイルの中身」を手で組み立てると、
		/// BaseScene::Archive に項目が増えたときに古い形のまま作り続けてしまう
		/// (バイナリ(.obscene)は並び順で読むので、そのまま壊れる)
		/// </remarks>
		Core::GUID CreateEmptyScene(Resource::AssetDatabase& a_assetDB, const std::string& a_path, const std::string& a_name);

		//------------------------------------------------------------------------------------------
		// シーンの切り替え
		//------------------------------------------------------------------------------------------

		/// <summary>
		/// シーンの切り替え命令
		/// </summary>
		/// <param name="a_nextScene">切り替え先のシーンタイプ</param>
		/// <param name="a_changeType">切り替え方法</param>
		void ReserveChangeScene(const Core::GUID& a_guid, const ESceneChangeType& a_changeType);

		/// <summary>
		/// 更新するのを一番上のシーンだけにするか
		/// </summary>
		/// <remarks>
		/// 既定は true。ポーズ画面のように重ねたシーンを出している間、
		/// 後ろのシーンは描画だけ続けて更新は止まる。
		/// 後ろも動かしたい重ね方をするときだけ false にすること。
		/// </remarks>
		void SetUpdateTopSceneOnly(bool a_isTopOnly) { m_isUpdateTopSceneOnly = a_isTopOnly; }
		bool IsUpdateTopSceneOnly() const { return m_isUpdateTopSceneOnly; }

		//------------------------------------------------------------------------------------------
		// 取得
		//------------------------------------------------------------------------------------------

		/// <summary>
		/// 現在のシーンのワールドを参照
		/// </summary>
		/// <remarks>
		/// 読み込みを進めている最中(UpdateSceneLoad の間)は、そのシーンのワールドを返す。
		///
		/// 読み込みの中では先読み・組み立て・初期化のフェーズが走り、ワールドを要るもの
		/// (プレハブのように、コンポーネントのメタ情報が無いと読めないリソース)も読まれる。
		/// ロード画面はスタックに乗らないので、ここで行き先を教えないと別のシーンのワールドか
		/// nullptr が返ってしまい、読めなかったことに気付かないまま空の実体がキャッシュに載る。
		/// </remarks>
		Engine::ECS::World* RefWorld();

		/// <summary>
		/// 現在の一番上のシーンを取得
		/// </summary>
		/// <returns>ベースシーンポインタ</returns>
		BaseScene* RefCurrentTopScene();

		/// <summary>
		/// 現在のシーンのECS外オブジェクトマネージャーを参照(エディター用)
		/// </summary>
		GameObject::GameObjectManager* RefGameObjectManager();

		/// <summary>
		/// 環境設定を GraphicsEngine へ流し込んでいるシーン
		/// </summary>
		/// <remarks>
		/// 積んであるシーンのうち、環境設定を使う(SceneAmbient::IsEnabled)一番上のもの。
		/// 1つも無ければ nullptr(環境光・フォグ・空・平行光なしで描かれる)
		/// </remarks>
		BaseScene* RefAmbientSourceScene();

		/// <summary>
		/// 環境設定に演出(暗くする・フォグを濃くする)を掛けるよう頼む
		/// </summary>
		/// <remarks>
		/// 今フレームだけ効く。掛けている間は毎フレーム頼むこと(頼まなくなれば次のフレームからシーンの値に戻る)。
		/// 後ろに重ねたシーンは更新が止まっても描画は回るので、頼むのは描画(GameObject::Draw など)の中でよい。
		/// 頼む側が複数あると最後に頼んだものが勝つ
		/// </remarks>
		void RequestAmbientMood(const SceneAmbientMood& a_mood) { m_ambientMood = a_mood; }

		/// <summary>
		/// 先読み一覧を計測しているシーン
		/// </summary>
		/// <remarks>
		/// 計測フラグ(SceneConfig)を立てて開いたシーン。閉じるまで計測が続く。
		/// 計測は同時に1つだけなので、計測していなければ nullptr
		/// </remarks>
		const BaseScene* GetPreLoadRecordingScene() const { return m_pPreLoadRecordingScene; }

		//------------------------------------------------------------------------------------------
		// 使ったアセットの記録(起動から終了まで)
		//
		// 起動設定の isRecordUsedAssets が ON のとき、ResourceManager へ読み込みが要求されたアセットのGUIDを
		// 初めて要求された順に重複なく溜める。一括コンバートやビルド用フォルダへの移動に使う想定
		// (それらはまだ無く、ここは溜めるところまで)。
		//
		// ・シーンの先読み一覧の計測(BeginRecordPreLoadAssets)とは別物で、止めない・捨てない
		// ・ローダーの中から読まれたもの(モデルの中のメッシュなど)も数える。持ち出す対象を漏らさないため
		// ・ワーカースレッドからの要求も入るので、受け取りは複製で返す
		// ・OFF(提出用)では溜めない(空のまま)
		//------------------------------------------------------------------------------------------
		void RecordUsedAsset(const Core::GUID& a_guid);					// 1件記録する(ResourceManager が呼ぶ。スレッドセーフ)
		std::vector<Core::GUID> GetUsedAssetGUIDs() const;				// ここまでの記録を複製して返す
		bool IsRecordingUsedAssets() const { return m_isRecordUsedAssets; }

		//------------------------------------------------------------------------------------------
		// 読み込みとロード画面
		//
		// シーンは積んだ(Push / Replace)フレームでは組み立てず、毎フレーム少しずつ読み込む
		// (BaseScene::UpdateLoad)。読み込みが済むまでそのシーンは更新も描画もされない。
		//
		// 画面が染まり切った(フェードアウトが済んだ)まま読み込みが一定時間
		// (LOADING_SCREEN_DELAY_SEC)より長引いたら、ロード画面のシーンを一番上に重ねて出す。
		// 出し入れは染めた色から戻す / 染めた色へ落とす(LOADING_SCREEN_FADE_SEC)で、
		// 一度出したら短くても LOADING_SCREEN_MIN_SEC は見せる(一瞬だけ映ってちらつかないように)。
		// 出ている間はスタックのシーンを更新も描画もしない : 読み込みが済んだ行き先が
		// ロード画面の裏で動き出さないように(ステージの時間が進む・敵が撃ってくる)。
		// 重ねる(Push)ときは染めないので出さない(後ろのシーンがそのまま見えている)。
		//
		// ロード画面はスタックの外で常駐させるので、シーンの切り替えの掃除(SweepUnusedAll)で消えない。
		//------------------------------------------------------------------------------------------

		/// <summary>
		/// ロード画面に使うシーンを設定する
		/// </summary>
		/// <remarks>
		/// 起動時に一度だけ呼ぶ想定。設定したシーンもほかのシーンと同じく少しずつ読み込まれ、
		/// 読み込みが済むまでは出さない。無効なGUIDなら外す(ロード画面なしで動く)
		/// </remarks>
		void SetLoadingScreen(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid);

		// 読み込み中のシーンがあるか
		bool IsLoading() const;

		// 読み込みの進み具合(0〜1) : 読み込み中のシーンのうち一番上のもの。無ければ 1
		float GetLoadProgress() const;

		// ロード画面を出しているか
		bool IsLoadingScreenVisible() const;

		//------------------------------------------------------------------------------------------
		// シーン遷移のフェード
		//
		// シーンの切り替え(Replace / ReplaceAll)のとき、今のシーンの設定(SceneConfig)に従って画面を染めてから
		// 切り替え、行き先のシーンの読み込みが済んだら行き先の設定で染めた画面から戻す。
		// 重ねる / 外す(ポーズ画面など)ではかけない。最初のシーンを開くときはフェードインだけ。
		// 染めている間も今のシーンは動き続ける。
		// 濃さは時間に比例させず、出だしと終わりをなだらかにする(EaseFade)
		//------------------------------------------------------------------------------------------
		bool IsFading() const { return m_fadeAlpha > 0.0f; }

		//------------------------------------------------------------------------------------------
		// 重ねたシーンの後ろのぼかし
		//
		// 一番上のシーンが設定(SceneConfig::GetBackdrop)でぼかしを使い、下に別のシーンがあるとき、
		// 後ろの 3D をぼかす(SceneView::SetBackdropBlur)。掛け外しは設定の時間でなだらかに。
		// 下のシーンは止まっている(一番上だけ更新)ので、止まった瞬間の画がぼけて残る
		//------------------------------------------------------------------------------------------
		float GetBackdropBlur() const { return m_backdropBlur; }

	private:

		enum class EFadeState
		{
			None,		// かけていない
			Out,		// 染めている最中(終わったら切り替える)
			WaitReady,	// 染まったまま行き先の読み込みを待っている
			In,			// 戻している最中
		};

		enum class ELoadingScreenState
		{
			Hidden,		// 出していない
			In,			// 染めた色から戻している最中
			Shown,		// 出している
			Out,		// 染めた色へ落としている最中(終わったら隠す)
		};

		void UpdateFade(float a_dt);					// 毎フレーム
		bool TryBeginFadeOut();							// 今のシーンの設定でフェードアウトを始める(始めたら true)
		void BeginFadeInWait(bool a_wasFadedOut);		// 切り替えた直後に、行き先のフェードインに備える
		void SubmitFade();								// 染めている色を画面全体に積む
		void SubmitScreenColor(float a_alpha, float a_zLayer);	// 染める色を画面全体に1枚積む

		void UpdateLoadingScreen(float a_dt, bool a_isLoading);	// ロード画面の出し入れ
		void UpdateBackdropBlur(float a_dt);					// 重ねたシーンの後ろのぼかし

		// 出だしと終わりをなだらかにする(0〜1 → 0〜1)
		static float EaseFade(float a_t);

		//------------------------------------------------------------------------------------------
		// シーン
		//------------------------------------------------------------------------------------------
		void ApplyReservedSceneChanges(Resource::ResourceManager& a_resourceManager);								// フレームの初めにシーンの切り替えを実行する
		void ReplaceScene(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid);	// シーンの切り替え
		bool PushScene(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid);		// シーンを重ねる(読み込めたら true)
		void PopScene(Resource::ResourceManager& a_resourceManager);									// 最前面のシーンを消去
		void ReplaceAllScenes(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid);	// 全部外して切り替え

		// シーンを作る : 初期化と設定ファイルの読み込みまで(組み立ては読み込みの中で行う)。
		// 見つからなければ nullptr
		std::unique_ptr<BaseScene> CreateScene(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid,
			std::string& a_outFileDir, std::string& a_outFileName);

		// シーンの読み込みを1歩進める : その間は RefWorld がこのシーンのワールドを返す
		void UpdateSceneLoad(Resource::ResourceManager& a_resourceManager, BaseScene& a_scene);

		//------------------------------------------------------------------------------------------
		// シーンの環境設定
		//------------------------------------------------------------------------------------------
		// 環境設定を使う一番上のシーンのものを GraphicsEngine へ流し込む(無ければ「無し」を流す)
		void ApplySceneAmbient();

		//------------------------------------------------------------------------------------------
		// 先読み一覧の計測
		//
		// 計測フラグを立てたシーンを開いてから閉じるまでの間、ResourceManager に
		// 読み込み要求を記録させ、閉じるときにそのシーンの先読み一覧を置き換える。
		// 上に重ねたシーン(ポーズ画面など)が読んだものも、その間に読まれたものとして数える
		//------------------------------------------------------------------------------------------
		void BeginRecordPreLoadAssets(Resource::ResourceManager& a_resourceManager, BaseScene& a_scene);	// 開くときに始める
		void EndRecordPreLoadAssets(Resource::ResourceManager& a_resourceManager);						// 閉じるときに止めて設定ファイルへ書く

	private:

		struct SceneChangeCmd
		{
			Core::GUID sceneGUID = Core::DEFAULT_GUID;
			ESceneChangeType changeType = ESceneChangeType::Replace;
		};

		// シーンスタック
		std::vector<std::unique_ptr<BaseScene>> m_upBaseSceneVec;

		// 今読み込みを進めているシーン。UpdateSceneLoad の間だけ入る(RefWorld がこれを優先する)
		BaseScene* m_pLoadingScene = nullptr;

		// ロード画面 : スタックの外で常駐させる(切り替えの掃除で消えないように)
		std::unique_ptr<BaseScene> m_upLoadingScreen = nullptr;

		// 読み込みが続いている時間。ロード画面を出すかの判定に使う
		float m_loadingTime = 0.0f;

		// これより長く読み込みが続いたらロード画面を出す。
		// すぐ済む切り替えでロード画面がちらつかないように(済めば染めた色からそのまま戻す)
		static constexpr float LOADING_SCREEN_DELAY_SEC = 0.3f;

		// ロード画面の出し入れにかける時間 / 一度出したら見せておく最短の時間
		static constexpr float LOADING_SCREEN_FADE_SEC = 0.35f;
		static constexpr float LOADING_SCREEN_MIN_SEC = 1.0f;

		// ロード画面の出し入れ
		ELoadingScreenState m_loadingScreenState = ELoadingScreenState::Hidden;
		float m_loadingScreenTimer = 0.0f;		// 出し入れの経過(秒)
		float m_loadingScreenShownTime = 0.0f;	// 出してからの時間(秒)
		float m_loadingScreenVeil = 1.0f;		// ロード画面の上へ重ねる染めた色の濃さ(0〜1)

		// 重ねたシーンの後ろのぼかし
		float m_backdropBlur = 0.0f;			// 今の掛かり具合(0〜1、なだらかにする前)
		float m_backdropBlurStrength = 0.0f;	// 掛ける強さ(最後にぼかしを使ったシーンの設定)
		float m_backdropBlurRadius = 0.0f;		// 半径(同上)
		float m_backdropBlurDuration = 0.0f;	// 掛け外しの時間(同上)

		// 先読み一覧を計測しているシーン(閉じるまで)。計測していなければ nullptr
		BaseScene* m_pPreLoadRecordingScene = nullptr;

		// シーン遷移のフェード
		EFadeState m_fadeState = EFadeState::None;
		float m_fadeTimer = 0.0f;
		float m_fadeDuration = 0.0f;
		float m_fadeAlpha = 0.0f;			// 染めている濃さ(0〜1)
		Math::Color m_fadeColor = Math::Color(0.0f, 0.0f, 0.0f, 1.0f);
		bool m_isFadeOutDone = false;		// フェードアウトが済んで、切り替えを待っている

		// 染めたまま行き先のフレームが落ち着くのを待つ(WaitReady)
		float m_fadeWaitTime = 0.0f;		// 行き先の読み込みが済んでからの時間
		int m_fadeStableFrameCount = 0;		// 続けて軽かったフレームの数
		static constexpr float FADE_STABLE_FRAME_SEC = 1.0f / 20.0f;	// これより短いフレームを軽いとみなす
		static constexpr int FADE_STABLE_FRAME_COUNT = 4;				// これだけ続いたら戻し始める
		static constexpr float FADE_STABLE_TIMEOUT_SEC = 2.5f;			// 落ち着かなくてもこれを過ぎたら戻す

		// 更新するのは一番上のシーンだけか(重ねたシーンの後ろを止めるための既定)
		bool m_isUpdateTopSceneOnly = true;

		// 使ったアセットの記録 : 記録するかどうかは生成時に決まり(起動設定)、以降変わらない
		bool m_isRecordUsedAssets = false;
		mutable std::mutex m_usedAssetMutex;							// 下の2つを守る
		std::vector<Core::GUID> m_usedAssetGUIDVec = {};				// 初めて要求された順
		std::unordered_set<Core::GUID> m_usedAssetGUIDSet = {};			// 重複を弾く

		// シーン切り替え命令スタック
		std::queue<SceneChangeCmd> m_sceneChangeCmd = {};

		// ワールドの実体を作る関数(上位層が差し込む)
		WorldFactory m_worldFactory = nullptr;

		// 環境設定の平行光が借りている LightManager の席。
		// シーンごとではなくここで1つだけ持つ : 重ねたシーンの数だけ太陽が並ばないように
		Handle<Graphics::DirectionalLight> m_ambientDLHandle = {};

		// 今フレーム頼まれた環境設定の演出。流し込んだら空に戻す(Draw)
		SceneAmbientMood m_ambientMood = {};

		// 持ち主(借り物)。描画・リソース・開発ツールへの通知はここから引く
		MainEngine* m_pEngine = nullptr;
	};
}
