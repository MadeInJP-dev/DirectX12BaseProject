#pragma once

namespace Engine::Audio
{
	/// <summary>
	/// 3Dサウンドの「聞き手」の情報。
	/// 誰がリスナーなのかはエンジンは知らないので、持っている側(プレイヤー等)が
	/// 毎フレーム SubmitListener で送り込む。
	/// </summary>
	struct ListenerData
	{
		Math::Vector3 pos      = { 0.0f, 0.0f, 0.0f };	// ワールド座標
		Math::Vector3 front    = { 0.0f, 0.0f, 1.0f };	// 前方 : このエンジンは左手系で +Z が前
		Math::Vector3 up       = { 0.0f, 1.0f, 0.0f };	// 上方向
		Math::Vector3 velocity = { 0.0f, 0.0f, 0.0f };	// 速度(m/秒)。ドップラーに使う
	};

	/// <summary>
	/// 音関係を扱うクラス、サウンドはここで読込要求が来るとロードしてインスタンスのみを返す
	/// </summary>
	/// <remarks>
	/// 実体は MainEngine が持ち、EngineServices::pAudioManager で配る。
	/// 発行したサウンドインスタンスには自分を渡すので、インスタンスの側からここを探しには来ない
	/// </remarks>
	class AudioManager
	{
	public:

		AudioManager();
		~AudioManager();
		NON_COPYABLE_NON_MOVABLE(AudioManager);

		//----------------------------------------------------------------------------------------------------
		// 初期化・解放
		//----------------------------------------------------------------------------------------------------
		// a_pResourceManager : サウンドの実体を引く先(借り物)
		bool Init(Resource::ResourceManager* a_pResourceManager);

		/// <summary>
		/// 発行済みのサウンドインスタンスをすべて停止・破棄する
		/// DirectX::SoundEffectInstance は生成元の SoundEffect と AudioEngine を
		/// 参照しているため、それらより先にここで破棄しきる必要がある
		/// </summary>
		void ReleaseInstances();

		/// <summary>
		/// 全解放 : サウンドインスタンス → オーディオエンジン の順で破棄する
		/// </summary>
		void Release();

		//----------------------------------------------------------------------------------------------------
		// 更新
		//----------------------------------------------------------------------------------------------------

		/// <summary>
		/// 毎フレーム呼ぶこと
		/// 再生し終わったワンショットボイスの回収と、
		/// オーディオデバイスのロスト検出・復帰をここで行う
		/// </summary>
		void Update();

		//----------------------------------------------------------------------------------------------------
		// アクセサ
		//----------------------------------------------------------------------------------------------------
		DirectX::AudioEngine* RefAudioEngine() { return m_upAudioEngine.get(); }
		DirectX::AudioListener& RefAudioListner() { return m_listener; }

		//----------------------------------------------------------------------------------------------------
		// リスナー
		//----------------------------------------------------------------------------------------------------
		/// <summary>
		/// 聞き手の情報を更新する。リスナーを持つ側から毎フレーム呼ぶこと。
		/// 3D再生(Apply3D)は必ずここで設定された最新のリスナーを見る。
		/// 送らない間は初期値(原点で +Z 向き)のままなので、定位がおかしくなる。
		/// </summary>
		void SubmitListener(const ListenerData& a_data);

		const Resource::SoundInstance* GetInstance(const Handle<Resource::SoundInstance>& a_handle) const;
		Resource::SoundInstance* RefInstance(const Handle<Resource::SoundInstance>& a_handle);

		//----------------------------------------------------------------------------------------------------
		// サウンドインスタンスの発行
		//----------------------------------------------------------------------------------------------------
		/// <param name="a_is3D">
		/// true で3Dサウンド用インスタンスを発行する。
		/// Play3D / SetPos / Apply3D はこれを true にしたインスタンスでしか使えない
		/// </param>
		/// <param name="a_group">
		/// 音のグループ。シーンとプレイヤーの音量はこの単位で掛かる。
		/// 省略すると効果音(Se)扱い
		/// </param>
		// GUIDから発行する。
		// (ファイルパス版はどこからも呼ばれておらず、アセットデータベースを直に引いていたので消した)
		Handle<Resource::SoundInstance> CreateSoundInstance(
			const Core::GUID& a_guid, bool a_is3D = false,
			ESoundGroup a_group = ESoundGroup::Se);

		//----------------------------------------------------------------------------------
		// シーンの音量(音量の席)
		//
		// 作り手が決める音量のつり合いはシーンが持つ(Scene::SceneAudio)。
		// シーンは開くときに席を1つ取って音量を預け、閉じるときに返す。
		//
		// 音は発行した瞬間の「今の席」(VolumeScopeGuard で張る)を札として覚え、
		// 以降はその席の音量で鳴る。重ねたシーン(ポーズ画面)の音と後ろのゲームの音が
		// それぞれ自分のシーンの音量で鳴るのはこのため。
		//
		// 席の外で発行した音(エディターの試聴など)と、席を返した後に残った音は素通し(1.0)
		//----------------------------------------------------------------------------------

		// 席を取る。返った番号はシーンが閉じるまで持っておく
		VolumeScopeID AddVolumeScope(const SoundVolume& a_volume);

		/// <summary>
		/// 席の音量を差し替える
		/// </summary>
		/// <remarks>
		/// 変えた瞬間に鳴っているもの全部へ送り直す。
		/// 更新が止まっているシーンの音(ポーズ中のゲームBGMなど)にも効かせるため
		/// </remarks>
		void SetVolumeScope(VolumeScopeID a_id, const SoundVolume& a_volume);

		// 席を返す。その席の札が付いた音は以降素通しになる(普通はシーンと一緒に返却済み)
		void RemoveVolumeScope(VolumeScopeID a_id);

		// 今の席 : これから発行する音に付く札。張り替えは VolumeScopeGuard で行う
		VolumeScopeID GetCurrentVolumeScope() const { return m_currentVolumeScopeID; }
		void SetCurrentVolumeScope(VolumeScopeID a_id) { m_currentVolumeScopeID = a_id; }

		//----------------------------------------------------------------------------------
		// プレイヤーの音量(マスター・グループ)
		//----------------------------------------------------------------------------------

		/// <summary>
		/// プレイヤーの音量 : シーンの音量へさらに掛かる
		/// </summary>
		/// <remarks>
		/// シーンの音量は作り手が決めるつり合いで、
		/// こちらはプレイヤーが設定画面で上下するもの(アプリのユーザーデータ)。
		/// 分けて持つので、プレイヤーが下げても作り手のつり合いは崩れない。
		/// 変えた瞬間に鳴っているもの全部へ送り直す
		/// </remarks>
		void SetUserMasterVolume(float a_volume);
		float GetUserMasterVolume() const { return m_userMasterVolume; }
		void SetUserGroupVolume(ESoundGroup a_group, float a_volume);
		float GetUserGroupVolume(ESoundGroup a_group) const;

		/// <summary>
		/// そのグループの音へ掛ける倍率(シーンの音量 × プレイヤーの音量)
		/// </summary>
		/// <remarks>SoundInstance が実際に送る音量を出すのに使う</remarks>
		float CalcVolumeScale(ESoundGroup a_group, VolumeScopeID a_scopeID) const;

		/// <summary>
		/// 発行したサウンドインスタンスを停止して破棄する
		/// プールはアプリ寿命なので、発行した側(コンポーネント等)が必ず返却すること
		/// </summary>
		/// <param name="a_handle">CreateSoundInstance が返したハンドル : 無効なら何もしない</param>
		void ReleaseSoundInstance(const Handle<Resource::SoundInstance>& a_handle);

		//----------------------------------------------------------------------------------
		// 同じ音を鳴らしすぎないための関所
		//
		// 一発もののエフェクト(被弾・爆発など)は鳴らすたびに別のエンティティになるので、
		// エンティティ側の待ち時間では間引けない。同じ音(GUID)ごとに、最後に鳴らした時刻と
		// いま鳴っている声をここで覚えて間引く。
		// 使い方 : CanPlaySound が true なら鳴らし、鳴らしたら NotifySoundPlayed で知らせる
		//----------------------------------------------------------------------------------

		/// <param name=a_minInterval>前回鳴らしてからこの秒数が経つまでは鳴らさない(0 で制限なし)</param>
		/// <param name=a_maxConcurrent>同時に鳴っている数の上限(0 で制限なし)</param>
		/// <returns>鳴らしてよいか</returns>
		bool CanPlaySound(const Core::GUID& a_guid, float a_minInterval, uint32_t a_maxConcurrent);

		// 鳴らしたことを記録する(最後に鳴らした時刻と、鳴っている声)
		void NotifySoundPlayed(const Core::GUID& a_guid, const Handle<Resource::SoundInstance>& a_handle);

	private:

		// 鳴っているものすべてへ音量を送り直す
		void RefreshAllVolume();

	private:

		// ---- メンバの宣言順が破棄順を決めるので入れ替えないこと ----
		// 破棄は宣言の逆順。SoundInstance が AudioEngine を参照しているため、
		// オーディオエンジンは必ず一番上(= 最後に破棄される位置)に置く。

		// オーディオエンジン
		std::unique_ptr<DirectX::AudioEngine> m_upAudioEngine = nullptr;

		// 3Dサウンドリスナー
		DirectX::AudioListener m_listener;

		// 現在再生中のサウンド管理リスト
		Pool::ItemPool<Resource::SoundInstance> m_soundInstancePool;

		//----------------------------------------------------------------------------------
		// シーンの音量(席)
		//
		// 保存はシーン側(SceneAudio)。ここは実行中の値を預かるだけ
		//----------------------------------------------------------------------------------
		std::unordered_map<VolumeScopeID, SoundVolume> m_volumeScopeMap;
		VolumeScopeID m_nextVolumeScopeID = INVALID_VOLUME_SCOPE_ID + 1;

		// 今の席 : 発行する音に付ける札
		VolumeScopeID m_currentVolumeScopeID = INVALID_VOLUME_SCOPE_ID;

		// プレイヤーの音量。保存はアプリ側(ユーザーデータ)で、起動時と設定画面から流し込まれる
		float m_userMasterVolume = 1.0f;
		std::array<float, SOUND_GROUP_COUNT> m_userGroupVolumeArray = {};

		// サウンドの実体を引く先(借り物) : 持ち主は MainEngine
		Resource::ResourceManager* m_pResourceManager = nullptr;

		// 同じ音ごとの関所(CanPlaySound / NotifySoundPlayed)
		struct SoundPlayGate
		{
			std::chrono::steady_clock::time_point lastPlayTime = {};
			bool hasPlayed = false;
			std::vector<Handle<Resource::SoundInstance>> playingHandles;	// 鳴らした声(鳴り終わったものは見るときに外す)
		};
		std::unordered_map<Core::GUID, SoundPlayGate> m_playGates;
	};

	//======================================================================================
	// 今の席を張り替えて、スコープを抜けるときに戻す
	//
	//     Audio::VolumeScopeGuard _guard(pAudioManager, sceneScopeID);
	//     world.Update();		// ここで発行した音はそのシーンの音量で鳴る
	//
	// 入れ子にしてよい(抜けるときは張る前の席へ戻す)。AudioManager が無ければ何もしない
	//======================================================================================
	class VolumeScopeGuard
	{
	public:

		VolumeScopeGuard(AudioManager* a_pAudioManager, VolumeScopeID a_id)
			: m_pAudioManager(a_pAudioManager)
		{
			if (!m_pAudioManager) return;

			m_prevID = m_pAudioManager->GetCurrentVolumeScope();
			m_pAudioManager->SetCurrentVolumeScope(a_id);
		}
		~VolumeScopeGuard()
		{
			if (m_pAudioManager) m_pAudioManager->SetCurrentVolumeScope(m_prevID);
		}
		NON_COPYABLE_NON_MOVABLE(VolumeScopeGuard);

	private:

		AudioManager* m_pAudioManager = nullptr;
		VolumeScopeID m_prevID = INVALID_VOLUME_SCOPE_ID;
	};
}
