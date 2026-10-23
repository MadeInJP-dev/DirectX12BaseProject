#include "ResourceManager.h"
#include "Engine/Scene/SceneManager/SceneManager.h"

namespace Engine::Resource
{
	void ResourceManager::Release()
	{

		SweepUnusedAll();

		// 全プール解放。
		// ここで解放し損ねたリソースは MainEngine が壊れるまで生き残ってしまい、
		// それより先に解放される他マネージャー(AudioEngine など)を掴んだまま落ちる。
		// リソース型を追加したらここにも必ず足すこと。
		// フォント : アトラステクスチャを ResourceRef で握っているのでテクスチャより先に捨てる
		ReleaseData<Font>();

		// UIマテリアル : ノイズのテクスチャを ResourceRef で握っているのでテクスチャより先に捨てる
		ReleaseData<UIMaterial>();

		// UIプリセット : 何も握っていない(中身は文字列だけ)
		ReleaseData<UIPreset>();

		ReleaseData<Model>();
		ReleaseData<Material>();
		ReleaseData<Mesh>();
		ReleaseData<AnimationData>();
		ReleaseData<Texture>();
		ReleaseData<Shader>();
		ReleaseData<AnimatorAsset>();
		ReleaseData<ParticlesAsset>();
		ReleaseData<Prefab>();
		ReleaseData<EffectPrefab>();

		// サウンド : DirectX::SoundEffect は AudioEngine を参照しているため、
		// AudioEngine が生きているこのタイミングで必ず解放しきる
		ReleaseData<Sound>();

		// サウンドの流れ : 中身はGUIDと設定値だけだが、
		// ここに足しておかないとプールが静的破棄まで残る
		ReleaseData<AudioBehavior>();

		// エフェクト : 参照しているパーティクル/モデルより後に解放する
		ReleaseData<EffectAsset>();
	}

	//======================================================================================
	// 使われなくなったリソースを片付ける
	//--------------------------------------------------------------------------------------
	// 参照カウントが 0 のものだけを破棄する。数えているのは実際の持ち主
	// (ResourceRef と、ECS側が Acquire で取った参照)だけで、走査で数え直すことはしない。
	//
	// 呼ぶのはシーンの切れ目(SceneManager::PopScene でシーンが1つも残らなくなったとき)。
	// 参照が 0 になった瞬間に捨てないのは、同じシーンの中で出し直すたびに
	// 読み直しが走るのを避けるため(実質シーン内のキャッシュとして残す)。
	//======================================================================================
	void ResourceManager::SweepUnusedAll()
	{
		// エフェクトが参照しているものより先にエフェクトを片付ける。
		// 先に中身を消すと、参照が残っているのに実体が無い状態を挟んでしまう
		SweepUnused<EffectAsset>();
		SweepUnused<Prefab>();
		SweepUnused<EffectPrefab>();
		SweepUnused<ParticlesAsset>();
		SweepUnused<AudioBehavior>();
		SweepUnused<AnimatorAsset>();

		// フォントもアトラステクスチャを ResourceRef で握っているので、テクスチャより先に片付ける
		SweepUnused<Font>();

		// UIマテリアルもノイズのテクスチャを握っているので、テクスチャより先に片付ける
		SweepUnused<UIMaterial>();
		SweepUnused<UIPreset>();

		// モデルは中身(メッシュ・マテリアル・アニメーション)を ResourceRef で握っているので、
		// モデルが消えた後でないと下は 0 にならない
		SweepUnused<Model>();
		SweepUnused<Material>();
		SweepUnused<Mesh>();
		SweepUnused<AnimationData>();
		SweepUnused<Texture>();

		// シェーダーとシェーディングモデルテーブルは使い回すので片付けない
		// (パスの構築時に引くだけで、持ち主が居ない時間帯がある)
	}

	//======================================================================================
	// 種別の文字列から型を引く
	//--------------------------------------------------------------------------------------
	// 種別の文字列は MainEngine がアセットデータベースへ登録しているもの。
	// 種別を足したらここにも足すこと(足さないと先読み一覧に載っても読まれない)。
	//
	// a_func には std::type_identity<T> を渡すので、受け側は
	//   using T = typename decltype(a_tag)::type;
	// で型を取り出す
	//======================================================================================
	template<typename Func>
	bool ResourceManager::VisitAssetType(const Core::GUID& a_guid, Func&& a_func)
	{
		// 実体はデータベースの持ち物で、監視スレッドに消されうるので種別は写しておく
		std::string _type = {};
		if (const AssetProperty* _pProp = m_upAssetDatabase->FindAssetProperty(a_guid))
		{
			_type = _pProp->type;
		}
		if (_type.empty()) return false;

		if (_type == "Model")					{ a_func(std::type_identity<Model>{});					return true; }
		if (_type == "Texture")					{ a_func(std::type_identity<Texture>{});				return true; }
		if (_type == "EffectAsset")				{ a_func(std::type_identity<EffectAsset>{});			return true; }
		if (_type == "Font")					{ a_func(std::type_identity<Font>{});					return true; }
		if (_type == "UIMaterial")				{ a_func(std::type_identity<UIMaterial>{});				return true; }
		if (_type == "UIPreset")				{ a_func(std::type_identity<UIPreset>{});				return true; }
		if (_type == "Mesh")					{ a_func(std::type_identity<Mesh>{});					return true; }
		if (_type == "Material")				{ a_func(std::type_identity<Material>{});				return true; }
		if (_type == "Animation")				{ a_func(std::type_identity<AnimationData>{});			return true; }
		if (_type == "AnimatorAsset")			{ a_func(std::type_identity<AnimatorAsset>{});			return true; }
		if (_type == "ParticlesAsset")			{ a_func(std::type_identity<ParticlesAsset>{});			return true; }
		if (_type == "Shader")					{ a_func(std::type_identity<Shader>{});					return true; }
		if (_type == "Sound")					{ a_func(std::type_identity<Sound>{});					return true; }
		if (_type == "AudioBehavior")			{ a_func(std::type_identity<AudioBehavior>{});			return true; }
		if (_type == "RenderingPipelineAsset")	{ a_func(std::type_identity<Graphics::Pipeline::RenderingPipelineAsset>{}); return true; }
		if (_type == "Prefab")					{ a_func(std::type_identity<Prefab>{});					return true; }
		if (_type == "EffectPrefab")			{ a_func(std::type_identity<EffectPrefab>{});			return true; }

		// シーンなど、このマネージャーが持たない種別
		return false;
	}

	//======================================================================================
	// 型を知らないままの読み込み要求
	//--------------------------------------------------------------------------------------
	// ジョブへ流すのは、ほかの場所でも非同期に読まれている種別だけにしてある。
	// それ以外は今まで呼び出しスレッドでしか読まれてこなかったので、
	// ワーカーから読んで大丈夫かを確かめていない。
	// プレハブはワールドを引きながら読むので、必ず呼び出し元のシーンのワールドを掴ませる
	//======================================================================================
	bool ResourceManager::RequestLoadByGUID(const Core::GUID& a_guid)
	{
		return VisitAssetType(a_guid, [this, &a_guid](auto a_tag)
			{
				using T = typename decltype(a_tag)::type;

				constexpr bool IS_ASYNC =
					std::is_same_v<T, Model> || std::is_same_v<T, Texture> ||
					std::is_same_v<T, EffectAsset> || std::is_same_v<T, Font>;

				if constexpr (IS_ASYNC)
				{
					RequestLoad<T>(a_guid);		// ジョブへ流す : 待たない
				}
				else
				{
					LoadImmediate<T>(a_guid);	// その場で読み切る
				}
			});
	}

	//======================================================================================
	// 型を知らないままの状態の取得
	//======================================================================================
	EResourceState ResourceManager::GetStateByGUID(const Core::GUID& a_guid)
	{
		// 読めない種別・消えたアセットは「もう届かない」ものとして扱う
		EResourceState _state = EResourceState::Failed;
		VisitAssetType(a_guid, [this, &a_guid, &_state](auto a_tag)
			{
				using T = typename decltype(a_tag)::type;
				_state = GetState<T>(a_guid);
			});
		return _state;
	}

	//======================================================================================
	// 読み込み要求の記録
	//======================================================================================
	void ResourceManager::BeginRecordRequests()
	{
		std::lock_guard _lock(m_recordMutex);

		if (m_isRecordingRequests.load(std::memory_order_acquire))
		{
			ENGINE_WARNING("[Resource] 読み込み要求の記録が二重に始められました。前の記録は捨てます");
		}

		m_recordedGUIDVec.clear();
		m_recordedGUIDSet.clear();
		m_isRecordingRequests.store(true, std::memory_order_release);
	}

	std::vector<Core::GUID> ResourceManager::EndRecordRequests()
	{
		std::lock_guard _lock(m_recordMutex);

		m_isRecordingRequests.store(false, std::memory_order_release);

		std::vector<Core::GUID> _result = std::move(m_recordedGUIDVec);
		m_recordedGUIDVec.clear();
		m_recordedGUIDSet.clear();
		return _result;
	}

	void ResourceManager::RecordRequest(const Core::GUID& a_guid)
	{
		std::lock_guard _lock(m_recordMutex);

		// 記録中かを見てからロックを取るまでの間に止められていたら数えない
		if (!m_isRecordingRequests.load(std::memory_order_acquire)) return;

		if (m_recordedGUIDSet.insert(a_guid).second)
		{
			m_recordedGUIDVec.push_back(a_guid);
		}
	}

	void ResourceManager::ReportAssetUsage(const Core::GUID& a_guid)
	{
		if (auto* _pSceneManager = m_pSceneManager.load(std::memory_order_acquire))
		{
			_pSceneManager->RecordUsedAsset(a_guid);
		}
	}

	ResourceManager::ResourceManager()
		: m_upAssetDatabase(std::make_unique<AssetDatabase>())
	{
		ENGINE_ERRLOG(s_pInstance == nullptr, "ResourceManager は1つだけ作ること");
		s_pInstance = this;
		AliveFlag() = true;
	}
	ResourceManager::~ResourceManager()
	{
		// 以降 ResourceRef のデストラクタなどからアクセスされないようにする。
		// メンバ(プール)が壊れるのはこの本体を抜けた後で、そこで中身の ResourceRef が
		// 参照を返しに来るが、生存フラグを見て引き返す
		AliveFlag() = false;
		s_pInstance = nullptr;
	}
}