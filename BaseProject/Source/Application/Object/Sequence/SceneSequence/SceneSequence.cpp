#include "SceneSequence.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Application/ECS/World/APPWorld.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/EditorField/EditorField.h"
#include "Engine/Common/Color.h"

#include "Application/Components/Core/SpawnerComponent.h"
#include "Application/Components/Boss/BossParamsComponent.h"
#include "Application/Components/Combat/HealthComponent.h"
#include "Application/Utility/PrefabSpawnHelper.h"
#include "Application/InstanceResource/WaveAnnounceResource.h"
#include "Application/Components/Input/PlayerControllTag.h"
#include "Application/InstanceResource/GameDataResource.h"
#include "Application/InstanceResource/DebugCommandResource.h"
#include "Application/Game/DebugKeys/DebugCommands.h"
#include "Application/Object/ObjectPicker.h"
#include "SequenceActor.h"

#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/MainEngine.h"

//==========================================================================================
// SceneSequence
//
// ウェーブの進行:
//   1) 生存数を数える      … 出したエンティティに付けた SpawnerComponent を毎フレーム集計
//   2) 全滅判定を進める    … 生存数が 0 になった瞬間の時刻を覚えておく
//   3) 条件を満たしたウェーブを出す
//   4) 条件を満たした戦闘開始命令をボスへ送る
//
// ボスはウェーブで「出す」だけでは動かない。BossOrder が届いてから戦い始める。
// 出現と戦闘開始を分けておくと、湧いた直後に演出を挟んでから戦わせる、といった
// 組み立てがシーケンス側の設定だけでできる。
//
// 「出した直後は生存数 0」問題に注意。プレハブの実体化は遅延生成(次の BeginFrame)なので、
// 出した次の瞬間に数えると 0 になり、全滅扱いで次のウェーブが即座に走ってしまう。
// そのため一度でも生存を確認する(isConfirmed)まで全滅とみなさない。
// 実体化に失敗した(プレハブが空など)場合に詰まらないよう、確認には時間切れを設けてある。
//
// シーンに直接置いたオブジェクト(SequenceActor)もウェーブに載せられる。
// 読み込み直後に「呼ばれるまで出るな」と伝え、ウェーブが来たら呼び出し、
// 生存数には相手の IsAlive を足す。プレハブで出したものと同じ流れで全滅判定に乗る。
//==========================================================================================
namespace App::Object
{
	namespace
	{
		// 実体化を待つ時間(秒)。これを過ぎても生存が確認できなければ全滅扱いにして次へ進む
		constexpr float SPAWN_CONFIRM_TIMEOUT = 2.0f;

		// 出現位置マーカーの大きさ(m)
		constexpr float MARKER_SIZE = 1.0f;

		// 出し終えたウェーブのマーカー色(これから出るものと見分けるため落とした色)
		constexpr Math::Color DIMMED_COLOR = { 0.35f, 0.35f, 0.35f, 1.0f };

		// dir(水平方向)から左手系 +Z 前方のクォータニオンを作る。
		// 長さが無い/真上を向いている場合は回さない(プレハブの向きのまま)。
		Math::Quaternion MakeYawQuat(const Math::Vector3& a_dir, bool& a_outHasRotation)
		{
			Math::Vector3 _flat = { a_dir.x, 0.0f, a_dir.z };
			if (_flat.LengthSquared() <= 1e-6f)
			{
				a_outHasRotation = false;
				return Math::Quaternion::Identity();
			}

			a_outHasRotation = true;

			// 左手系 +Z 前方なので Yaw = atan2(x, z)
			const float _yaw = std::atan2(_flat.x, _flat.z);
			return Math::Quaternion::CreateFromYawPitchRoll(_yaw, 0.0f, 0.0f);
		}
	}

	//======================================================================================
	// 読み込み直後 : ウェーブに載せたオブジェクトを待たせる
	//--------------------------------------------------------------------------------------
	// 初期化はフェーズごとに全員を回すので、ここ(PostDeserialize)は誰の Awake よりも前。
	// 相手は Awake で出るのをやめ、このウェーブの Appear を待つ。
	// 読み込みは全員を並べてから初期化を回すので、FindByGUID でもう引ける
	//======================================================================================
	void SceneSequence::PostDeserialize(Engine::GameObject::ObjectContext& a_context)
	{
		for (const Wave& _wave : m_waves)
		{
			for (const Core::GUID& _guid : _wave.actorGUIDs)
			{
				auto* _pActor = Picker::Find<SequenceActor>(a_context.pObjectManager, _guid);
				if (!_pActor)
				{
					// 消したものを指したままなら知らせる(未設定は黙って飛ばす)
					Picker::CheckFound<SequenceActor>(a_context.pObjectManager, _guid, "SceneSequence", "Wave Actor");
					continue;
				}

				_pActor->HoldAppear();
			}
		}
	}

	void SceneSequence::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// BGM は先に進める。この後の早期リターンで止めてしまうと、
		// ポーズを開いた瞬間にフェードが固まる
		m_bgm.Update(a_context);

		if (!a_context.pWorld) return;

		// ポーズ画面を重ねる/戻ってきたことを拾う。
		// 重ねている間このシーンは更新されないので、下の処理はそのまま止まる
		UpdatePause(a_context);
		if (m_isPauseRequested) return;

		m_time += a_context.dt;

		// 生存数 → 全滅判定 → 出現、の順に進める
		UpdateAliveCount(a_context);
		UpdateClearState();

		// デバッグキーでウェーブを飛ばす(条件を待たずに出すので、いつもの出現より前に置く)
		ApplyReservedDebugSkip(a_context);

		for (size_t _i = 0; _i < m_waves.size(); ++_i)
		{
			if (m_waves[_i].isSpawned) continue;
			if (!CanSpawn(_i)) continue;

			Spawn(a_context, _i);
		}

		SendBossOrders(a_context);

		// 勝ち負けの判定と、決まっていればリザルトへ
		UpdateResultState(a_context);

		DrawSpawnMarker(a_context);
	}

	//======================================================================================
	// 決着の判定
	//--------------------------------------------------------------------------------------
	// 負けを先に見る。プレイヤーが倒れた時に最後の敵も道連れになっていた場合、
	// 勝ち負けが同じフレームに揃うので、そこは負けを優先する。
	//
	// 決まった後は待ち時間を進めるだけ。倒れる演出や爆発を見せてから移りたいので、
	// 決まった瞬間には切り替えない。
	//======================================================================================
	void SceneSequence::UpdateResultState(Engine::GameObject::ObjectContext& a_context)
	{
		// 記録の置き場(シーンをまたぐもの)。持ち主は GameManager で、ワールドに入口が置いてある
		auto* _pGameData = InstanceResource::GameDataResource::Find(a_context.pWorld);
		if (!_pGameData) return;
		auto& _gameData = *_pGameData;

		//----------------------------------------------------------------------
		// シーンの入り口で記録を消す
		//
		// リザルトからやり直したときに前回のスコアが残らないようにする。
		// 最初の更新でだけ行う(Update の頭ではなくここに置いているのは、
		// 決着まわりの持ち物をひとまとめにしておくため)
		//----------------------------------------------------------------------
		if (!m_isStarted)
		{
			m_isStarted = true;

			if (m_isResetOnStart)
			{
				_gameData.ResetRun();
			}
		}

		//----------------------------------------------------------------------
		// タイムは毎フレーム移しておく
		//
		// 決着した時だけ書くと、ゲーム中のタイム表示が 0 のままになる。
		// 決まった後は止める。リザルトに出したいのは決着した時刻で、
		// その後も進み続けると遷移待ちのぶんだけ伸びてしまう
		//----------------------------------------------------------------------
		if (m_result == App::Game::EGameResult::None)
		{
			_gameData.time = m_time;
		}

		// 決着済み : 待ってから移る
		if (m_result != App::Game::EGameResult::None)
		{
			if (m_isSceneRequested) return;

			// 勝ったときのスロー。スローの間は待ちを実時間で数える(倍率で待ちが伸びないように)
			const float _realDt = (a_context.pServices && a_context.pServices->pMainEngine)
				? a_context.pServices->pMainEngine->GetUnscaledDeltaTime()
				: a_context.dt;
			UpdateClearSlow(a_context, _realDt);

			m_resultTimer = (std::max)(0.0f, m_resultTimer - (IsClearSlow() ? _realDt : a_context.dt));
			if (m_resultTimer > 0.0f) return;

			ReserveResultScene(_gameData, a_context.pServices ? a_context.pServices->pSceneManager : nullptr);
			return;
		}

		// ---- 負け ----
		if (IsPlayerDead(a_context))
		{
			m_result = App::Game::EGameResult::GameOver;
			m_resultTimer = m_deadDelay;
			return;
		}

		// ---- 勝ち ----
		if (IsAllWaveCleared())
		{
			m_result = App::Game::EGameResult::Clear;
			m_resultTimer = m_clearDelay;
		}
	}

	//======================================================================================
	// プレイヤーが倒されたか
	//--------------------------------------------------------------------------------------
	// 「死亡状態になった」と「居なくなった」の両方を倒された扱いにする。
	// 死亡してから消えるまでには猶予があるが、消えた後は探しても見つからないため。
	//
	// 湧く前の1フレームを死亡と間違えないよう、一度でも見つけるまでは判定しない
	// (プレハブの実体化は遅延生成なので、シーンの最初のフレームはまだ居ない)。
	//======================================================================================
	bool SceneSequence::IsPlayerDead(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return false;

		bool _isFound = false;
		bool _isDead  = false;

		a_context.pWorld->ForEach<const Component::ActiveTag, const Component::PlayerControllTag>(
			[&](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Component::ActiveTag* /*a_activeTagArray*/,
				const Component::PlayerControllTag* /*a_playerTagArray*/
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					_isFound = true;

					const Engine::ECS::Entity _entity = a_pChunk->entityData[_i];
					if (!a_context.pWorld->HasComponent<Component::HealthComponent>(_entity)) continue;

					const auto* _pHealth = a_context.pWorld->RefData<Component::HealthComponent>(_entity);
					if (_pHealth && _pHealth->isDead) _isDead = true;
				}
			}
		);

		if (_isFound)
		{
			m_isPlayerFound = true;
			return _isDead;
		}

		// 居たはずのものが見つからない = 消えた
		return m_isPlayerFound;
	}

	//======================================================================================
	// 全ウェーブを片付けたか
	//--------------------------------------------------------------------------------------
	// ボスも1つのウェーブとして出すので、ここで一緒に見ている。
	// ウェーブを1つも置いていないシーンは「終わりの無いシーン」として扱い、
	// 勝ちにはしない(置き忘れた瞬間にリザルトへ飛ぶのを防ぐ)。
	//======================================================================================
	bool SceneSequence::IsAllWaveCleared() const
	{
		if (m_waves.empty()) return false;

		for (const Wave& _wave : m_waves)
		{
			if (!_wave.isSpawned) return false;
			if (!_wave.isCleared) return false;
		}

		return true;
	}

	int SceneSequence::GetClearedWaveCount() const
	{
		int _count = 0;
		for (const Wave& _wave : m_waves)
		{
			if (_wave.isCleared) ++_count;
		}
		return _count;
	}

	//======================================================================================
	// 記録を移してリザルトシーンへ
	//--------------------------------------------------------------------------------------
	// スコアは倒すたびに ScoreSystem がグローバルへ足しているので、
	// ここで移すのはこのシーンでしか分からないもの(タイム・結末・ウェーブ数)だけ。
	//======================================================================================
	void SceneSequence::ReserveResultScene(Game::GlobalGameContext& a_gameData, Engine::Scene::SceneManager* a_pSceneManager)
	{
		m_isSceneRequested = true;

		// スローはここで戻す。重ねたリザルトの演出まで遅くならないように
		// (このシーンは重ねた後は止まるので、スローのまま止まった画が残る)
		RestoreTimeScale();

		auto& _gameData = a_gameData;

		// タイムは決着するまで毎フレーム入れているので、ここでは触らない
		_gameData.result           = m_result;
		_gameData.clearedWaveCount = GetClearedWaveCount();
		_gameData.totalWaveCount   = static_cast<int>(m_waves.size());

		if (!m_resultSceneGUID.IsValid())
		{
			// 設定していなければ記録だけ残してその場に留まる。
			// (作りかけのシーンで勝手に飛ばされない方が調べやすい)
			ENGINE_WARNING("[SceneSequence] リザルトシーンが設定されていません");
			return;
		}

		if (!a_pSceneManager) return;

		// 重ねるなら、このシーンは止まったまま後ろに残る(戻るのはリザルトが全部外して切り替える)
		a_pSceneManager->ReserveChangeScene(
			m_resultSceneGUID,
			m_isResultOverlay ? Engine::Scene::ESceneChangeType::Push : Engine::Scene::ESceneChangeType::Replace);
	}

	//======================================================================================
	// 勝ったときのスロー
	//--------------------------------------------------------------------------------------
	// 最後の敵が爆散した瞬間(決着した瞬間)から、時間の倍率を m_clearSlowScale へなだらかに落とす。
	// 倍率はアプリ全体のもの(MainEngine::SetTimeScale)なので、リザルトへ移るときと
	// このシーンが消えるときに必ず 1 へ戻す
	//======================================================================================
	void SceneSequence::UpdateClearSlow(Engine::GameObject::ObjectContext& a_context, float a_realDt)
	{
		if (!IsClearSlow()) return;
		if (!a_context.pServices || !a_context.pServices->pMainEngine) return;

		m_resultRealTime += a_realDt;
		const float _t = (m_clearSlowInTime > 0.0f) ? std::clamp(m_resultRealTime / m_clearSlowInTime, 0.0f, 1.0f) : 1.0f;
		const float _ease = _t * _t * (3.0f - 2.0f * _t);

		m_pSlowEngine = a_context.pServices->pMainEngine;
		m_pSlowEngine->SetTimeScale(std::lerp(1.0f, (std::max)(m_clearSlowScale, 0.0f), _ease));
	}

	void SceneSequence::RestoreTimeScale()
	{
		if (!m_pSlowEngine) return;

		m_pSlowEngine->SetTimeScale(1.0f);
		m_pSlowEngine = nullptr;
	}

	//======================================================================================
	// ポーズ
	//--------------------------------------------------------------------------------------
	// ポーズ画面は切り替え(Replace)ではなく重ねる(Push)。このシーンは消えずに残るので、
	// 閉じればウェーブの進行も敵の配置もそのまま続きから動く。
	//
	// 更新されるのは一番上のシーンだけなので、重ねている間ここは自動的に止まる。
	// 「止める」ための旗を別に持つ必要はない。
	//
	// 閉じるのは重ねた側(PauseSequence)の仕事。ここは重ねるところまでを持つ。
	//======================================================================================
	void SceneSequence::UpdatePause(Engine::GameObject::ObjectContext& a_context)
	{
		//----------------------------------------------------------------------
		// 重ねている間ここは更新されない。
		// つまり印が立ったままここへ来たのは「ポーズ画面が閉じられた」ときなので、
		// 印を下ろして続きから再開する
		//----------------------------------------------------------------------
		if (m_isPauseRequested)
		{
			m_isPauseRequested = false;
			return;
		}

		// 行き先が無ければ止められない
		if (!m_pauseSceneGUID.IsValid()) return;

		// 決着した後は止めない(リザルトへ移る待ち時間の最中)
		if (m_result != App::Game::EGameResult::None) return;

		if (!a_context.pServices || !a_context.pServices->pInputManager) return;
		if (!a_context.pServices->pInputManager->IsPress(m_pauseAction)) return;

		// 重ねる。実際に積まれるのは次のフレームの初め
		if (!a_context.pServices->pSceneManager) return;
		a_context.pServices->pSceneManager->ReserveChangeScene(
			m_pauseSceneGUID, Engine::Scene::ESceneChangeType::Push);

		m_isPauseRequested = true;
	}

	//======================================================================================
	// 生存数の集計
	//--------------------------------------------------------------------------------------
	// 自分のGUIDが付いた生存エンティティを、ウェーブ番号ごとに数える。
	// ActiveTag も条件に入れているので、解放待ち(ReleaseTag)の個体は数えない。
	//
	// 死亡状態(HealthComponent.isDead)の個体も数えない。倒されてから実際に消えるまでは
	// 死亡演出のぶんの猶予があり、その間もまだ ActiveTag が付いているため、
	// そのまま数えると全滅判定が演出の尺だけ遅れてしまう。
	//======================================================================================
	void SceneSequence::UpdateAliveCount(Engine::GameObject::ObjectContext& a_context)
	{
		for (Wave& _wave : m_waves) _wave.aliveCount = 0;

		const Core::GUID _selfGUID = GetGUID();
		if (!_selfGUID.IsValid()) return;

		a_context.pWorld->ForEach<const Component::ActiveTag, const Component::SpawnerComponent>(
			[&](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Component::ActiveTag* /*a_activeTagArray*/,
				const Component::SpawnerComponent* a_spawnerArray
			)
			{
				for (uint32_t _i = 0; _i < a_count; ++_i)
				{
					const Component::SpawnerComponent& _spawner = a_spawnerArray[_i];

					if (!(_spawner.spawnerGUID == _selfGUID)) continue;
					if (_spawner.waveIndex < 0) continue;
					if (static_cast<size_t>(_spawner.waveIndex) >= m_waves.size()) continue;

					// 倒されて消えるのを待っているだけの個体は生存に数えない
					const Engine::ECS::Entity _entity = a_pChunk->entityData[_i];
					if (a_context.pWorld->HasComponent<Component::HealthComponent>(_entity))
					{
						const auto* _pHealth = a_context.pWorld->RefData<Component::HealthComponent>(_entity);
						if (_pHealth && _pHealth->isDead) continue;
					}

					++m_waves[_spawner.waveIndex].aliveCount;
				}
			}
		);

		// 呼び出したシーンのオブジェクト。生死の見方は相手に任せる
		for (Wave& _wave : m_waves)
		{
			if (!_wave.isSpawned) continue;

			for (const Core::GUID& _guid : _wave.actorGUIDs)
			{
				const auto* _pActor = Picker::Find<SequenceActor>(a_context.pObjectManager, _guid);
				if (_pActor && _pActor->IsAlive()) ++_wave.aliveCount;
			}
		}
	}

	//======================================================================================
	// 全滅判定
	//======================================================================================
	void SceneSequence::UpdateClearState()
	{
		for (Wave& _wave : m_waves)
		{
			if (!_wave.isSpawned || _wave.isCleared) continue;

			// 1体も出せなかったウェーブは待つ相手がいないので即クリア扱い
			if (_wave.spawnCount <= 0)
			{
				_wave.isCleared   = true;
				_wave.clearedTime = m_time;
				continue;
			}

			// 実体化を確認するまでは全滅とみなさない(遅延生成の1フレームずれ対策)
			if (_wave.aliveCount > 0)
			{
				_wave.isConfirmed = true;
				continue;
			}

			const bool _isTimeout = (m_time - _wave.spawnTime) >= SPAWN_CONFIRM_TIMEOUT;
			if (!_wave.isConfirmed && !_isTimeout) continue;

			_wave.isCleared   = true;
			_wave.clearedTime = m_time;
		}
	}

	//======================================================================================
	// 出現条件
	//--------------------------------------------------------------------------------------
	//   isAnnihilation = false : シーン開始から timing 秒
	//   isAnnihilation = true  : 前のウェーブが全滅してから timing 秒
	//                            (先頭のウェーブは待つ相手がいないので時間だけ見る)
	//======================================================================================
	bool SceneSequence::CanSpawn(size_t a_index) const
	{
		if (a_index >= m_waves.size()) return false;

		const Wave& _wave = m_waves[a_index];

		if (!_wave.isAnnihilation || a_index == 0)
		{
			return m_time >= _wave.timing;
		}

		const Wave& _prev = m_waves[a_index - 1];
		if (!_prev.isCleared) return false;

		return (m_time - _prev.clearedTime) >= _wave.timing;
	}

	//======================================================================================
	// 出現
	//======================================================================================
	void SceneSequence::Spawn(Engine::GameObject::ObjectContext& a_context, size_t a_index)
	{
		if (a_index >= m_waves.size()) return;
		if (!a_context.pWorld) return;
		if (!a_context.pServices || !a_context.pServices->pResourceManager) return;

		Wave& _wave = m_waves[a_index];

		// 出現処理はこのフレームで完了させる。プレハブが引けずに0体でも進める
		// (待ち続けるとウェーブの連鎖が止まってしまうため)
		_wave.isSpawned  = true;
		_wave.spawnTime  = m_time;
		_wave.spawnCount = 0;

		for (SpawnSettings& _settings : _wave.spawnEntities)
		{
			App::Utility::SpawnParams _params = {};

			// 出現位置はウェーブ位置からの相対。ウェーブごと動かせるようにするため
			_params.pos = _wave.pos + _settings.pos;

			bool _hasRotation = false;
			const Math::Quaternion _quat = MakeYawQuat(_settings.dir, _hasRotation);
			_params.quat               = _quat;
			_params.isOverrideRotation = _hasRotation;

			// 全滅判定用の印。ウェーブ番号まで入れて自分の中で区別する
			_params.spawnerGUID = GetGUID();
			_params.waveIndex   = static_cast<int>(a_index);

			const bool _isSpawned = App::Utility::SpawnPrefab(
				*a_context.pWorld,
				*a_context.pServices->pResourceManager,
				_settings.spawnEntityGUID,
				_settings.spawnPrefabHandle,
				_params);

			if (_isSpawned) ++_wave.spawnCount;
		}

		// シーンに置いたオブジェクトを呼び出す。出る位置は相手が持っている
		for (const Core::GUID& _guid : _wave.actorGUIDs)
		{
			auto* _pActor = Picker::Find<SequenceActor>(a_context.pObjectManager, _guid);
			if (_pActor && _pActor->Appear(a_context)) ++_wave.spawnCount;
		}

		//------------------------------------------------------------------
		// 出たことを知らせる(表示と音は WaveAnnounceHUD の担当)
		//------------------------------------------------------------------
		// ここは「何番目が出たか」を置くだけで、見た目も音も持たない。
		// 1体も出せなかったウェーブでも知らせる。ウェーブが進んだこと自体は
		// 起きているので、黙って飛ばすと進行が止まったように見える
		if (a_context.pWorld->HasResource<InstanceResource::WaveAnnounceResource>())
		{
			a_context.pWorld->RefResource<InstanceResource::WaveAnnounceResource>().Push(
				static_cast<int>(a_index), static_cast<int>(m_waves.size()));
		}
	}

	//======================================================================================
	// デバッグキーでウェーブを飛ばす
	//--------------------------------------------------------------------------------------
	// 次に出るウェーブ(まだ出していない先頭)を条件を待たずに出す。
	// 最後のウェーブへ飛ばすときは、間のウェーブを「出したが誰も居なかった」ことにして片付ける
	// (spawnCount 0 のウェーブは次の全滅判定で即クリアになる)。
	//
	// 出ている敵(このシーケンスが出したプレハブ)は倒す予約をする。倒すのは次のフレームの
	// DebugKillSystem で、体力切れと同じく死亡エフェクトとスコアが出る。
	// シーンに置いたオブジェクト(SequenceActor。群れのボスなど)は倒さない。ボスは F3 で減らす。
	//
	// 全部出し終えていれば何もしない。決着した後も何もしない。
	//======================================================================================
	void SceneSequence::ApplyReservedDebugSkip(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pDebug = InstanceResource::DebugCommandResource::Find(a_context.pWorld);
		if (!_pDebug || !_pDebug->pCommands) return;

		const Game::DebugCommands& _commands = *_pDebug->pCommands;
		if (!_commands.isSkipWaveReserved && !_commands.isSkipToLastWaveReserved) return;
		if (m_result != App::Game::EGameResult::None) return;

		// まだ出していない先頭のウェーブ
		size_t _next = m_waves.size();
		for (size_t _i = 0; _i < m_waves.size(); ++_i)
		{
			if (m_waves[_i].isSpawned) continue;
			_next = _i;
			break;
		}
		if (_next >= m_waves.size())
		{
			ENGINE_LOG("[SceneSequence] 飛ばせるウェーブがありません(全部出し終えています)");
			return;
		}

		const size_t _target = _commands.isSkipToLastWaveReserved ? (m_waves.size() - 1) : _next;

		//----------------------------------------------------------------------
		// 出ている敵を倒す予約(倒れて消えるのを待っている個体は除く)
		//----------------------------------------------------------------------
		const Core::GUID _selfGUID = GetGUID();
		if (_selfGUID.IsValid())
		{
			a_context.pWorld->ForEach<const Component::ActiveTag, const Component::SpawnerComponent>(
				[&](
					Engine::ECS::Chunk* a_pChunk,
					uint32_t a_count,
					const Component::ActiveTag* /*a_activeTagArray*/,
					const Component::SpawnerComponent* a_spawnerArray
				)
				{
					for (uint32_t _i = 0; _i < a_count; ++_i)
					{
						if (!(a_spawnerArray[_i].spawnerGUID == _selfGUID)) continue;
						if (a_spawnerArray[_i].waveIndex < 0) continue;

						const Engine::ECS::Entity _entity = a_pChunk->entityData[_i];
						if (Component::IsDeadEntity(*a_context.pWorld, _entity)) continue;

						_pDebug->killReservedVec.push_back(_entity);
					}
				}
			);
		}

		//----------------------------------------------------------------------
		// 間のウェーブは誰も出さずに済ませ、行き先のウェーブを出す
		//----------------------------------------------------------------------
		for (size_t _i = _next; _i < _target; ++_i)
		{
			Wave& _wave = m_waves[_i];
			_wave.isSpawned  = true;
			_wave.spawnCount = 0;
			_wave.spawnTime  = m_time;
		}

		Spawn(a_context, _target);

		ENGINE_LOG("[SceneSequence] ウェーブ %d へ飛ばしました", static_cast<int>(_target) + 1);
	}

	//======================================================================================
	// 戦闘開始命令の送信条件
	//--------------------------------------------------------------------------------------
	//   afterWaveIndex < 0  : シーン開始から timing 秒
	//   afterWaveIndex >= 0 : そのウェーブが全滅してから timing 秒
	//                         (全滅していなければまだ送らない)
	//======================================================================================
	bool SceneSequence::CanSendOrder(size_t a_index) const
	{
		if (a_index >= m_bossOrders.size()) return false;

		const BossOrder& _order = m_bossOrders[a_index];

		if (_order.afterWaveIndex < 0)
		{
			return m_time >= _order.timing;
		}

		const size_t _waveIndex = static_cast<size_t>(_order.afterWaveIndex);
		if (_waveIndex >= m_waves.size()) return false;

		const Wave& _wave = m_waves[_waveIndex];
		if (!_wave.isCleared) return false;

		return (m_time - _wave.clearedTime) >= _order.timing;
	}

	//======================================================================================
	// 戦闘開始命令の送信
	//--------------------------------------------------------------------------------------
	// BossParamsComponent を持つエンティティの isCombatStarted を立てる。あとは
	// BossCombatIntentSystem が勝手に動き出すので、ここは合図を送るだけでよい。
	//
	// targetWaveIndex で相手を絞るときは、ウェーブの全滅判定と同じ SpawnerComponent の
	// 印を見る。絞らない(-1)ときはシーンに直接置いたボスにも届く。
	//
	// 1体も居なければ「送った」ことにせず、次のフレームでもう一度試す。プレハブの
	// 実体化は遅延生成なので、出した直後のフレームはまだ相手が居ないため。
	//======================================================================================
	void SceneSequence::SendBossOrders(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;
		if (m_bossOrders.empty()) return;

		const Core::GUID _selfGUID = GetGUID();

		for (size_t _i = 0; _i < m_bossOrders.size(); ++_i)
		{
			BossOrder& _order = m_bossOrders[_i];

			if (_order.isSent) continue;
			if (!CanSendOrder(_i)) continue;

			// 印で絞るのに自分のGUIDが無効だと誰にも当たらない。設定ミスで
			// 全ボスへ送ってしまうより、送らないほうが分かりやすい
			if (_order.targetWaveIndex >= 0 && !_selfGUID.IsValid()) continue;

			int _sentCount = 0;

			a_context.pWorld->ForEach<const Component::ActiveTag, Component::BossCommandComponent>(
				[&](
					Engine::ECS::Chunk* a_pChunk,
					uint32_t a_count,
					const Component::ActiveTag* /*a_activeTagArray*/,
					Component::BossCommandComponent* a_bossArray
				)
				{
					for (uint32_t _b = 0; _b < a_count; ++_b)
					{
						// ウェーブで絞る場合は、自分が出した個体かどうかを印で確かめる
						if (_order.targetWaveIndex >= 0)
						{
							const Engine::ECS::Entity _entity = a_pChunk->entityData[_b];

							if (!a_context.pWorld->HasComponent<Component::SpawnerComponent>(_entity)) continue;

							const auto* _pSpawner =
								a_context.pWorld->RefData<Component::SpawnerComponent>(_entity);
							if (!_pSpawner) continue;

							if (!(_pSpawner->spawnerGUID == _selfGUID)) continue;
							if (_pSpawner->waveIndex != _order.targetWaveIndex) continue;
						}

						a_bossArray[_b].isCombatStarted = true;
						++_sentCount;
					}
				}
			);

			// まだ相手が居ない。次のフレームで送り直す
			if (_sentCount <= 0) continue;

			_order.isSent    = true;
			_order.sentCount = _sentCount;
			_order.sentTime  = m_time;
		}
	}

	//======================================================================================
	// 進行状況のリセット(エディターから何度も試せるように)
	//======================================================================================
	void SceneSequence::ResetProgress()
	{
		m_time = 0.0f;

		// 決着もやり直す。遷移待ちの途中で押されても止められるようにしておく
		m_result           = App::Game::EGameResult::None;
		m_resultTimer      = 0.0f;
		m_isSceneRequested = false;
		m_isPlayerFound    = false;
		m_isStarted        = false;

		for (Wave& _wave : m_waves)
		{
			_wave.isSpawned   = false;
			_wave.spawnCount  = 0;
			_wave.aliveCount  = 0;
			_wave.isConfirmed = false;
			_wave.isCleared   = false;
			_wave.spawnTime   = 0.0f;
			_wave.clearedTime = 0.0f;
		}

		// 命令は送り直させる。すでに戦闘に入っているボスまでは止められない
		// (相手のIDを持っていないため)ので、必要なら個体のインスペクタから戻すこと
		for (BossOrder& _order : m_bossOrders)
		{
			_order.isSent    = false;
			_order.sentCount = 0;
			_order.sentTime  = 0.0f;
		}
	}

	//======================================================================================
	// ギズモの対象を外す(添え字がずれる操作のあとに呼ぶ)
	//======================================================================================
	void SceneSequence::ClearGizmoTarget()
	{
		m_gizmoWaveIndex  = -1;
		m_gizmoSpawnIndex = -1;
	}

	//======================================================================================
	// デバッグ描画 : ウェーブの基準位置と、そこからの出現位置・向きを出す
	//--------------------------------------------------------------------------------------
	// 出し終えたウェーブは灰色にして、これから出るものと見分けられるようにする。
	// ギズモで選んでいる点は白。
	//======================================================================================
	void SceneSequence::DrawSpawnMarker(Engine::GameObject::ObjectContext& a_context) const
	{
		if (!a_context.pServices) return;

		auto* _pDebugDraw = a_context.pServices->pDebugDraw;
		if (!_pDebugDraw) return;

		// 位置の十字(縦は上方向だけ伸ばして地面基準に見せる)
		auto _drawCross = [&](const Math::Vector3& a_pos, float a_size, const Math::Color& a_color)
			{
				_pDebugDraw->DrawLine(
					a_pos - Math::Vector3(a_size, 0.0f, 0.0f),
					a_pos + Math::Vector3(a_size, 0.0f, 0.0f), a_color);
				_pDebugDraw->DrawLine(
					a_pos - Math::Vector3(0.0f, 0.0f, a_size),
					a_pos + Math::Vector3(0.0f, 0.0f, a_size), a_color);
				_pDebugDraw->DrawLine(
					a_pos, a_pos + Math::Vector3(0.0f, a_size, 0.0f), a_color);
			};

		for (size_t _i = 0; _i < m_waves.size(); ++_i)
		{
			const Wave& _wave = m_waves[_i];

			const bool _isGizmoWave = (m_gizmoWaveIndex == static_cast<int>(_i));
			const Math::Color _baseColor = _wave.isSpawned ? DIMMED_COLOR : Math::Color(Engine::Color::GREEN);

			// ウェーブの基準位置は少し大きめに
			_drawCross(_wave.pos, MARKER_SIZE * 2.0f,
				(_isGizmoWave && m_gizmoSpawnIndex < 0) ? Math::Color(Engine::Color::WHITE) : _baseColor);

			for (size_t _s = 0; _s < _wave.spawnEntities.size(); ++_s)
			{
				const SpawnSettings& _settings = _wave.spawnEntities[_s];

				// 出現位置はウェーブからの相対
				const Math::Vector3 _pos = _wave.pos + _settings.pos;

				const bool _isGizmoSpawn =
					_isGizmoWave && (m_gizmoSpawnIndex == static_cast<int>(_s));
				const Math::Color _color = _isGizmoSpawn ? Math::Color(Engine::Color::WHITE) : _baseColor;

				_drawCross(_pos, MARKER_SIZE, _color);

				// ウェーブとのつながり
				_pDebugDraw->DrawLine(_wave.pos, _pos, _baseColor);

				// 向き
				Math::Vector3 _dir = { _settings.dir.x, 0.0f, _settings.dir.z };
				if (_dir.LengthSquared() > 1e-6f)
				{
					_dir.Normalize();
					_pDebugDraw->DrawLine(_pos, _pos + _dir * (MARKER_SIZE * 2.0f), Engine::Color::BLUE);
				}
			}

			// 呼び出すシーンのオブジェクト。位置は相手が持つので、ウェーブから線を引くだけ
			for (const Core::GUID& _guid : _wave.actorGUIDs)
			{
				const auto* _pActor = Picker::Find<SequenceActor>(a_context.pObjectManager, _guid);
				if (!_pActor) continue;

				const Math::Vector3 _pos = _pActor->GetAppearPos();
				_drawCross(_pos, MARKER_SIZE * 4.0f, _baseColor);
				_pDebugDraw->DrawLine(_wave.pos, _pos, _baseColor);
			}
		}
	}

	//======================================================================================
	// ギズモ : インスペクタで選んだ1点を動かす
	//--------------------------------------------------------------------------------------
	// ギズモは一度に1つの行列しか操作できないので、対象はインスペクタ側で選ぶ。
	// SetDrawlist / SetRect は呼び出し元(SceneViewPanel)が済ませてある。
	//======================================================================================
	bool SceneSequence::DrawGizmo(
		const Engine::GameObject::ObjectGizmoContext& a_ctx,
		Engine::GameObject::ObjectContext& /*a_context*/)
	{
		if (m_gizmoWaveIndex < 0) return false;
		if (static_cast<size_t>(m_gizmoWaveIndex) >= m_waves.size()) return false;

		Wave& _wave = m_waves[m_gizmoWaveIndex];

		// 動かす座標と、その座標が乗っている基準(ワールド)
		Math::Vector3* _pTargetPos = &_wave.pos;
		Math::Vector3  _origin     = {};

		if (m_gizmoSpawnIndex >= 0)
		{
			if (static_cast<size_t>(m_gizmoSpawnIndex) >= _wave.spawnEntities.size()) return false;

			_pTargetPos = &_wave.spawnEntities[m_gizmoSpawnIndex].pos;
			_origin     = _wave.pos;	// 出現位置はウェーブからの相対
		}

		const Math::Vector3 _worldPos = _origin + *_pTargetPos;

		Math::Matrix _mat = Math::Matrix::CreateTranslation(_worldPos);

		// Ctrl を押している間だけスナップ(エンティティ用ギズモと同じ操作感)
		const float _snap = Engine::EditorField::IsCtrlDown() ? 1.0f : 0.0f;

		if (Engine::EditorField::TranslateGizmo(a_ctx.viewMat, a_ctx.projMat, _mat, _snap))
		{
			// 保持しているのは相対座標なので、基準を引いてから書き戻す
			*_pTargetPos = Math::Vector3(_mat._41, _mat._42, _mat._43) - _origin;
		}

		return true;
	}

	//======================================================================================
	// シリアライズ : 設定値だけを保存する(進行状況は保存しない)
	//======================================================================================
	//======================================================================================
	// 解放
	//======================================================================================
	void SceneSequence::Release(Engine::GameObject::ObjectContext& a_context)
	{
		// 借りているBGMを返す
		m_bgm.Release(a_context);

		// スローのまま抜けた(リザルトを待たずに切り替わった)ときも倍率を戻す
		RestoreTimeScale();
	}

	void SceneSequence::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		//----------------------------------------------------------------------
		// ウェーブ
		//----------------------------------------------------------------------
		// キーが無ければ中身を飛ばすだけにして、後ろのボス命令まで読み進める
		// (ウェーブを持たず命令だけを置くシーケンスもあり得るため)。
		//----------------------------------------------------------------------
		size_t _waveCount = m_waves.size();
		if (a_ar.BeginArray("Waves", _waveCount))
		{
			m_waves.resize(_waveCount);

			for (size_t _i = 0; _i < _waveCount; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;

				Wave& _wave = m_waves[_i];
				a_ar.Field("pos", _wave.pos);
				a_ar.Field("timing", _wave.timing);
				a_ar.Field("isAnnihilation", _wave.isAnnihilation);

				size_t _spawnCount = _wave.spawnEntities.size();
				if (a_ar.BeginArray("Spawns", _spawnCount))
				{
					_wave.spawnEntities.resize(_spawnCount);

					for (size_t _s = 0; _s < _spawnCount; ++_s)
					{
						if (!a_ar.BeginObject(_s)) continue;

						SpawnSettings& _settings = _wave.spawnEntities[_s];
						a_ar.GUIDField("prefabGUID", _settings.spawnEntityGUID);
						a_ar.Field("pos", _settings.pos);
						a_ar.Field("dir", _settings.dir);

						a_ar.EndObject();
					}
					a_ar.EndArray();
				}

				a_ar.EndObject();
			}
			a_ar.EndArray();
		}

		//----------------------------------------------------------------------
		// ボスへの戦闘開始命令
		//----------------------------------------------------------------------
		// 既存のシーンにはこのキーが無い。読み込み時は BeginArray が false を返すので、
		// 命令なし(＝今までどおり)として読み込まれる。
		//----------------------------------------------------------------------
		size_t _orderCount = m_bossOrders.size();
		if (a_ar.BeginArray("BossOrders", _orderCount))
		{
			m_bossOrders.resize(_orderCount);

			for (size_t _i = 0; _i < _orderCount; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;

				BossOrder& _order = m_bossOrders[_i];
				a_ar.Field("timing", _order.timing);
				a_ar.Field("afterWaveIndex", _order.afterWaveIndex);
				a_ar.Field("targetWaveIndex", _order.targetWaveIndex);

				a_ar.EndObject();
			}
			a_ar.EndArray();
		}

		//----------------------------------------------------------------------
		// 決着(リザルトへの遷移)
		//
		// ※ 追加は末尾に。以前はここで早期 return していたので、
		//    命令が入っていないシーンだと後ろが読まれずに落ちていた
		//----------------------------------------------------------------------
		a_ar.GUIDField("ResultSceneGUID", m_resultSceneGUID);
		a_ar.Field("ClearDelay", m_clearDelay);
		a_ar.Field("DeadDelay", m_deadDelay);
		a_ar.Field("IsResetOnStart", m_isResetOnStart);

		//----------------------------------------------------------------------
		// ポーズ(重ねるシーン)
		//----------------------------------------------------------------------
		a_ar.GUIDField("PauseSceneGUID", m_pauseSceneGUID);
		Game::ActionField(a_ar, "PauseActionName", m_pauseAction);

		//----------------------------------------------------------------------
		// BGM
		//----------------------------------------------------------------------
		m_bgm.Archive(a_ar);

		//----------------------------------------------------------------------
		// ウェーブで呼び出すシーンのオブジェクト(ウェーブの番号順)
		//
		// ※ ウェーブの中ではなく末尾に置く。バイナリは順番に読むので、
		//    途中へ足すと以前の .obscene の読み出しがずれる。
		//    キーが無いシーンは呼び出すもの無しとして読み込まれる
		//----------------------------------------------------------------------
		size_t _actorWaveCount = m_waves.size();
		if (a_ar.BeginArray("WaveActors", _actorWaveCount))
		{
			for (size_t _i = 0; _i < _actorWaveCount; ++_i)
			{
				if (!a_ar.BeginObject(_i)) continue;

				// ウェーブより多く書かれていても、読み進めるために受け皿を用意する
				std::vector<Core::GUID> _discard = {};
				std::vector<Core::GUID>& _guidVec = (_i < m_waves.size()) ? m_waves[_i].actorGUIDs : _discard;
				a_ar.GUIDVectorField("GUIDs", _guidVec);

				a_ar.EndObject();
			}
			a_ar.EndArray();
		}

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		// 区切りを持たない古い .ob* には無いので読まない
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.Field("IsResultOverlay", m_isResultOverlay);
			a_ar.Field("ClearSlowScale", m_clearSlowScale);
			a_ar.Field("ClearSlowInTime", m_clearSlowInTime);
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void SceneSequence::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Value("Time", "%.2f", m_time);
		if (Engine::EditorField::Button("Reset Progress")) ResetProgress();

		Engine::EditorField::HelpText("Gizmo : select a position below (Ctrl to snap)");

		Engine::EditorField::Line();

		m_bgm.DrawInspector(a_context);

		Engine::EditorField::Line();

		//----------------------------------------------------------------------
		// 決着(リザルトへの遷移)
		//----------------------------------------------------------------------
		if (Engine::EditorField::CollapsingHeader("Result", true))
		{
			Engine::EditorField::HelpText("負け : プレイヤーが倒された");
			Engine::EditorField::HelpText("勝ち : 生き残って全ウェーブを全滅させた(ボスも1ウェーブ)");

			Engine::EditorField::AssetField(
				*a_context.pServices,
				"Result Scene", "Scene", m_resultSceneGUID);
			if (!m_resultSceneGUID.IsValid())
			{
				Engine::EditorField::HelpText("(未設定 : 決着しても移動しません)");
			}

			Engine::EditorField::Field("Clear Delay (s)", m_clearDelay, 0.1f, 0.0f, 60.0f);
			Engine::EditorField::Field("Dead Delay (s)", m_deadDelay, 0.1f, 0.0f, 60.0f);
			Engine::EditorField::Tooltip("決着してから移るまでの間(演出を見せる時間)");

			Engine::EditorField::Field("Result Overlay", m_isResultOverlay);
			Engine::EditorField::Tooltip("リザルトをこのシーンの上へ重ねる(止まった決着の瞬間を後ろに残す)。切ると切り替える");

			Engine::EditorField::Field("Clear Slow Scale", m_clearSlowScale, 0.01f, 0.0f, 1.0f);
			Engine::EditorField::Tooltip("勝った瞬間から時間の倍率をここまで落とす(1 でスローにしない)。スローの間は Clear Delay を実時間で数える");
			Engine::EditorField::Field("Clear Slow In Time (s)", m_clearSlowInTime, 0.05f, 0.0f, 10.0f);
			Engine::EditorField::Tooltip("倍率を落としきるまでの実時間");

			Engine::EditorField::Field("Reset On Start", m_isResetOnStart);
			Engine::EditorField::Tooltip("シーンの入り口でスコアとタイムを消す");

			// 実行中の状態は表示のみ
			Engine::EditorField::Line();
			const char* _resultName = "None";
			switch (m_result)
			{
			case App::Game::EGameResult::Clear:    _resultName = "Clear";    break;
			case App::Game::EGameResult::GameOver: _resultName = "GameOver"; break;
			default: break;
			}
			Engine::EditorField::Value("Result", "%s", _resultName);
			Engine::EditorField::Value("Timer", "%.2f", m_resultTimer);
			Engine::EditorField::Value("Cleared", "%d / %d", GetClearedWaveCount(), static_cast<int>(m_waves.size()));
			Engine::EditorField::Value("PlayerHit", "%s", m_isPlayerFound ? "found" : "not yet");
			Engine::EditorField::Value("Requested", "%s", m_isSceneRequested ? "yes" : "no");
		}

		//----------------------------------------------------------------------
		// ポーズ(重ねるシーン)
		//----------------------------------------------------------------------
		if (Engine::EditorField::CollapsingHeader("Pause", true))
		{
			Engine::EditorField::HelpText("切り替えずに重ねるので、閉じれば続きから再開する");

			Engine::EditorField::AssetField(
				*a_context.pServices,
				"Pause Scene", "Scene", m_pauseSceneGUID);
			if (!m_pauseSceneGUID.IsValid())
			{
				Engine::EditorField::HelpText("(未設定 : ポーズしません)");
			}

			Engine::EditorField::Field("Pause Action", m_pauseAction);
			Engine::EditorField::Tooltip("InputManager へ登録したアクション名(既定 : Esc)");

			Engine::EditorField::Value("Paused", "%s", m_isPauseRequested ? "yes" : "no");
		}

		Engine::EditorField::Line();

		if (Engine::EditorField::CreateButton("Add Wave")) m_waves.emplace_back();

		int _removeWaveIndex = -1;

		for (size_t _i = 0; _i < m_waves.size(); ++_i)
		{
			Wave& _wave = m_waves[_i];

			Engine::EditorField::IDScope _id(static_cast<int>(_i));

			const std::string _label = "Wave " + std::to_string(_i);
			if (Engine::EditorField::CollapsingHeader(_label.c_str(), true))
			{
				// ---- 基準位置 ----
				// ここを動かすと配下の出現位置(相対座標)がまとめて動く
				Engine::EditorField::Field("Wave Pos", _wave.pos, 0.1f);

				const bool _isGizmoWave =
					(m_gizmoWaveIndex == static_cast<int>(_i)) && (m_gizmoSpawnIndex < 0);
				if (Engine::EditorField::RadioButton("Gizmo##wave", _isGizmoWave))
				{
					m_gizmoWaveIndex  = static_cast<int>(_i);
					m_gizmoSpawnIndex = -1;
				}

				// ---- 出現条件 ----
				Engine::EditorField::Field("IsAnnihilation", _wave.isAnnihilation);
				Engine::EditorField::Tooltip(_wave.isAnnihilation ? "(after prev cleared)" : "(from scene start)");

				Engine::EditorField::Field("Timing", _wave.timing, 0.1f, 0.0f, 3600.0f);

				// ---- 進行状況 ----
				Engine::EditorField::HelpText("Spawned : %s / Alive : %d / Cleared : %s", _wave.isSpawned ? "yes" : "no", _wave.aliveCount, _wave.isCleared ? "yes" : "no");

				// ---- 出現させるエンティティ ----
				if (Engine::EditorField::CreateButton("Add Spawn")) _wave.spawnEntities.emplace_back();

				int _removeSpawnIndex = -1;

				for (size_t _s = 0; _s < _wave.spawnEntities.size(); ++_s)
				{
					SpawnSettings& _settings = _wave.spawnEntities[_s];

					Engine::EditorField::IDScope _spawnID(static_cast<int>(_s));
					Engine::EditorField::Line();

					// プレハブを選び直したらハンドルを捨てて解決し直させる
					if (Engine::EditorField::AssetField(
						*a_context.pServices,
						"Prefab", "Prefab", _settings.spawnEntityGUID))
					{
						_settings.spawnPrefabHandle = {};
					}

					// 位置はウェーブからの相対
					Engine::EditorField::Field("Pos (relative)", _settings.pos, 0.1f);

					const bool _isGizmoSpawn =
						(m_gizmoWaveIndex == static_cast<int>(_i)) &&
						(m_gizmoSpawnIndex == static_cast<int>(_s));
					if (Engine::EditorField::RadioButton("Gizmo##spawn", _isGizmoSpawn))
					{
						m_gizmoWaveIndex  = static_cast<int>(_i);
						m_gizmoSpawnIndex = static_cast<int>(_s);
					}

					Engine::EditorField::Field("Dir", _settings.dir, 0.01f);
					Engine::EditorField::Tooltip("World : %.2f, %.2f, %.2f", _wave.pos.x + _settings.pos.x, _wave.pos.y + _settings.pos.y, _wave.pos.z + _settings.pos.z);

					if (Engine::EditorField::DeleteButton("Remove Spawn")) _removeSpawnIndex = static_cast<int>(_s);
				}

				if (_removeSpawnIndex >= 0)
				{
					_wave.spawnEntities.erase(
						_wave.spawnEntities.begin() + _removeSpawnIndex);

					// 添え字がずれるのでギズモの対象は外す
					ClearGizmoTarget();
				}

				// ---- 呼び出すシーンのオブジェクト ----
				Engine::EditorField::Line();
				Engine::EditorField::HelpText("シーンに置いたボスなど。このウェーブまで出てこない(載せた後は読み直すと効く)");
				Picker::DrawList<SequenceActor>("Scene Actors", a_context.pObjectManager, _wave.actorGUIDs);

				Engine::EditorField::Line();
				if (Engine::EditorField::DeleteButton("Remove Wave")) _removeWaveIndex = static_cast<int>(_i);
			}
		}

		if (_removeWaveIndex >= 0)
		{
			m_waves.erase(m_waves.begin() + _removeWaveIndex);

			// ウェーブ番号がずれるので、出し済みの個体との対応も作り直す
			ResetProgress();
			ClearGizmoTarget();
		}

		//==================================================================================
		// ボスへの戦闘開始命令
		//==================================================================================
		Engine::EditorField::Header("Boss Orders");
		Engine::EditorField::HelpText("Bosses stand by until an order reaches them");

		if (Engine::EditorField::CreateButton("Add Boss Order")) m_bossOrders.emplace_back();

		int _removeOrderIndex = -1;

		for (size_t _i = 0; _i < m_bossOrders.size(); ++_i)
		{
			BossOrder& _order = m_bossOrders[_i];

			// ウェーブ側と添え字が衝突しないように別の基点でIDを振る
			Engine::EditorField::IDScope _id(static_cast<int>(_i) + 10000);

			const std::string _label = "Boss Order " + std::to_string(_i);
			if (Engine::EditorField::CollapsingHeader(_label.c_str(), true))
			{
				// ---- 送る条件 ----
				Engine::EditorField::Field("AfterWaveIndex", _order.afterWaveIndex, 0.1f, -1, static_cast<int>(m_waves.size()) - 1);
				Engine::EditorField::Tooltip(_order.afterWaveIndex < 0
					? "from scene start"
					: "after that wave cleared");

				Engine::EditorField::Field("Timing", _order.timing, 0.1f, 0.0f, 3600.0f);

				// ---- 送る相手 ----
				Engine::EditorField::Field("TargetWaveIndex", _order.targetWaveIndex, 0.1f, -1, static_cast<int>(m_waves.size()) - 1);
				Engine::EditorField::Tooltip(_order.targetWaveIndex < 0
					? "all bosses"
					: "bosses spawned by that wave");

				// ---- 進行状況 ----
				Engine::EditorField::HelpText("Sent : %s / Count : %d", _order.isSent ? "yes" : "no", _order.sentCount);

				if (Engine::EditorField::DeleteButton("Remove Boss Order"))
				{
					_removeOrderIndex = static_cast<int>(_i);
				}
			}
		}

		if (_removeOrderIndex >= 0)
		{
			m_bossOrders.erase(m_bossOrders.begin() + _removeOrderIndex);
		}
	}
}
