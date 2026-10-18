#pragma once
#include "../../../../Engine/GameObject/BaseObject/BaseObject.h"

#include "../../../Game/GlobalGameContext.h"
#include "../../../Game/Core/InputSettings.h"

#include "../../SequenceBgm.h"

namespace Engine::Scene
{
	class SceneManager;
}

namespace App::Object
{
	/// <summary>
	/// 1体ぶんの出現設定
	/// </summary>
	struct SpawnSettings
	{
		// 出現させるエンティティ
		Core::GUID								spawnEntityGUID = {};		// 出すプレハブ(保存する)
		Engine::Handle<Engine::Resource::Prefab>	spawnPrefabHandle = {};	// 解決済みハンドル(ランタイム)

		// 初期情報
		Math::Vector3 pos = {};						// 位置(ウェーブ位置からの相対座標)
		Math::Vector3 dir = { 0.0f, 0.0f, 1.0f };	// 方向(左手系 +Z 前方。水平成分だけ使う)
	};

	/// <summary>
	/// エンティティを出現させる仕様
	/// </summary>
	struct Wave
	{
		// 出現させるエンティティ
		std::vector<SpawnSettings> spawnEntities = {};

		// 呼び出すシーンのオブジェクト(SequenceActor。保存する)。
		// プレハブにできない大物(群れのボスなど)はシーンに直接置き、ここに載せる。
		// 載せたものはこのウェーブが来るまで出てこない。出る位置は相手が持つ
		std::vector<Core::GUID> actorGUIDs = {};

		// ウェーブの基準位置(ワールド)。
		// 各 SpawnSettings.pos はここからの相対座標なので、ここを動かせば
		// そのウェーブの出現位置がまとめて動く。
		Math::Vector3 pos = {};

		// ウェーブ条件(保存する)
		float timing = 0.0f;			// 出現タイミング(秒)
										//   isAnnihilation = false : シーン開始からの経過時間
										//   isAnnihilation = true  : 前のウェーブが全滅してからの経過時間
		bool isAnnihilation = false;	// 前のウェーブが全滅してから出現させるかどうか

		// 現在の状態(保存しない。シーン開始時は既定値から始まる)
		bool  isSpawned   = false;		// 出現処理を済ませたか
		int   spawnCount  = 0;			// 出した数(生成コマンドを積めた数 + 呼び出せたオブジェクトの数)
		int   aliveCount  = 0;			// 生存数。毎フレーム SpawnerComponent と呼び出したオブジェクトを数え直す
		bool  isConfirmed = false;		// 実体化を一度でも確認したか(遅延生成の1フレームずれ対策)
		bool  isCleared   = false;		// 全滅したか
		float spawnTime   = 0.0f;		// 出現させた時刻(シーン経過秒)
		float clearedTime = 0.0f;		// 全滅した時刻(シーン経過秒)
	};

	/// <summary>
	/// ボスへ送る「戦闘開始命令」1件ぶん
	/// </summary>
	/// <remarks>
	/// ボス(BossParamsComponent 保持者)は出しただけでは動かず、この命令が届いてから
	/// 戦闘を始める。「出現 → 演出 → 戦闘開始」のように、出す時刻と戦い始める時刻を
	/// 分けて置けるようにするため。
	///
	/// 命令はエンティティIDではなくコンポーネントへ直接立てる。プレハブの実体化は
	/// 遅延生成なので、出した側はIDを持ち歩けない(ウェーブの全滅判定と同じ事情)。
	/// </remarks>
	struct BossOrder
	{
		// ---- 送る条件(保存する) ----
		float timing = 0.0f;			// 送るタイミング(秒)
										//   afterWaveIndex < 0  : シーン開始からの経過時間
										//   afterWaveIndex >= 0 : そのウェーブが全滅してからの経過時間
		int   afterWaveIndex  = -1;		// 待つウェーブの番号。-1 ならシーン開始から数える

		// 送る相手。-1 なら「シーンに居る全てのボス」へ送る。
		// 0 以上なら、自分がそのウェーブで出したボスだけに絞る(SpawnerComponent の印で判別)。
		int   targetWaveIndex = -1;

		// ---- 状態(保存しない) ----
		bool  isSent    = false;		// 1体でも送れたか
		int   sentCount = 0;			// 送れた数
		float sentTime  = 0.0f;			// 送った時刻(シーン経過秒)
	};

	/// <summary>
	/// シーン全体の流れを握る : シーンに一つのみ存在
	///
	/// 敵の出現(ウェーブ)やチュートリアルの操作説明など、
	/// 「時間や状況で進むイベント」をここでまとめて管理する。
	/// 今はウェーブと、ボスへの戦闘開始命令(BossOrder)を持つ。
	///
	/// ・出現させたエンティティには SpawnerComponent(自分のGUID + ウェーブ番号)が付く。
	///   全滅判定は毎フレームその印を数えるだけなので、エンティティIDを持ち歩かなくて済む
	///   (プレハブの実体化は遅延生成なので、出した直後はIDが取れない)。
	/// ・生成は GameObjectManager::Update から呼ばれる。ECSのシステム反復の外なので
	///   World の遅延生成コマンドがそのまま次の BeginFrame で消化される。
	///
	/// ・決着(リザルトへ移る条件)もここが握る。
	///     負け : プレイヤーが倒された
	///     勝ち : プレイヤーが生きていて、全ウェーブを出し切って全滅させた
	///   ボスも1つのウェーブとして出すので、ボスを倒せばそのウェーブが片付き、
	///   それが最後のウェーブならそのまま勝ちになる(ボス専用の判定は要らない)。
	///   シーンに直接置くボス(群れのボスなど)は、ウェーブの actorGUIDs に載せれば同じ扱いになる
	///   (SequenceActor。読み込み直後に「呼ばれるまで出るな」と伝えておく)。
	///   決着したら記録(スコア・タイム・結末)を GlobalGameContext へ移してから
	///   リザルトシーンを読み込む。ワールドのリソースはシーンを切り替えると
	///   作り直されるので、持っていきたいものはグローバル側へ置く。
	/// </summary>
	class SceneSequence : public Engine::GameObject::BaseObject
	{
	public:

		// 読み込み直後 : ウェーブに載せたシーンのオブジェクトへ、呼ばれるまで出ないよう伝える(Awake より前)
		void PostDeserialize(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : 経過時間を進め、条件を満たしたウェーブを出す
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 解放処理 : 借りているBGMを返す
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "SceneSequence"; }

		// インスペクター : ウェーブの編集と進行状況の表示
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

		// シーンビューのギズモ : インスペクタで選んだ位置(ウェーブ or 出現位置)を動かす
		bool DrawGizmo(
			const Engine::GameObject::ObjectGizmoContext& a_ctx,
			Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 出現位置の印をデバッグ描画する
		void DrawSpawnMarker(Engine::GameObject::ObjectContext& a_context) const;

		// SpawnerComponent を数えて各ウェーブの生存数を更新する
		void UpdateAliveCount(Engine::GameObject::ObjectContext& a_context);

		// 全滅の判定を進める
		void UpdateClearState();

		// 出現条件を満たしているか
		bool CanSpawn(size_t a_index) const;

		// ウェーブを出現させる
		void Spawn(Engine::GameObject::ObjectContext& a_context, size_t a_index);

		// 条件を満たした戦闘開始命令をボスへ送る
		void SendBossOrders(Engine::GameObject::ObjectContext& a_context);

		// 命令を送ってよい時刻になっているか
		bool CanSendOrder(size_t a_index) const;

		// 進行状況を初期化して最初からやり直す
		void ResetProgress();

		// デバッグキー(Development まで)の予約を拾う :
		// 出ている敵を倒す予約をして、次のウェーブ(または最後のウェーブ)をすぐ出す
		void ApplyReservedDebugSkip(Engine::GameObject::ObjectContext& a_context);

		//-------------------------------------------------------------------
		// 決着まわり
		//-------------------------------------------------------------------

		// 勝ち負けを見て、決まっていれば結末を立てる
		void UpdateResultState(Engine::GameObject::ObjectContext& a_context);

		// プレイヤーが倒されたか(消えてしまった場合も倒された扱い)
		bool IsPlayerDead(Engine::GameObject::ObjectContext& a_context);

		// 全ウェーブを出し切って全滅させたか
		bool IsAllWaveCleared() const;

		// 全滅させたウェーブの数
		int GetClearedWaveCount() const;

		// 記録をグローバルへ移してリザルトシーンを読み込む
		// a_pSceneManager : 切り替えの予約先(無ければ記録だけ移す)
		void ReserveResultScene(Game::GlobalGameContext& a_gameData, Engine::Scene::SceneManager* a_pSceneManager);

		//-------------------------------------------------------------------
		// ポーズ
		//-------------------------------------------------------------------

		// ポーズ入力を見て、押されていればポーズ画面を重ねる
		void UpdatePause(Engine::GameObject::ObjectContext& a_context);

		// ギズモの対象を外す(要素を消して添え字がずれたとき用)
		void ClearGizmoTarget();

	private:

		// 敵の出現をつかさどる
		std::vector<Wave> m_waves = {};

		// ボスへの戦闘開始命令
		std::vector<BossOrder> m_bossOrders = {};

		// シーンの経過時間
		float m_time = 0.0f;

		//=======================================================================
		// 決着(リザルトへの遷移)
		//=======================================================================
		// ---- 設定(保存する) ----
		Core::GUID m_resultSceneGUID = {};	// 遷移先。未設定なら遷移しない

		// 決着してから実際に移るまでの待ち。倒れる演出や爆発を見せるための間
		float m_clearDelay = 3.0f;	// 勝ったとき
		float m_deadDelay  = 3.0f;	// 負けたとき

		// シーンが始まったときにグローバルの記録を消すか。
		// ゲームシーンの入り口なので既定で消す(前回のスコアが残らないように)
		bool m_isResetOnStart = true;

		//=======================================================================
		// ポーズ
		//=======================================================================
		// 重ねるポーズ画面のシーン。未設定ならポーズしない。
		//
		// 切り替え(Replace)ではなく重ねる(Push)ので、このシーンは消えずに残る。
		// 更新されるのは一番上のシーンだけなので、重ねている間ここは止まり、
		// 描画だけが続く(SceneManager::SetUpdateTopSceneOnly)。
		// 閉じるのは重ねた側(PauseSequence)の仕事。
		Core::GUID m_pauseSceneGUID = {};

		// ポーズに使う入力アクション名(InputManager へ登録した名前)
		Game::EGameAction m_pauseAction = Game::EGameAction::Pose;

		// ---- 状態(保存しない) ----
		// ポーズ画面を重ねるよう頼んだか。重ねている間このシーンは更新されないので、
		// 戻ってきた最初のフレームで下ろす
		bool m_isPauseRequested = false;

		// ---- 状態(保存しない) ----
		App::Game::EGameResult m_result = App::Game::EGameResult::None;	// 決着の内容
		float m_resultTimer = 0.0f;			// 移るまでの残り時間(秒)
		bool  m_isSceneRequested = false;	// 二重に遷移要求を出さないための印
		bool  m_isPlayerFound = false;		// プレイヤーを一度でも見つけたか
											// (湧く前の1フレームを死亡と間違えないため)
		bool  m_isStarted = false;			// 最初の更新を通ったか(記録を消す判定に使う)

		//-------------------------------------------------------------------
		// BGM
		//-------------------------------------------------------------------
		SequenceBgm m_bgm = {};

		//=======================================================================
		// エディター用(保存しない)
		//=======================================================================
		// ギズモで動かす対象。ImGuizmo は一度に1つしか操作できないので、
		// インスペクタで選んだ1点だけを出す。
		//   m_gizmoWaveIndex  < 0 … ギズモを出さない
		//   m_gizmoSpawnIndex < 0 … ウェーブの基準位置を動かす(配下がまとめて動く)
		//   それ以外              … その出現位置(ウェーブからの相対)を動かす
		int m_gizmoWaveIndex  = -1;
		int m_gizmoSpawnIndex = -1;
	};
}
