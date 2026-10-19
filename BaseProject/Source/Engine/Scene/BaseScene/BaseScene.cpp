#include "BaseScene.h"

#include "Engine/ECS/World/World.h"									// ECS
#include "Engine/Scene/SceneManager/SceneManager.h"					// シーンマネージャー

// コンポーネント
#include "Engine/ECS/Component/GUIDComponent.h"
#include "Engine/ECS/Component/TransientTag.h"

// エンジン系
#include "../../MainEngine.h"
#include "../../Resource/Manager/ResourceManager/ResourceManager.h"
#include "../../Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "../../Option/OptionManager.h"
#include "../../Physics/PhysicsWorld.h"
#include "../../Input/InputManager/InputManager.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/Graphics/Raytracing/RayEngine.h"
#include "../../Audio/AudioManager.h"
#include "../../GameObject/GameObjectManager/GameObjectManager.h"

// アプリ側UIオブジェクト

// リソースマネージャー
#include "../../Resource/Manager/ResourceManager/ResourceManager.h"

namespace Engine::Scene
{
	BaseScene::BaseScene()
	{}

	BaseScene::~BaseScene()
	{}

	//======================================================================================
	// シーン用ワールドの生成
	//--------------------------------------------------------------------------------------
	// 通常のシーンとエディターのプレビュー用シーンで同じものを使う。詳細はヘッダを参照。
	//======================================================================================
	std::unique_ptr<Engine::ECS::World> CreateSceneWorld(MainEngine& a_engine, bool a_isPreview)
	{
		// 実体を作るのは上位層(App::ECS::APPWorld)。
		// エンジンは基盤の Engine::ECS::World としてしか触らない
		auto* _pSceneManager = a_engine.RefSceneManager();
		if (!_pSceneManager) return nullptr;

		auto _upWorld = _pSceneManager->CreateWorld();
		if (!_upWorld) return nullptr;

		// 型情報はエンジンに1つ。どのワールドも同じものを借りるので、タイプIDが揃う
		_upWorld->Init(a_engine.RefComponentRegistry());

		// アプリ寿命のサービスを差し込む。
		// 組むのは MainEngine::BuildEngineServices(合成の入り口)で、ここは写すだけ。
		// 各システムは SystemContext 経由で受け取る。
		_upWorld->SetEngineServices(a_engine.GetEngineServices());

		// 物理空間(Jolt)。当たり判定はすべてここ。
		//
		// シーン(ワールド)ごとに1つ持つ。エンジンが1つだけ持つ共有物にすると、
		//   ・ポーズ画面を重ねただけで消すと後ろのゲームの静的コライダーが失われる
		//     (登録は Start の一度きりなので戻らない)
		//   ・エフェクトエディターのプレビューがゲームのコライダーと同じ空間に乗る
		// といった具合に、持ち主が誰なのかを場所ごとに考える必要がある(以前の自作判定がそうだった)。
		// ワールドと同じ寿命にしておけば、シーンを消せば当たり判定も一緒に消える。
		//
		// ここで足しているのでプレビュー用のワールドにも必ず1つある。
		// システムは a_ctx.pWorld->RefResource<Physics::PhysicsWorld>() で引くこと。
		// PhysicsSystem::Init で先に確保するので、プレビューは小さくしておく
		_upWorld->AddResource<Physics::PhysicsWorld>(
			_upWorld->RefEngineServices()->pPhysicsEngine,
			a_isPreview ? Physics::PhysicsWorldDesc::MakePreview() : Physics::PhysicsWorldDesc{});

		// ゲーム固有のコンポーネントとシステムの登録。
		// 何を登録するかはワールドの実体(派生)が持っている
		_upWorld->RegisterGameTypes();

		return _upWorld;
	}

	void BaseScene::Enter(MainEngine& a_engine)
	{
		// 初期化中
		m_state = EState::PreLoad;

		// ワールド作成
		m_upWorld = CreateSceneWorld(a_engine);

		// ECS外オブジェクトの生成
		// 中身はシーン読み込み(Archive)またはエディターの AddObject で追加される。
		// 自シーンのワールドを渡し、各オブジェクトへは ObjectContext 経由で配らせる。
		m_upGameObjectManager = std::make_unique<GameObject::GameObjectManager>(m_upWorld.get());

		// 音量の席を取る。値はシーンファイルを読んだときに流し込まれる
		m_audio.Register(a_engine.RefAudioManager());

		// 物理の設定(重力)の流し込み先。ここで既定値を入れ、シーンファイルを読んだときに上書きされる
		if (m_upWorld && m_upWorld->HasResource<Physics::PhysicsWorld>())
		{
			m_physics.Register(&m_upWorld->RefResource<Physics::PhysicsWorld>());
		}
	}

	//======================================================================================
	// 読み込み
	//--------------------------------------------------------------------------------------
	// 1フレームで組み立て切ると、重いシーン(Desert_02 で約1秒)の間は画面が止まり、
	// ロード画面のバーも動かない。そこで毎フレーム少しずつ進める。
	//
	// ・先読みは予算(PRELOAD_BUDGET_MS)の範囲で要求する。
	//   その場で読む種別(アニメーター・エフェクト・プレハブなど)が重いので、
	//   ここで区切ると読み込み中もフレームが回る。
	// ・先読みを済ませてから組み立てるので、組み立てと初期化のフェーズ(Fixup)は
	//   キャッシュに当たって軽くなる。
	// ・物理への登録は Start で行われるので、全エンティティが Start を通り終えるまで
	//   (= 動き出していない数が 0 になるまで)待ってから Ready にする。
	//   モデルの到着待ちで Awake に止まっているものもここで待つ。
	//======================================================================================
	namespace
	{
		// 1フレームで先読みに使ってよい時間。少なくとも1件は進める
		constexpr double PRELOAD_BUDGET_MS = 8.0;

		// 進み具合のうち先読みが占める割合(先読み一覧が空なら全部を組み立て側に回す)
		constexpr float PRELOAD_PROGRESS_WEIGHT = 0.7f;

		// 落ち着き待ちの打ち切り : もう届かないものを待ち続けてシーンが始まらないのを防ぐ
		constexpr double SETTLE_TIMEOUT_SEC = 10.0;

		double ElapsedMs(std::chrono::steady_clock::time_point a_begin)
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a_begin).count();
		}
	}

	void BaseScene::BeginLoad(const std::string& a_fileDir, const std::string& a_fileName, bool a_isPreLoad)
	{
		m_fileDir = a_fileDir;
		m_fileName = a_fileName;

		m_preLoadIndex = 0;
		m_preLoadDoneCount = 0;
		m_preLoadWaitVec.clear();

		m_settleMaxRemaining = 0;
		m_settleProgress = 0.0f;
		m_isSettleStarted = false;

		m_loadStartTime = std::chrono::steady_clock::now();

		// 先読みしないときは一覧を読み終えたことにする(進み具合も満たしておく)
		if (!a_isPreLoad)
		{
			m_preLoadIndex = m_config.GetPreLoadAssetGUIDs().size();
			m_preLoadDoneCount = m_preLoadIndex;
		}

		m_state = EState::PreLoad;
	}

	void BaseScene::UpdateLoad(Resource::ResourceManager& a_resourceManager)
	{
		if (m_state == EState::Ready) return;

		// 落ち着き待ちの間に動き出したもの(BGMの進行役など)が鳴らす音も、このシーンの音量で鳴らす
		Audio::VolumeScopeGuard _volumeScope(m_audio.RefAudioManager(), m_audio.GetVolumeScopeID());

		const auto _frameBegin = std::chrono::steady_clock::now();

		//------------------------------------------------------------------
		// 先読み : 予算の範囲で要求する
		//------------------------------------------------------------------
		if (m_state == EState::PreLoad)
		{
			if (!UpdatePreLoad(a_resourceManager, PRELOAD_BUDGET_MS)) return;
			m_state = EState::Build;

			// 予算を使い切っていたら組み立ては次のフレーム
			if (ElapsedMs(_frameBegin) >= PRELOAD_BUDGET_MS) return;
		}

		//------------------------------------------------------------------
		// 組み立て : シーンファイルを読む(1フレームで済ませる)
		//------------------------------------------------------------------
		if (m_state == EState::Build)
		{
			BuildFromFile();
			m_state = EState::Settle;

			// 軽ければ同じフレームで落ち着き待ちまで進める(ポーズ画面などは1フレームで済む)
			if (ElapsedMs(_frameBegin) >= PRELOAD_BUDGET_MS) return;
		}

		//------------------------------------------------------------------
		// 落ち着き待ち : 全エンティティが Start を通り終えるまで
		//------------------------------------------------------------------
		if (m_state == EState::Settle)
		{
			if (!UpdateSettle(a_resourceManager)) return;
			m_state = EState::Ready;

			ENGINE_LOG("[Scene] 読み込み完了 : %s (%.0f ms)", m_fileName.c_str(), ElapsedMs(m_loadStartTime));
		}
	}

	float BaseScene::GetLoadProgress() const
	{
		if (m_state == EState::Ready) return 1.0f;

		const size_t _total = m_config.GetPreLoadAssetGUIDs().size();
		const float _preLoadRatio = (_total == 0) ? 1.0f : static_cast<float>(m_preLoadDoneCount) / static_cast<float>(_total);
		const float _preLoadWeight = (_total == 0) ? 0.0f : PRELOAD_PROGRESS_WEIGHT;

		return std::clamp(_preLoadWeight * _preLoadRatio + (1.0f - _preLoadWeight) * m_settleProgress, 0.0f, 1.0f);
	}

	//======================================================================================
	// 先読み
	//--------------------------------------------------------------------------------------
	// 参照は持たない。読んだものはキャッシュに残り、組み立てやシステムが
	// 同じGUIDを引いたときにそれを受け取る。
	// キャッシュを片付けるのはシーンが1つも残らなくなったときだけなので、
	// このシーンを開いている間に誰も持っていなくても捨てられない。
	//
	// ジョブへ流す種別(モデル・テクスチャなど)は要求しただけで次へ進み、
	// 届いたかは UpdatePreLoadWait で数える。届くのは組み立てと並んでいてよい
	//======================================================================================
	bool BaseScene::UpdatePreLoad(Resource::ResourceManager& a_resourceManager, double a_budgetMs)
	{
		const auto& _guidVec = m_config.GetPreLoadAssetGUIDs();
		const auto _begin = std::chrono::steady_clock::now();

		while (m_preLoadIndex < _guidVec.size())
		{
			const Core::GUID& _guid = _guidVec[m_preLoadIndex++];

			// 読めない種別・消えたアセットは読み飛ばす(止まらないように済んだ扱い)
			if (!a_resourceManager.RequestLoadByGUID(_guid) ||
				a_resourceManager.GetStateByGUID(_guid) != Resource::EResourceState::Loading)
			{
				++m_preLoadDoneCount;
			}
			else
			{
				m_preLoadWaitVec.push_back(_guid);
			}

			if (ElapsedMs(_begin) >= a_budgetMs) break;
		}

		UpdatePreLoadWait(a_resourceManager);

		return m_preLoadIndex >= _guidVec.size();
	}

	void BaseScene::UpdatePreLoadWait(Resource::ResourceManager& a_resourceManager)
	{
		std::erase_if(m_preLoadWaitVec,
			[this, &a_resourceManager](const Core::GUID& a_guid)
			{
				if (a_resourceManager.GetStateByGUID(a_guid) == Resource::EResourceState::Loading) return false;
				++m_preLoadDoneCount;
				return true;
			});
	}

	//======================================================================================
	// 組み立て
	//--------------------------------------------------------------------------------------
	// 形式はビルドモード任せ(Auto)。Development までは .ojscene 優先、Shipping は .obscene のみ。
	// 場所が無いシーン(保存前に作ったものなど)は空のまま進める
	//======================================================================================
	void BaseScene::BuildFromFile()
	{
		if (m_fileName.empty()) return;

		Persistence::Archive _ar(Persistence::Archive::EMode::Load, m_fileDir, m_fileName, "scene");
		Archive(_ar);
	}

	//======================================================================================
	// 落ち着き待ち
	//--------------------------------------------------------------------------------------
	// 回すのは初期化のフェーズ(BeginFrame)と物理への反映だけ。
	// 入力・更新・描画のフェーズと GameObject は回さない
	// (シーンの進行役の経過時間やBGMが、読み込み中に始まらないように)。
	//======================================================================================
	bool BaseScene::UpdateSettle(Resource::ResourceManager& a_resourceManager)
	{
		if (!m_isSettleStarted)
		{
			m_isSettleStarted = true;
			m_settleStartTime = std::chrono::steady_clock::now();
		}

		// 初期化のフェーズを通す。リソースが届いていないものは Awake に残る
		m_upWorld->BeginFrame();

		// Start で作ったボディを空間へ入れておく(物理を進めはしない)
		m_upWorld->RefResource<Physics::PhysicsWorld>().FlushPendingBodies();

		// 先読みのうちジョブへ流したもの
		UpdatePreLoadWait(a_resourceManager);

		//------------------------------------------------------------------
		// 進み具合
		//
		// 動き出していないエンティティの数だけで測ると、重いモデルを待つ数体だけが残った間
		// ほぼ 100% のまま止まって見える。読み込み中のアセットの数も残りに足し、
		// これまでで一番多かった残りに対する割合で進める(戻らないよう最大値を取る)
		//------------------------------------------------------------------
		const uint32_t _pending = m_upWorld->GetPendingStartCount();
		const uint32_t _remaining = _pending + a_resourceManager.GetInFlightLoadCount()
			+ static_cast<uint32_t>(m_preLoadWaitVec.size());
		m_settleMaxRemaining = std::max(m_settleMaxRemaining, _remaining);
		if (m_settleMaxRemaining > 0)
		{
			const float _ratio = 1.0f - static_cast<float>(_remaining) / static_cast<float>(m_settleMaxRemaining);
			m_settleProgress = std::max(m_settleProgress, std::clamp(_ratio, 0.0f, 1.0f));
		}

		if (_pending == 0 && m_preLoadWaitVec.empty())
		{
			m_settleProgress = 1.0f;
			return true;
		}

		// 届かないものを待ち続けない : 知らせて先へ進める
		if (ElapsedMs(m_settleStartTime) >= SETTLE_TIMEOUT_SEC * 1000.0)
		{
			ENGINE_WARNING("[Scene] 読み込みの待ちを打ち切りました : %s (動き出していないエンティティ %u / 未着の先読み %zu)",
				m_fileName.c_str(), _pending, m_preLoadWaitVec.size());
			return true;
		}

		return false;
	}

	//======================================================================================
	// 解放
	//--------------------------------------------------------------------------------------
	// エンティティを消すところまで。コンポーネントが借りているリソースは
	// 解放フック(ComponentTraits<T>::Release)が返すので、ここで数え直すことはしない。
	//
	// 当たり判定の空間はワールドの持ち物なので、ワールドと一緒に消える。
	// 残るのは「誰も持っていないリソースの破棄」だけで、これは他にシーンが
	// 残っているかを見ないと決められないので呼び出し側(SceneManager::PopScene)が持つ。
	//======================================================================================
	void BaseScene::Exit()
	{
		m_upWorld->Release();

		// 音量の席を返す。音を借りていたものは上の Release で返し終わっている
		m_audio.Unregister();

		// 物理空間はワールドの持ち物なので、ワールドより先に手を離す
		m_physics.Unregister();
	}

	void BaseScene::Update(float a_dt)
	{
		// 読み込み中は UpdateLoad だけが回す
		if (!IsReady()) return;

		// ここで発行された音には、このシーンの音量の札が付く
		Audio::VolumeScopeGuard _volumeScope(m_audio.RefAudioManager(), m_audio.GetVolumeScopeID());

		m_upGameObjectManager->PreUpdate();

		// シーンの初めに一括でエンティティを生成・削除
		// 解放処理と初期化処理も含まれているため、呼び出しはシングルスレッド限定
		// (この中で Start フェーズが走り、コライダーのボディ登録もここで行われる)
		m_upWorld->BeginFrame();

		// シーンのシステム処理
		//
		// 入力フェーズはモードに関わらず毎フレーム回す。
		// プレイモード以外では InputManager が無入力を返すので、入力フェーズの
		// システムが MoveIntent などへ 0 を書き込み続けることになる。
		// ここを止めてしまうと最後に書き込まれた入力がそのまま残り、
		// エディターへ戻ってもプレイヤーが走り続ける(＝入力が残る)。
		m_upWorld->RunSystem(Engine::ECS::ESystemType::Input, a_dt);

		m_upWorld->RunSystem(Engine::ECS::ESystemType::PreUpdate, a_dt);

		m_upWorld->RunSystem(Engine::ECS::ESystemType::Update, a_dt);

		// 物理空間を1ステップ進める。必ず Physics フェーズの前に置くこと。
		//   ・上の BeginFrame(Start フェーズ)で作ったボディをここでまとめて空間へ入れるので、
		//     置いたそのフレームから判定クエリに乗る
		//   ・動くボディの位置合わせ(Update フェーズの SyncPhysicsBodySystem)の後なので、
		//     判定クエリ(Physics フェーズ)は今フレームの位置を見る
		{
			ENGINE_PROFILE_SCOPE("Physics_Update");
			m_upWorld->RefResource<Physics::PhysicsWorld>().Update(a_dt);
		}

		m_upWorld->RunSystem(Engine::ECS::ESystemType::Physics, a_dt);

		m_upWorld->RunSystem(Engine::ECS::ESystemType::Animation, a_dt);

		m_upWorld->RunSystem(Engine::ECS::ESystemType::Camera, a_dt);

		m_upWorld->RunSystem(Engine::ECS::ESystemType::PostUpdate, a_dt);

		m_upGameObjectManager->Update(a_dt);
	}

	void BaseScene::Draw()
	{
		// 組み立て途中のワールドは描かない
		if (!IsReady()) return;

		// 描画のフェーズで鳴らすもの(エフェクトの再生など)もこのシーンの音量で鳴らす
		Audio::VolumeScopeGuard _volumeScope(m_audio.RefAudioManager(), m_audio.GetVolumeScopeID());

		// 判定メッシュのボディのAABBをデバッグ表示へ積む(静的=水色、動く=黄色)。
		// 積む先はエンジン側の置き場で、実際に出すかどうかは
		// DebugDrawOption(エディターの表示設定)が決める
		m_upWorld->RefResource<Engine::Physics::PhysicsWorld>()
			.DrawDebug(m_upWorld->RefEngineServices()->pDebugDraw);

		// このワールドで出てきたアニメーションモデルの BLAS と頂点領域を用意する。
		// 描画のシステムは用意された領域の位置を読むので、必ず PreDraw より前
		if (auto* _pMainEngine = m_upWorld->RefEngineServices()->pMainEngine)
		{
			if (auto* _pGE = _pMainEngine->RefGraphicsEngine())
			{
				_pGE->RefDrawSubmitter()->ProcessDynamicRaytracingInit(*m_upWorld);
			}
		}

		m_upWorld->RunSystem(Engine::ECS::ESystemType::PreDraw, 0.0f);

		m_upWorld->RunSystem(Engine::ECS::ESystemType::Draw, 0.0f);

		m_upWorld->RunSystem(Engine::ECS::ESystemType::PostDraw, 0.0f);

		m_upGameObjectManager->Draw(0.0f);
	}

	void BaseScene::Archive(Persistence::Archive& a_ar)
	{
		size_t _entityCount = 0;
		std::vector<ECS::Entity> _entityVec = {};

		// ---------------------------------------------------------
		// セーブ時のみ：保存対象のエンティティを事前収集
		//
		// 実行中にプレハブから出したもの(TransientTag)は飛ばす。
		// 書き込むと、次に開いたときにスポナーが出し直すぶんと二重になる
		// ---------------------------------------------------------
		if (a_ar.GetMode() == Persistence::Archive::EMode::Save)
		{
			const ECS::ComponentTypeID _transientTypeID = m_upWorld->GetCompTypeID<Engine::ECS::TransientTag>();

			m_upWorld->ForEach<Engine::ECS::GUIDComponent>(
				[this, &_entityVec, _transientTypeID](ECS::Chunk* a_pChunk, uint32_t a_count, Engine::ECS::GUIDComponent* /*a_guidArray*/)
				{
					for (size_t _i = 0; _i < a_count; ++_i)
					{
						const ECS::Entity _entity = a_pChunk->entityData[_i];
						if (m_upWorld->HasComponent(_entity, _transientTypeID)) continue;

						_entityVec.push_back(_entity);
					}
				}
			);
			_entityCount = _entityVec.size();
		}

		// ---------------------------------------------------------
		// 配列の処理（セーブ時はサイズを書き込み、ロード時は読み込んで_entityCountに入る）
		// ---------------------------------------------------------
		if (a_ar.BeginArray("Entities", _entityCount))
		{
			for (size_t _i = 0; _i < _entityCount; ++_i)
			{
				if (a_ar.BeginObject(_i))
				{
					std::vector<std::string> _compNames;
					ECS::Entity _entity;

					// 【セーブ時のみ】エンティティからコンポーネント名リストを作成
					if (a_ar.GetMode() == Persistence::Archive::EMode::Save)
					{
						_entity = _entityVec[_i];
						_compNames = m_upWorld->GetComponentNames(m_upWorld->GetSignature(_entity));
					}

					// コンポーネント名のリストを保存 or 読み込み
					// セーブなら書き込まれ、ロードなら _compNames にデータが入る
					a_ar.VectorField("ComponentNames", _compNames);

					// 【ロード時のみ】読み込んだリストからシグネチャを作り、エンティティを生成
					if (a_ar.GetMode() == Persistence::Archive::EMode::Load)
					{
						ECS::Signature _sig = {};
						for (const std::string& _name : _compNames)
						{
							ECS::ComponentTypeID _typeID = m_upWorld->GetCompTypeID(_name);
							if (_typeID != ECS::Limits::INVALID_COMPONENTTYPEID)
							{
								_sig.set(_typeID);
							}
						}
						_entity = m_upWorld->CreateEntity(_sig);
					}

					// ---------------------------------------------------------
					// 各コンポーネントデータのシリアライズ
					// ---------------------------------------------------------
					for (const std::string& _name : _compNames)
					{
						ECS::ComponentTypeID _typeID = m_upWorld->GetCompTypeID(_name);
						if (_typeID == ECS::Limits::INVALID_COMPONENTTYPEID) continue;

						auto _func = m_upWorld->GetCompFunc(_typeID).archive;
						if (_func)
						{
							// セーブもロードも同じグループ構造で実行
							if (a_ar.BeginGroup(_name))
							{
								void* _data = m_upWorld->NRefData(_entity, _typeID);
								_func(a_ar, _data);
								a_ar.EndGroup();
							}
						}
					}
					a_ar.EndObject(); // エンティティオブジェクトの終了
				}
			}
			a_ar.EndArray(); // エンティティ配列の終了
		}

		// ---------------------------------------------------------
		// ECS外オブジェクト(GameObject)のシリアライズ
		// タイプインデックス / GUID / データ を GameObjectManager 側で処理する。
		// ---------------------------------------------------------
		if (m_upGameObjectManager)
		{
			m_upGameObjectManager->Archive(a_ar);
		}

		// ---------------------------------------------------------
		// シーンの環境設定
		// 読み込み後はテクスチャ(空・フォグのノイズ)の読み込みを始めさせる
		// (実体が届くのは待たない)
		// ---------------------------------------------------------
		if (a_ar.BeginGroup("Ambient"))
		{
			m_ambient.Archive(a_ar);
			a_ar.EndGroup();
		}

		// ---------------------------------------------------------
		// シーンの音量
		// 後から足したので区切りで囲む : 古いバイナリ(.obscene)には無く、
		// そのときは読まずに既定値(素通し)のまま残す
		// ---------------------------------------------------------
		if (a_ar.BeginGroup("Audio"))
		{
			Persistence::ArchiveSection _section(a_ar, "SceneAudio");
			if (!a_ar.IsLegacyLayout()) m_audio.Archive(a_ar);
			a_ar.EndGroup();
		}

		// ---------------------------------------------------------
		// シーンの物理の設定(重力)
		// 音量と同じく後から足したので区切りで囲む : 無いファイルは既定値のまま
		// ---------------------------------------------------------
		if (a_ar.BeginGroup("Physics"))
		{
			Persistence::ArchiveSection _section(a_ar, "ScenePhysics");
			if (!a_ar.IsLegacyLayout()) m_physics.Archive(a_ar);
			a_ar.EndGroup();
		}

		if (a_ar.IsLoading())
		{
			if (auto* _pServices = m_upWorld->RefEngineServices(); _pServices && _pServices->pResourceManager)
			{
				m_ambient.RequestLoadAssets(*_pServices->pResourceManager);
			}
		}
	}
}