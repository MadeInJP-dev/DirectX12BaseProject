#include "SceneManager.h"

#include "../BaseScene/BaseScene.h"
#include "../../ECS/World/World.h"	// unique_ptr<World> を扱うので完全型が要る

#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"

#include "../../Resource/Manager/AssetDatabase/AssetDatabase.h"
#include "../../Resource/Manager/ResourceManager/ResourceManager.h"

#include "../../Audio/AudioManager.h"

#include "Engine/DevTool/IDevTool.h"

namespace Engine::Scene
{
	void SceneManager::Release()
	{
		// エディターが覚えている選択はここで消えるシーンのもの
		if (auto* _pDevTool = m_pEngine->RefDevTool()) _pDevTool->OnSceneChanged();

		//----------------------------------------------------------------------------------
		// 上のシーンから順に、PopScene と同じく後始末を通して消す
		//
		// clear() だけだと Exit(= World::Release)が走らず、Release フェーズのシステムが
		// 返すはずのもの(動的BLAS・ポーズ領域・音など)がワールドごと直接壊れる。
		// 動的BLASは GPU が最後のフレームで読んでいる最中に最終解放され、
		// デバッグレイヤーが CORRUPTION で止まっていた(終了時にたまに落ちる原因)。
		//
		// 待つのは Present まで含めて : 終了時なのでキューを空にしてよい
		//----------------------------------------------------------------------------------
		if (!m_upBaseSceneVec.empty() || m_upLoadingScreen)
		{
			if (auto* _pGE = m_pEngine->RefGraphicsEngine())
			{
				_pGE->RefRenderDevice()->WaitForGPUIdle();
			}
		}
		while (!m_upBaseSceneVec.empty())
		{
			// 計測中のシーンを開いたまま終了した : ここまでに読んだもので一覧を置き換える
			if (m_upBaseSceneVec.back().get() == m_pPreLoadRecordingScene)
			{
				if (auto* _pResourceManager = m_pEngine->RefResourceManager())
				{
					EndRecordPreLoadAssets(*_pResourceManager);
				}
			}

			m_upBaseSceneVec.back()->Exit();
			m_upBaseSceneVec.pop_back();
		}
		m_pPreLoadRecordingScene = nullptr;

		// ロード画面 : スタックの外に居るので別に消す
		if (m_upLoadingScreen)
		{
			m_upLoadingScreen->Exit();
			m_upLoadingScreen.reset();
		}
		m_loadingTime = 0.0f;

		// 消えたシーンのテクスチャを指したままにせず、平行光の席も返す
		ApplySceneAmbient();
	}

	//======================================================================================
	// エフェクトエディターが開いているか
	//--------------------------------------------------------------------------------------
	// 開いている間はゲームのシーンを止め、あちらの確認用ワールドだけを回す。
	// エフェクト単体を見るための画面なので、後ろでゲームが動いていると
	// 描画も当たり判定も混ざってしまう。
	//======================================================================================
	namespace
	{
		// 開発ツールが確認用シーン(エフェクトの確認など)を回しているなら、その窓口を返す
		DevTool::IDevTool* RefActiveScenePreview(MainEngine& a_engine)
		{
			auto* _pDevTool = a_engine.RefDevTool();
			if (!_pDevTool || !_pDevTool->IsScenePreviewActive()) return nullptr;
			return _pDevTool;
		}
	}

	void SceneManager::Update(Resource::ResourceManager& a_resourceManager, float a_dt)
	{
		// エフェクト確認中はゲームのシーンを止める。
		// シーンの切り替え命令もここで消化しないので、閉じたあとに順番どおり流れる
		if (auto* _pDevTool = RefActiveScenePreview(*m_pEngine))
		{
			// 確認用ワールドで鳴らす音は、開いているシーンの音量で鳴らす(本番と同じつり合いで聞くため)
			Audio::AudioManager* _pAudioManager = nullptr;
			Audio::VolumeScopeID _scopeID = Audio::INVALID_VOLUME_SCOPE_ID;
			if (auto* _pTopScene = RefCurrentTopScene())
			{
				_pAudioManager = _pTopScene->RefAudio().RefAudioManager();
				_scopeID = _pTopScene->GetAudio().GetVolumeScopeID();
			}
			Audio::VolumeScopeGuard _volumeScope(_pAudioManager, _scopeID);

			_pDevTool->UpdateScenePreview(a_dt);
			return;
		}

		// シーンの切り替え
		ApplyReservedSceneChanges(a_resourceManager);

		//==================================================================
		// 読み込み
		//------------------------------------------------------------------
		// 積んだばかりのシーンを1歩ずつ組み立てる。済んだものは同じフレームから更新に乗る。
		// ロード画面も同じ形で読み込む(起動直後は出せる状態になっていない)
		//==================================================================
		bool _isLoading = false;
		for (auto& _upScene : m_upBaseSceneVec)
		{
			if (_upScene->IsReady()) continue;

			UpdateSceneLoad(a_resourceManager, *_upScene);
			if (!_upScene->IsReady()) _isLoading = true;
		}

		if (m_upLoadingScreen && !m_upLoadingScreen->IsReady())
		{
			UpdateSceneLoad(a_resourceManager, *m_upLoadingScreen);
		}

		// 読み込みが続いている時間 : ロード画面を出すかの判定に使う
		m_loadingTime = _isLoading ? (m_loadingTime + a_dt) : 0.0f;

		// ロード画面の更新(バーの値はロード画面の進行役が GetLoadProgress から引く)
		if (IsLoadingScreenVisible())
		{
			m_upLoadingScreen->Update(a_dt);
		}

		//==================================================================
		// シーンの更新
		//------------------------------------------------------------------
		// 既定は一番上のシーンだけ。重ねたシーン(ポーズ画面)を出している間、
		// 後ろのゲームは止まっていてほしいため。
		//
		// 描画(Draw)は積んであるシーンを全部通すので、止まっていても後ろは
		// 見えたままになる。
		//
		// 後ろも一緒に動かしたい重ね方をするときだけ、この切り替えを外す。
		//
		// 読み込み中のシーンは更新しない(BaseScene::Update の中でも弾いている)。
		// 一番上が読み込み中なら、後ろのシーンも止めたままにする
		//==================================================================
		if (m_isUpdateTopSceneOnly)
		{
			// 最前面のみ更新
			if (!m_upBaseSceneVec.empty())
			{
				m_upBaseSceneVec.back()->Update(a_dt);
			}
		}
		else
		{
			// すべてのシーンを更新
			for (auto& _scene : m_upBaseSceneVec)
			{
				_scene->Update(a_dt);
			}
		}
	}

	void SceneManager::Draw()
	{
		// エフェクト確認中は、あちらのワールドの描画命令だけをレンダーグラフへ流す。
		// レンダーグラフ自体はゲームと同じものを通るので、見え方は本番と揃う
		if (auto* _pDevTool = RefActiveScenePreview(*m_pEngine))
		{
			// 環境設定はツールが選んだもの(黒背景など)。無ければゲームのシーンのものを使う。
			// 毎フレーム流すので、ツール側で切り替えればその場で入れ替わる
			if (const SceneAmbient* _pAmbient = _pDevTool->GetScenePreviewAmbient())
			{
				if (auto* _pGE = m_pEngine->RefGraphicsEngine())
				{
					_pAmbient->Apply(*_pGE, m_ambientDLHandle);
				}
			}
			else
			{
				ApplySceneAmbient();
			}

			_pDevTool->DrawScenePreview();
			return;
		}

		// シーンの環境設定を流し込む。
		// 各シーンの描画より前に置くが、レンダーグラフが回るのはこの後なので順番はどちらでもよい
		ApplySceneAmbient();

		// すべてのシーンを描画(読み込み中のシーンは BaseScene::Draw の中で弾く)
		for (auto& _scene : m_upBaseSceneVec)
		{
			// 命令のスタック
			_scene->Draw();
		}

		// ロード画面は一番上に重ねる
		if (IsLoadingScreenVisible())
		{
			m_upLoadingScreen->Draw();
		}
	}

	void SceneManager::SetWorldFactory(WorldFactory a_factory)
	{
		m_worldFactory = std::move(a_factory);
	}

	std::unique_ptr<Engine::ECS::World> SceneManager::CreateWorld()
	{
		// ここで返せないとシーンがワールドを持てない。
		// 差し込みはアプリ起動時に一度きりなので、起動直後に気付ける
		ENGINE_ERRLOG(m_worldFactory != nullptr, "[SceneManager] ワールドの作り手が差し込まれていません");

		if (!m_worldFactory) return nullptr;

		return m_worldFactory();
	}

	//======================================================================================
	// 空のシーンを作る
	//======================================================================================
	Core::GUID SceneManager::CreateEmptyScene(Resource::AssetDatabase& a_assetDB, const std::string& a_path, const std::string& a_name)
	{
		if (a_name.empty())
		{
			ENGINE_WARNING("[Scene] 名前が空のためシーンを作成できません");
			return Core::GUID();
		}

		//------------------------------------------------------------------
		// 置き場所を決める
		//
		// 「名前を付けて保存」と同じ並びにしておく。
		// シーンごとにフォルダを掘るのは、後から一緒に置きたいものが出てくるため
		//   Asset/Scenes/<サブフォルダ>/<名前>/<名前>.ojscene
		//------------------------------------------------------------------
		std::string _dirPath = "Asset/Scenes/";
		if (!a_path.empty()) _dirPath += a_path + "/";
		_dirPath += a_name;

		const std::string _basePath = _dirPath + "/" + a_name;

		// すでにないかチェック
		const Core::GUID _checkGUID = a_assetDB.GetGUIDFromFilePath(_basePath);
		if (_checkGUID != Core::DEFAULT_GUID)
		{
			ENGINE_WARNING("[Scene] すでに同じ名前のシーンがあります : %s", _basePath.c_str());
			return Core::GUID();
		}

		std::error_code _errorCode = {};
		std::filesystem::create_directories(_dirPath, _errorCode);
		if (_errorCode)
		{
			ENGINE_WARNING("[Scene] フォルダを作成できません : %s", _dirPath.c_str());
			return Core::GUID();
		}

		// アセットデータベースに場所を作る
		const Core::GUID _guid = a_assetDB.AddMetaData(_basePath, "Scene");

		//------------------------------------------------------------------
		// 空の中身を書き出す
		//
		// 開いているシーンには触らない。作るだけで、開くかどうかは呼び出し側が決める
		//------------------------------------------------------------------
		{
			BaseScene _emptyScene;
			_emptyScene.Enter(*m_pEngine);
			_emptyScene.SetGUID(_guid);

			Persistence::Archive _ar(Persistence::Archive::EMode::Save, _dirPath, a_name, "scene");
			_emptyScene.Archive(_ar);

			_emptyScene.Exit();
		}

		ENGINE_LOG("[Scene] 新規作成 : %s", _basePath.c_str());

		return _guid;
	}

	//======================================================================================
	// シーンを作る
	//--------------------------------------------------------------------------------------
	// 初期化(ワールドとオブジェクトマネージャーの用意)と設定ファイルの読み込みまで。
	// 中身の組み立ては読み込み(BaseScene::UpdateLoad)の中で、先読みの後に行う
	//======================================================================================
	std::unique_ptr<BaseScene> SceneManager::CreateScene(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid,
		std::string& a_outFileDir, std::string& a_outFileName)
	{
		const std::string _sceneFilePath = a_resourceManager.GetAssetDatabase().GetFilePathFromGUID(a_guid);
		if (_sceneFilePath.empty())
		{
			ENGINE_ERRLOG(false, "指定されたGUIDのシーンファイルが見つかりません");
			return nullptr;
		}

		// どのシーンを読み込むかをログ出力する
		ENGINE_LOG("[Scene] ロード : %s", _sceneFilePath.c_str());

		auto _upScene = std::make_unique<BaseScene>();
		_upScene->Enter(*m_pEngine);
		_upScene->SetGUID(a_guid);

		a_outFileDir = Core::File::GetDirFromPath(_sceneFilePath);
		a_outFileName = Core::File::GetFileNameWithoutExtension(_sceneFilePath);

		// シーンの設定(先読み一覧・計測フラグ)
		_upScene->RefConfig().LoadFile(a_outFileDir, a_outFileName);

		return _upScene;
	}

	bool SceneManager::PushScene(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid)
	{
		std::string _fileDir = {};
		std::string _fileName = {};
		auto _upScene = CreateScene(a_resourceManager, a_guid, _fileDir, _fileName);
		if (!_upScene) return false;

		//------------------------------------------------------------------
		// 先読み一覧の計測
		//
		// 計測するときは先読みしない : 先読みしたものまで「使った」と数えてしまうため。
		// 計測は組み立てから数え始める(組み立ては読み込みの中なので、ここで始めておけば拾える)
		//------------------------------------------------------------------
		bool _isPreLoad = true;
		if (_upScene->GetConfig().IsRecordPreLoadAssets())
		{
			if (m_pPreLoadRecordingScene == nullptr)
			{
				BeginRecordPreLoadAssets(a_resourceManager, *_upScene);
				_isPreLoad = false;
			}
			else
			{
				ENGINE_WARNING("[Scene] 別のシーンの先読み一覧を計測中のため、このシーンは計測しません : %s",
					_fileName.c_str());
			}
		}

		// 読み込みを始める : 実際に進めるのは Update(UpdateSceneLoad)
		_upScene->BeginLoad(_fileDir, _fileName, _isPreLoad);

		// スタックに積む。読み込みが済むまでは更新も描画もされない
		m_upBaseSceneVec.push_back(std::move(_upScene));

		return true;
	}

	//======================================================================================
	// シーンの読み込みを1歩進める
	//--------------------------------------------------------------------------------------
	// 読み込みの最中は、このシーンのワールドを RefWorld() が返すようにしておく。
	// プレハブのように、コンポーネントのメタ情報を引くためにワールドが要るリソースがある。
	// ロード画面はスタックに乗らないので、これが無いと別のシーンのワールドを掴んでしまう
	//======================================================================================
	void SceneManager::UpdateSceneLoad(Resource::ResourceManager& a_resourceManager, BaseScene& a_scene)
	{
		m_pLoadingScene = &a_scene;
		a_scene.UpdateLoad(a_resourceManager);
		m_pLoadingScene = nullptr;
	}

	//======================================================================================
	// ロード画面
	//======================================================================================
	void SceneManager::SetLoadingScreen(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid)
	{
		// 入れ替えるときは前のものを後始末してから
		if (m_upLoadingScreen)
		{
			m_upLoadingScreen->Exit();
			m_upLoadingScreen.reset();
		}

		if (!a_guid.IsValid()) return;

		std::string _fileDir = {};
		std::string _fileName = {};
		m_upLoadingScreen = CreateScene(a_resourceManager, a_guid, _fileDir, _fileName);
		if (!m_upLoadingScreen) return;

		// ロード画面自身も先読み一覧を持てる(計測はしない)
		m_upLoadingScreen->BeginLoad(_fileDir, _fileName, true);
	}

	bool SceneManager::IsLoading() const
	{
		for (const auto& _upScene : m_upBaseSceneVec)
		{
			if (!_upScene->IsReady()) return true;
		}
		return false;
	}

	float SceneManager::GetLoadProgress() const
	{
		// 読み込み中のうち一番上のもの(普通は積んだばかりの1つだけ)
		for (auto _it = m_upBaseSceneVec.rbegin(); _it != m_upBaseSceneVec.rend(); ++_it)
		{
			if (!(*_it)->IsReady()) return (*_it)->GetLoadProgress();
		}
		return 1.0f;
	}

	bool SceneManager::IsLoadingScreenVisible() const
	{
		return m_upLoadingScreen
			&& m_upLoadingScreen->IsReady()
			&& m_loadingTime >= LOADING_SCREEN_DELAY_SEC;
	}

	//======================================================================================
	// 最前面のシーンを消す
	//--------------------------------------------------------------------------------------
	// 使われなくなったリソースの破棄も当たり判定の空間も全シーンで共有しているので、
	// 片付けてよいのはシーンが1つも残らなくなったときだけ。
	//
	// ポーズ画面のように重ねたシーンを外しただけで片付けてしまうと、
	//   ・後ろのシーンがまだ持っているつもりのものが消える
	//   ・後ろのシーンの静的コライダーが消える(登録は Start の一度きりなので戻らない)
	// といった形で、戻った先が壊れる。
	//
	// リソースの破棄をここで行うのは、参照が外れた瞬間に捨てると
	// 同じシーンの中で出し直すたびに読み直しが走ってしまうため。
	// シーンの中では読み込んだものを持ったままにして、切れ目でまとめて片付ける。
	//======================================================================================
	void SceneManager::PopScene(Resource::ResourceManager& a_resourceManager)
	{
		if (m_upBaseSceneVec.empty()) return;

		// GPU待ち
		if (auto* _pGE = m_pEngine->RefGraphicsEngine())
		{
			_pGE->RefRenderDevice()->WaitForFrame();

			//----------------------------------------------------------------------
			// 消えるワールドのカメラの実行インスタンスを捨てる
			//
			// 残しておくと、次のシーンが同じ描画構成を使うとき
			// (Desert_00 → Desert_02 など)に前のシーンの実行インスタンスが
			// 使い回されたり並んだりして、直接開いたときと違う状態で描かれる。
			// 間に別の描画構成のシーン(Home)を挟むと直るのはこのため。
			// GPUは上で待ったので、グラフのリソースはここで捨ててよい
			//----------------------------------------------------------------------
			if (auto* _pCameraPipelines = _pGE->RefCameraPipelines())
			{
				_pCameraPipelines->ReleaseWorldCameras(m_upBaseSceneVec.back()->RefWorld());
			}
		}

		// これを外すと1つも残らないか
		const bool _isLastScene = (m_upBaseSceneVec.size() == 1);

		// 計測中のシーンを閉じる : ここまでに読んだもので先読み一覧を置き換える
		if (m_upBaseSceneVec.back().get() == m_pPreLoadRecordingScene)
		{
			EndRecordPreLoadAssets(a_resourceManager);
		}

		m_upBaseSceneVec.back()->Exit();
		m_upBaseSceneVec.pop_back();

		//----------------------------------------------------------------------
		// 環境設定を流し込み直す
		//
		// 消えたシーンの空やフォグのノイズのテクスチャは、そのシーンが握っていた。
		// 次の描画まで待つと、下の SweepUnusedAll で捨てられたテクスチャの
		// ハンドルが SceneView に残ったままになるので、ここで残ったシーンのもの
		// (無ければ「無し」)へ差し替える
		//----------------------------------------------------------------------
		ApplySceneAmbient();

		// 後ろに何も残っていなければ、共有しているものをまとめて片付ける
		// (当たり判定の空間はワールドの持ち物なので、上の Exit で一緒に消えている)
		if (_isLastScene)
		{
			//--------------------------------------------------------------
			// 鳴り残っている音を止める
			//
			// 音を借りているものは、消えるときに自分で返す作りになっている
			//   ・コンポーネント … Release フェーズ(SoundFreeSystem など)
			//   ・UI / 進行役    … GameObjectManager の破棄が Release を呼ぶ
			// ただし返し漏れが1つでもあると、鳴っているボイスがプールに残り、
			// 次のシーンへ持ち越して鳴り続ける。ループ再生だと止まらない。
			//
			// シーンが1つも残っていないなら、鳴っていてよい音はもう無い。
			// ここで残りをまとめて止めて、取りこぼしを次のシーンへ持ち込まない。
			//
			// ※ 重ねたシーン(ポーズ)を外しただけのときは通らない。
			//    後ろのゲームで鳴っている音を巻き添えにしないため
			//--------------------------------------------------------------
			if (auto* _pAudioManager = m_pEngine->RefAudioManager()) _pAudioManager->ReleaseInstances();

			// 誰も持っていないリソースはここで破棄する
			a_resourceManager.SweepUnusedAll();
		}
	}
	
	//======================================================================================
	// シーンの切り替え
	//--------------------------------------------------------------------------------------
	// 消してから読み込むので、読み込みに失敗すると「シーンが1つも無い」状態が残る。
	// そうなると現在のシーンを引く先が全部空振りし、以降のフレームで落ちる。
	//
	// 消す前に行き先が引けるかどうかを確かめて、引けなければ今のシーンを残す。
	// (行き先の指定漏れ・GUIDの消滅は設定ミスなので、気付けるように知らせる)
	//======================================================================================
	void SceneManager::ReplaceScene(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid)
	{
		if (m_upBaseSceneVec.empty()) return;

		if (a_resourceManager.GetAssetDatabase().GetFilePathFromGUID(a_guid).empty())
		{
			ENGINE_WARNING("[Scene] 切り替え先のシーンが見つかりません : %s 今のシーンを続けます",
				a_guid.String().c_str());
			return;
		}

		PopScene(a_resourceManager);
		PushScene(a_resourceManager, a_guid);
	}

	Engine::ECS::World* SceneManager::RefWorld()
	{
		// 読み込み中のシーンがあるならそちらが「今のシーン」。
		// まだスタックに乗っていないので、ここで拾わないと引けない
		if (m_pLoadingScene) return m_pLoadingScene->RefWorld();

		if (m_upBaseSceneVec.empty()) return nullptr;

		return m_upBaseSceneVec.back()->RefWorld();
	}

	BaseScene* SceneManager::RefCurrentTopScene()
	{
		// シーンが1つも無い間(起動直後・全消去後)もここは呼ばれる。
		// 空のまま back() を取るとその場で落ちるので、呼び出し側へ nullptr を返す
		if (m_upBaseSceneVec.empty()) return nullptr;

		return m_upBaseSceneVec.back().get();
	}

	GameObject::GameObjectManager* SceneManager::RefGameObjectManager()
	{
		if (m_upBaseSceneVec.empty()) return nullptr;

		return m_upBaseSceneVec.back()->RefGameObjectManager();
	}

	BaseScene* SceneManager::RefAmbientSourceScene()
	{
		// ロード画面は一番上に重なっている
		if (IsLoadingScreenVisible() && m_upLoadingScreen->GetAmbient().IsEnabled()) return m_upLoadingScreen.get();

		// 上から見て、最初に環境設定を使うシーン。読み込み中のシーンはまだ描かれないので飛ばす
		for (auto _it = m_upBaseSceneVec.rbegin(); _it != m_upBaseSceneVec.rend(); ++_it)
		{
			if (!(*_it)->IsReady()) continue;
			if ((*_it)->GetAmbient().IsEnabled()) return _it->get();
		}
		return nullptr;
	}

	//======================================================================================
	// シーンの環境設定を流し込む
	//--------------------------------------------------------------------------------------
	// 使うのは環境設定を使う一番上のシーンだけ。
	// ポーズ画面のように後ろの見た目をそのまま使いたいシーンは、環境設定を切っておけば
	// 後ろのゲームのものが使われ続ける。
	//
	// 1つも無ければ「無し」を流す(平行光の席もここで返す)。
	// 何も流さないと、前のシーンの空や太陽が残り続ける
	//======================================================================================
	void SceneManager::ApplySceneAmbient()
	{
		auto* _pGE = m_pEngine->RefGraphicsEngine();
		if (!_pGE) return;

		if (BaseScene* _pScene = RefAmbientSourceScene())
		{
			_pScene->GetAmbient().Apply(*_pGE, m_ambientDLHandle);
		}
		else
		{
			SceneAmbient::ApplyNone(*_pGE, m_ambientDLHandle);
		}
	}

	//======================================================================================
	// 先読み一覧の計測
	//======================================================================================
	void SceneManager::BeginRecordPreLoadAssets(Resource::ResourceManager& a_resourceManager, BaseScene& a_scene)
	{
		m_pPreLoadRecordingScene = &a_scene;
		a_resourceManager.BeginRecordRequests();

		ENGINE_LOG("[Scene] 先読み一覧の計測を始めます(シーンを閉じるまで)");
	}

	void SceneManager::EndRecordPreLoadAssets(Resource::ResourceManager& a_resourceManager)
	{
		BaseScene* _pScene = m_pPreLoadRecordingScene;
		m_pPreLoadRecordingScene = nullptr;
		if (!_pScene) return;

		std::vector<Core::GUID> _guidVec = a_resourceManager.EndRecordRequests();

		// データベースに無いもの(消えたファイルなど)は先読みしても読めないので外す
		auto& _assetDB = a_resourceManager.RefAssetDatabase();
		std::erase_if(_guidVec, [&_assetDB](const Core::GUID& a_guid) { return !_assetDB.IsValid(a_guid); });

		//------------------------------------------------------------------
		// 一覧を置き換えて、フラグを下ろす
		//
		// 書くのは設定ファイルだけ。シーンファイルには触らない
		// (閉じるときのシーンの中身はプレイで動いた後のものなので)
		//------------------------------------------------------------------
		auto& _config = _pScene->RefConfig();
		_config.SetPreLoadAssetGUIDs(std::move(_guidVec));
		_config.SetRecordPreLoadAssets(false);

		const std::string _path = _assetDB.GetFilePathFromGUID(_pScene->GetGUID());
		if (_path.empty())
		{
			ENGINE_WARNING("[Scene] シーンファイルが見つからないため、計測した先読み一覧を保存できません : %s",
				_pScene->GetGUID().String().c_str());
			return;
		}

		_config.SaveFile(Core::File::GetDirFromPath(_path), Core::File::GetFileNameWithoutExtension(_path));

		ENGINE_LOG("[Scene] 先読み一覧を計測しました : %s (%zu 件)",
			_path.c_str(), _config.GetPreLoadAssetGUIDs().size());
	}

	void SceneManager::ReserveChangeScene(const Core::GUID& a_guid, const ESceneChangeType& a_changeType)
	{
		m_sceneChangeCmd.push({ a_guid,a_changeType });
	}

	void SceneManager::ApplyReservedSceneChanges(Resource::ResourceManager& a_resourceManager)
	{
		// 命令がある間
		while (!m_sceneChangeCmd.empty())
		{
			//----------------------------------------------------------------------
			// 実行前にエディターの選択を捨てる
			//
			// 選択中のエンティティIDもゲームオブジェクトのポインタも、
			// 今のシーンのワールド・オブジェクトマネージャーが持っているもの。
			// Pop/Replace/Clear では実体ごと消え、Push でも参照先のシーンが
			// 変わるため、どの切り替え方でも持ち越してはいけない。
			// (パネル側の検証は描画時にしか回らないので、ここで先に断つ)
			//----------------------------------------------------------------------
			if (auto* _pDevTool = m_pEngine->RefDevTool()) _pDevTool->OnSceneChanged();

			// 命令キューの戦闘要素を処理
			const auto& _cmd = m_sceneChangeCmd.front();
			switch (_cmd.changeType)
			{
			case ESceneChangeType::Push:			// シーンを重ねる
				PushScene(a_resourceManager, _cmd.sceneGUID);
				break;
			case ESceneChangeType::Pop:				// 最上面のシーンを消去
				PopScene(a_resourceManager);
				break;
			case ESceneChangeType::Replace:			// 現在のシーンと切り替える
				ReplaceScene(a_resourceManager, _cmd.sceneGUID);
				break;
			case ESceneChangeType::Clear:			// すべてのシーンを消去
				// 1つずつ Pop に通す。最後の1つを外したところで
				// 共有の当たり判定空間が空になる
				while (!m_upBaseSceneVec.empty())
				{
					PopScene(a_resourceManager);
				}
				break;
			default:
				break;
			}

			// 命令消去
			m_sceneChangeCmd.pop();
		}
	}

	// コンストラクタ・デストラクタ
	SceneManager::SceneManager(MainEngine* a_pEngine)
		: m_pEngine(a_pEngine)
	{}
	SceneManager::~SceneManager()
	{}
}