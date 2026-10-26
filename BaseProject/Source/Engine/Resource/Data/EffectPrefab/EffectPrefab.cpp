#include "EffectPrefab.h"

#include "../../../ECS/World/World.h"
#include "../../../Scene/SceneManager/SceneManager.h"

#include "../../Manager/AssetDatabase/AssetDatabase.h"

namespace Engine::Resource
{
	namespace
	{
		// 拡張子(.oj / .ob の後ろ)。Prefab の "prfb" と分けて、別の型として扱わせる
		constexpr const char* FILE_EXT = "efprfb";
	}

	//======================================================================================
	// 保存 / 読み込み
	//======================================================================================
	void EffectPrefab::Save(ECS::World* a_pWorld, const std::string& a_savePath)
	{
		// ワールドが無いとコンポーネント名が引けず、中身の無いものを書き出してしまう。
		// 書き出し先は既存のアセットなので、それは中身の消去になる(Prefab と同じ理由)
		if (!a_pWorld)
		{
			ENGINE_WARNING("[EffectPrefab] ワールドが無いので保存しません(中身が消えるため) : %s", a_savePath.c_str());
			return;
		}

		auto _dir = Core::File::GetDirFromPath(a_savePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_savePath);

		if (!_dir.empty())
		{
			std::error_code _ec;
			std::filesystem::create_directories(_dir, _ec);
		}

		Persistence::Archive _arch(Persistence::Archive::EMode::Save, _dir, _fileName, FILE_EXT);
		Archive(_arch, a_pWorld);
	}

	void EffectPrefab::Load(ECS::World* a_pWorld, const std::string& a_filePath)
	{
		auto _dir = Core::File::GetDirFromPath(a_filePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_filePath);

		// 形式は起動設定任せ(JSON を読む設定なら .oj があればそちらを読む)
		Persistence::Archive _arch(Persistence::Archive::EMode::Load, _dir, _fileName, FILE_EXT);
		Archive(_arch, a_pWorld);
	}

	//======================================================================================
	// シリアライズ
	//--------------------------------------------------------------------------------------
	// 寿命を先頭に置き、残りは Prefab と同じ並び(同じ階層)で書く。
	// 手で書き足すときも、プレハブのファイルに LifeTime を1行足した形で読める
	//======================================================================================
	void EffectPrefab::Archive(Persistence::Archive& a_ar, ECS::World* a_pWorld)
	{
		a_ar.Field("LifeTime", m_lifeTime);
		if (a_ar.GetMode() == Persistence::Archive::EMode::Load)
		{
			m_lifeTime = (std::max)(m_lifeTime, MIN_LIFE_TIME);
		}

		m_prefab.Archive(a_ar, a_pWorld);
	}

	//======================================================================================
	// ローダー / 生成
	//======================================================================================
	EffectPrefab EffectPrefab::LoadFromFile(const std::string& a_path, const ResourceBuildContext* a_pContext)
	{
		EffectPrefab _effectPrefab;

		// コンポーネントのメタ情報が要るので World を借りる(Prefab と同じ)
		auto* _pSceneManager = a_pContext ? a_pContext->pSceneManager : nullptr;
		ECS::World* _pWorld = _pSceneManager ? _pSceneManager->RefWorld() : nullptr;
		if (_pWorld && _pWorld->IsInit())
		{
			_effectPrefab.Load(_pWorld, a_path);
			return _effectPrefab;
		}

		// 空のまま返すと、実体化しても何も出ず、保存するとアセットが消える。黙らずに知らせる
		ENGINE_WARNING("[EffectPrefab] ワールドが無いので読み込めませんでした : %s", a_path.c_str());
		return _effectPrefab;
	}

	void EffectPrefab::Create(AssetDatabase& a_assetDB, ECS::World* a_pWorld, const std::string& a_path, const std::string& a_name)
	{
		static const std::string ASSET_DIR = "Asset/EffectPrefab/";
		auto _basePath = ASSET_DIR + a_path + "/" + a_name;

		// すでに存在するなら作らない
		if (a_assetDB.GetGUIDFromFilePath(_basePath) != Core::DEFAULT_GUID)
		{
			ENGINE_LOG("すでに作成されたエフェクトプレハブです : %s", _basePath.c_str());
			return;
		}

		// 書き出すだけでよい。メタファイルとGUIDは AssetDatabase の監視が用意する
		EffectPrefab _effectPrefab;
		_effectPrefab.Save(a_pWorld, _basePath);
	}
}
