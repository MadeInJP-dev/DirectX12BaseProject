#pragma once

#include "Engine/Config/BuildConfig.h"

namespace Engine::DevTool
{
	class IDevTool;
}

namespace Engine
{
	// 前方宣言
	namespace Window
	{
		class NativeWindow;
	}
	namespace Time
	{
		class TimeManager;
	}
	namespace Graphics
	{
		class GraphicsEngine;
		class RenderContext;
	}

	namespace Thread
	{
		class JobSystem;
	}

	namespace Physics
	{
		class PhysicsEngine;
	}

	namespace Resource
	{
		class ResourceManager;
	}

	namespace ECS
	{
		struct EngineServices;
		class ComponentMetaRegistry;
	}

	namespace GameObject
	{
		class ObjectMetaRegistry;
	}

	namespace Config
	{
		class ConfigRegistry;
	}

	namespace Input
	{
		class InputManager;
	}

	namespace Audio
	{
		class AudioManager;
	}

	namespace Scene
	{
		class SceneManager;
	}

	//===============================================================================================
	// 起動設定 : エンジンの起動設定
	//===============================================================================================
	struct EngineConfig
	{
		
	};


	//===============================================================================================
	// エンジンクラス
	//===============================================================================================
	class MainEngine
	{
	public:

		// 初期化・解放
		void Init();
		void Release();

		// フレーム関係
		bool BeginFrame();
		void EndFrame();

		// 描画関係
		void BeginDraw();
		void EndDraw();

		// デルタタイム取得
		UINT GetFPS() const;
		float GetDeltaTime() const;			// 時間の倍率を掛けた後(ゲームはこちら)
		float GetUnscaledDeltaTime() const;	// 倍率を掛ける前(エディターなど、止めても動かすもの)

		// 時間の倍率(1 で等倍、0 で止める)とコマ送り。反映は次のフレームの頭から
		void SetTimeScale(float a_scale);
		float GetTimeScale() const;
		void ReserveFrameStep();

		// モード切替
		void ChangeMode(EAppMode a_mode);
		EAppMode GetMode() const { return m_appMode; }

		/// <summary>
		/// アプリの終了を予約する : 次のフレームの頭(BeginFrame)でメインループを抜ける
		/// </summary>
		/// <remarks>
		/// ゲーム側の「ゲームを終了」から呼ぶ。その場で抜けずにフレームの区切りまで待つので、
		/// 呼んだフレームの更新・描画は最後まで走る
		/// </remarks>
		void ReserveQuit() { m_isQuitReserved = true; }

		// 開発ツール(エディター)が差し込まれているか
		bool HasDevTool() const { return m_pDevTool != nullptr; }

		// グラフィックス関係
		void ExecuteDrawCmd();

		// ウィンドウ取得
		const Window::NativeWindow* GetNativeWindow() const;
		Window::NativeWindow* RefNativeWindow();

		// 開発ツール(エディター)の差し込み : 最上位(main.cpp)が Init より前に渡す。借り物
		void SetDevTool(DevTool::IDevTool* a_pDevTool) { m_pDevTool = a_pDevTool; }

		// 差し込まれていなければ nullptr(ツール無しで動く)
		DevTool::IDevTool* RefDevTool() { return m_pDevTool; }

		// グラフィックスエンジンアクセス
		Graphics::GraphicsEngine* RefGraphicsEngine();

		// レンダーコンテキストアクセス
		const Graphics::RenderContext* GetRenderContext() const;
		Graphics::RenderContext* RefRenderContext();

		// ジョブシステム
		Thread::JobSystem* RefJobSystem();

		// リソースマネージャー(アセットデータベースもこの中)
		Resource::ResourceManager* RefResourceManager() { return m_upResourceManager.get(); }

		// エンジン設定(EngineData.ojoptn)の読み書きと設定画面への一覧。値は各システムが持つ
		Config::ConfigRegistry* RefConfigRegistry() { return m_upConfigRegistry.get(); }

		/// <summary>
		/// プレイヤーが選んだ FPS の上限(nullopt で作り手の値 TimeConfig::fpsRate のまま)
		/// </summary>
		/// <remarks>
		/// 保存はアプリ側(ユーザーデータ)。エンジン設定は書き換えないので、
		/// エディターで EngineData を保存してもプレイヤーの値は混ざらない
		/// </remarks>
		void SetUserFPSRate(std::optional<EFPSRate> a_opFPSRate) { m_opUserFPSRate = a_opFPSRate; }

		// 実際に使う FPS の上限 : プレイヤーの値があればそちら
		EFPSRate GetFPSRate() const;

		// 入力
		Input::InputManager* RefInputManager() { return m_upInputManager.get(); }

		// オーディオ
		Audio::AudioManager* RefAudioManager() { return m_upAudioManager.get(); }

		// シーンの積み替え。解放(Release)はアプリ側が、ゲームより先に呼ぶ
		Scene::SceneManager* RefSceneManager() { return m_upSceneManager.get(); }

		//----------------------------------------------------------------------------
		// アプリ寿命のサービス一式(正本)
		//
		// ワールドはこれを写して持ち、エディターはこのポインタを持つ。
		// 中身が揃うのは Init() の途中(グラフィックスエンジンの初期化の後)から
		//----------------------------------------------------------------------------
		const ECS::EngineServices& GetEngineServices() const { return *m_upEngineServices; }
		ECS::EngineServices* RefEngineServices() { return m_upEngineServices.get(); }

		//----------------------------------------------------------------------------
		// コンポーネントの型情報(プロセスに1つ)
		//
		// どのワールドもこれを借りるので、同じ型はどのワールドでも同じタイプIDになる。
		// 渡すのはワールドを作る所(CreateSceneWorld)だけ
		//----------------------------------------------------------------------------
		ECS::ComponentMetaRegistry* RefComponentRegistry() { return m_upComponentRegistry.get(); }

		//----------------------------------------------------------------------------
		// 起動設定(今効いている値)
		//
		// 次の起動から効く項目は起動したときの値のまま、すぐ効く項目は
		// オプション画面で直した値が毎フレーム写される。使う側は時期を気にせずここを見る。
		// 設定画面で直した値そのものは GetEditBuildConfig()(保存されるのはこちら)
		//----------------------------------------------------------------------------
		const Config::BuildConfig& GetBuildConfig() const { return m_buildConfig; }
		const Config::BuildConfig& GetEditBuildConfig() const { return m_editBuildConfig; }

		// ============================================================================
		// 遅延開放処理
		// ============================================================================
		// 遅延開放したい処理を登録
		void ReserveRelease(std::function<void()> a_releaseFunc);

	private:

		// アセットマネージャーの初期化
		void InitializeAssetDatabase();

		// 起動設定をログへ出す(どの項目が効いているか)
		void LogBuildConfig() const;

		// アプリ寿命のサービス一式を組む : 載せる実体が揃ってから呼ぶ
		void BuildEngineServices();

	private:

		// クラス
		//
		// リソースマネージャーは先頭に置く : メンバは宣言の逆順に壊れるので、これが最後になる。
		// 後ろのメンバ(オーディオのサウンドインスタンス・シーンなど)が持つ ResourceRef は、破棄のときに参照を返しに来る
		std::unique_ptr<Resource::ResourceManager> m_upResourceManager = nullptr;		// リソース(とアセットデータベース)の持ち主

		// エンジン設定の読み書き : 何よりも先に読む(起動設定やウィンドウの大きさがここで決まる)。
		// 値は持ち主が持ち、ここは登録された設定を読み書きするだけ
		std::unique_ptr<Config::ConfigRegistry> m_upConfigRegistry = nullptr;

		// 入力 : ウィンドウのフォーカス通知を受けるので、ウィンドウより前に作る
		std::unique_ptr<Input::InputManager> m_upInputManager = nullptr;

		// オーディオ : 再生中のインスタンスが ResourceRef<Sound> を持っているので、
		// リソースマネージャーより後ろに置き、先に壊れるようにする
		std::unique_ptr<Audio::AudioManager> m_upAudioManager = nullptr;

		// コンポーネントの型情報 : ワールドの解放(解放フックの呼び出し)で引くので、
		// ワールドを持つどのメンバよりも後に壊れるよう、リソースマネージャーの次に置く
		std::unique_ptr<ECS::ComponentMetaRegistry> m_upComponentRegistry = nullptr;

		// ECS外オブジェクトのクラス情報 : コンポーネントの型情報と同じく、シーンより後に壊れる位置に置く
		std::unique_ptr<GameObject::ObjectMetaRegistry> m_upObjectRegistry = nullptr;

		// シーン(ワールドを持つ)。上の型情報より後ろに置き、先に壊れるようにする
		std::unique_ptr<Scene::SceneManager> m_upSceneManager = nullptr;
		std::unique_ptr<Window::NativeWindow> m_upWindow = nullptr;						// ウィンドウクラス
		std::unique_ptr<Time::TimeManager> m_upTimeManager = nullptr;					// 時間管理クラス
		std::unique_ptr<Graphics::GraphicsEngine> m_upGraphicsEngine = nullptr;			// 描画周りの管理クラス(パーティクル・レイトレもこの中)
		std::unique_ptr<Thread::JobSystem> m_upJobSystem = nullptr;						// ジョブシステム
		std::unique_ptr<Physics::PhysicsEngine> m_upPhysicsEngine = nullptr;			// Jolt 全体(シーンごとの空間は PhysicsWorld)
		std::unique_ptr<ECS::EngineServices> m_upEngineServices = nullptr;				// アプリ寿命のサービス一式(正本)

		// エンジン設定
		DevTool::IDevTool* m_pDevTool = nullptr;								// 開発ツール(借り物。無くてもよい)
		EAppMode m_appMode = EAppMode::Editor;								// アプリケーションのモード

		// プレイヤーが選んだ FPS の上限(未選択なら作り手の値)
		std::optional<EFPSRate> m_opUserFPSRate = std::nullopt;

		// 終了の予約(ReserveQuit)。次のフレームの頭でメインループを抜ける
		bool m_isQuitReserved = false;
		Config::BuildConfig m_buildConfig = {};			// 起動設定(今効いている値)
		Config::BuildConfig m_editBuildConfig = {};		// 起動設定(設定画面で直した値。保存されるのはこちら)

		// フレーム分のごみ箱を用意する。
		// 積むのはメインスレッドとは限らない(ワーカーでビルド中のリソースが壊れたときなど)ので、
		// 触るときは必ずロックを取る
		std::vector<std::function<void()>> m_releaseQueues[CPU_FRAME_COUNT];
		std::mutex m_releaseQueueMutex;

	// シングルトン
	private:

		// コンストラクタ・デストラクタ
		MainEngine();
		~MainEngine();
		NON_COPYABLE_NON_MOVABLE(MainEngine);

	public:

		static MainEngine& Instance()
		{
			static MainEngine _instance = {};
			return _instance;
		}

	};
}
