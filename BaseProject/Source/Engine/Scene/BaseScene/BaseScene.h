#pragma once

#include "../SceneAmbient/SceneAmbient.h"
#include "../SceneAudio/SceneAudio.h"
#include "../ScenePhysics/ScenePhysics.h"
#include "../SceneConfig/SceneConfig.h"

namespace Engine
{
	class MainEngine;

	namespace ECS
	{
		class World;
	}

	namespace GameObject
	{
		class GameObjectManager;
	}

	namespace Resource
	{
		class ResourceManager;
	}
}
namespace Engine::Scene
{
	//======================================================================================
	// シーン用のワールドを1つ作る
	//
	// ・ECSワールドの生成と初期化
	// ・アプリ寿命のサービス(ResourceManager / InputManager など)の差し込み
	// ・コンポーネントとシステムの登録(SceneManager のワールド初期化コールバック)
	// までを済ませたものを返す。
	//
	// 通常のシーン(BaseScene::Enter)だけでなく、エディターのプレビュー用シーン
	// (EffectEditor)も同じ構成のワールドを要るのでここに切り出してある。
	// 「ゲームのシーンと同じ環境で確認できる」ことが目的なので、
	// 片方だけ登録漏れが起きない形にしておきたい。
	//
	// a_engine    : 型情報・サービス・ワールドの作り手(SceneManager)を借りる先
	// a_isPreview : プレビュー用なら true。構成は同じで、物理空間の確保量だけ小さくする
	//======================================================================================
	std::unique_ptr<Engine::ECS::World> CreateSceneWorld(MainEngine& a_engine, bool a_isPreview = false);

	class BaseScene
	{
	public:

		BaseScene();
		virtual ~BaseScene();

		//------------------------------------------------------------------------------------
		// 初期化
		//------------------------------------------------------------------------------------
		// シーン構築に必要なクラスを作成、オブジェクトなどは作成しない
		// a_engine : ワールドを作るのに借りる(呼ぶのは SceneManager)
		void Enter(MainEngine& a_engine);

		//------------------------------------------------------------------------------------
		// 読み込み
		//
		// シーンは1フレームでは組み立てない。毎フレーム UpdateLoad を呼んで少しずつ進め、
		// 済んだら Ready になる。Ready までは Update / Draw を呼ばないこと(呼んでも何もしない)。
		//
		//   PreLoad : 先読み一覧を、1フレームあたりの予算の範囲で読む
		//   Build   : シーンファイルを読んでエンティティ・オブジェクトを組み立てる
		//   Settle  : 初期化のフェーズ(BeginFrame)と物理への反映だけを回し、
		//             全エンティティが Start を通り終えるのを待つ
		//   Ready   : 準備完了。通常の更新へ
		//------------------------------------------------------------------------------------

		// 読み込みを始める : シーンファイルの場所を覚えて PreLoad から始める。
		// a_isPreLoad が false なら先読みを飛ばす(先読み一覧を計測するとき)
		void BeginLoad(const std::string& a_fileDir, const std::string& a_fileName, bool a_isPreLoad);

		// 読み込みを1歩進める : 毎フレーム呼ぶ
		void UpdateLoad(Resource::ResourceManager& a_resourceManager);
		
		//------------------------------------------------------------------------------------
		// 解放
		//------------------------------------------------------------------------------------
		void Exit();				// エンティティを消すところまで,アセットの返却はコンポーネントのフック関数が処理

		//------------------------------------------------------------------------------------
		// 更新
		//------------------------------------------------------------------------------------
		void Update(float a_dt);	// ワールド、オブジェクトの更新
		void Draw();				// 更新結果の描画処理

		//------------------------------------------------------------------------------------
		// 保存
		//------------------------------------------------------------------------------------
		void  Archive(Persistence::Archive& a_ar);			// アーカイブ処理

		//------------------------------------------------------------------------------------
		// アクセサ
		//------------------------------------------------------------------------------------
		// 現在のワールドを参照
		Engine::ECS::World* RefWorld() { return m_upWorld.get(); }

		// ECS外オブジェクトのマネージャーを取得
		GameObject::GameObjectManager* RefGameObjectManager() { return m_upGameObjectManager.get(); }

		// シーンの環境設定(環境光・平行光・影・フォグ・ボリュメトリックフォグ・空)
		SceneAmbient& RefAmbient() { return m_ambient; }
		const SceneAmbient& GetAmbient() const { return m_ambient; }

		// シーンの音量(マスター + グループごと) : シーンと一緒に保存される。
		// このシーンで鳴らした音にはこれとプレイヤーの音量が掛かる
		SceneAudio& RefAudio() { return m_audio; }
		const SceneAudio& GetAudio() const { return m_audio; }

		// シーンの物理の設定(重力) : シーンと一緒に保存され、ワールドの PhysicsWorld へ流し込まれる
		ScenePhysics& RefPhysics() { return m_physics; }
		const ScenePhysics& GetPhysics() const { return m_physics; }

		// シーンの設定(先読み一覧・計測フラグ) : シーンファイルとは別のファイルに保存される
		SceneConfig& RefConfig() { return m_config; }
		const SceneConfig& GetConfig() const { return m_config; }

		// シーンのGUID
		void SetGUID(const Core::GUID& a_guid) { m_guid = a_guid; }
		const Core::GUID& GetGUID() const { return m_guid; }

		// シーンファイルの名前(BeginLoad で渡されたもの)
		const std::string& GetFileName() const { return m_fileName; }

		// 読み込みが済んで動いているか
		bool IsReady() const { return (m_state == EState::Ready); }

		// 読み込みの進み具合(0〜1)。Ready なら 1
		float GetLoadProgress() const;

	private:

		// 現在のシーンのステート
		enum class EState
		{
			PreLoad,		// 事前ロード
			Build,			// シーン構築
			Settle,			// エンティティ待ち
			Ready,			// 準備完了
		};

		// 読み込みの各段 : UpdateLoad から呼ぶ
		bool UpdatePreLoad(Resource::ResourceManager& a_resourceManager, double a_budgetMs);	// 先読み一覧を要求し切ったら true
		void BuildFromFile();																	// シーンファイルから組み立てる
		bool UpdateSettle(Resource::ResourceManager& a_resourceManager);						// 全エンティティが動き出したら true
		void UpdatePreLoadWait(Resource::ResourceManager& a_resourceManager);					// 先読みのうち届いたものを数える

	private:

		//-------------------------------------------------------------------
		// シーンの情報
		//-------------------------------------------------------------------
		EState m_state = EState::PreLoad;		// 現在の状態
		
		// データ
		Core::GUID m_guid;						// 自身のデータの所在
		SceneAmbient m_ambient = {};			// シーンの環境設定 : シーンと一緒に保存される
		SceneAudio m_audio;						// シーンの音量 : シーンと一緒に保存される
		ScenePhysics m_physics;					// シーンの物理の設定(重力) : シーンと一緒に保存される
		SceneConfig m_config = {};				// シーンの設定 : 開始時に読み込んでおきたいアセット(先読み一覧)と、その計測フラグ

		// ゲームの本体 : ワールド空間上にあるものすべてはこちらで管理
		std::unique_ptr<Engine::ECS::World> m_upWorld = nullptr;

		// ECS側で扱いにくいものなどの管理
		std::unique_ptr<GameObject::GameObjectManager> m_upGameObjectManager = nullptr;

		//-------------------------------------------------------------------
		// 読み込みの状態(保存しない)
		//-------------------------------------------------------------------
		// シーンファイルの場所 : Build で読む
		std::string m_fileDir = {};
		std::string m_fileName = {};

		// 先読み : 次に要求する添え字と、要求したがまだ届いていないもの
		size_t m_preLoadIndex = 0;
		size_t m_preLoadDoneCount = 0;
		std::vector<Core::GUID> m_preLoadWaitVec = {};

		// 落ち着き待ち : 残り(動き出していないエンティティ + 読み込み中のアセット)の最大値と、
		// それに対する進み具合
		uint32_t m_settleMaxRemaining = 0;
		float m_settleProgress = 0.0f;
		bool m_isSettleStarted = false;

		// 読み込みを始めた時刻 / 落ち着き待ちを始めた時刻(ログと打ち切りに使う)
		std::chrono::steady_clock::time_point m_loadStartTime = {};
		std::chrono::steady_clock::time_point m_settleStartTime = {};
	};
}