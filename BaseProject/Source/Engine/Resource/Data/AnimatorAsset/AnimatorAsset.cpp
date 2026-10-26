#include "AnimatorAsset.h"

#include "../../Manager/AssetDatabase/AssetDatabase.h"
#include "../../Manager/ResourceManager/ResourceManager.h"

namespace Engine::Resource
{
	//======================================================================================
	// ノードのArchive
	//======================================================================================
	void AnimatorNode::Archive(Persistence::Archive& a_arch)
	{
		// 共通の「つなぎ情報」(名前・座標・各種ID)
		ArchiveTopology(a_arch);

		// Animator固有: 再生アニメ情報
		a_arch.Field("animGUID", animGUID);
		a_arch.Field("speed", speed);
		a_arch.Field("isLoop", isLoop);
		a_arch.Field("additiveWeight", additiveWeight);
	}

	//======================================================================================
	// 加算ポーズ用ボーン定義のArchive
	//======================================================================================
	void AdditiveBoneDef::Archive(Persistence::Archive& a_arch)
	{
		a_arch.StringField("nodeName", nodeName);
		a_arch.Field("share", share);
		a_arch.Field("axisScale", axisScale);
		a_arch.Field("channel", channel);

		// ハッシュは保存せず、名前から張り直す(モデル差し替えに強くするため)
		if (a_arch.IsLoading())
		{
			nodeNameHash = Core::String::ToHash(nodeName);
		}
	}

	//======================================================================================
	// 保存
	//======================================================================================
	void AnimatorAsset::Save(const std::string& a_savePath, const ResourceManager& a_resourceManager)
	{
		// 保存直前: 各ノードの再生アニメ参照(playAnimData)からGUIDを取り出しておく
		auto* _pModel = a_resourceManager.Get(m_modelHandle);
		if (_pModel)
		{
			for (auto& [_hash, _node] : m_graph.Nodes())
			{
				_node.animGUID = _pModel->GetAnimationGUIDFromHandle(_node.playAnimData);
			}
		}

		auto _dir = Core::File::GetDirFromPath(a_savePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_savePath);
		Persistence::Archive _arch(Persistence::Archive::EMode::Save, _dir, _fileName, "stet");

		// Animator固有ヘッダ
		_arch.Field("m_name", m_name);
		_arch.Field("m_modelGUID", m_modelGUID);

		// グラフ本体(ノード・矢印・パラメータ・既定開始)
		m_graph.SaveGraph(_arch);

		// 加算ポーズのボーン定義
		ArchiveAdditiveBones(_arch);
	}

	//======================================================================================
	// 加算ポーズ用ボーン定義のシリアライズ
	//======================================================================================
	void AnimatorAsset::ArchiveAdditiveBones(Persistence::Archive& a_arch)
	{
		size_t _size = m_additiveBones.size();
		if (a_arch.BeginArray("AdditiveBones", _size))
		{
			// ロード時は読み取った要素数に合わせる
			if (a_arch.IsLoading()) m_additiveBones.resize(_size);

			for (size_t _i = 0; _i < _size; ++_i)
			{
				if (a_arch.BeginObject(_i))
				{
					m_additiveBones[_i].Archive(a_arch);
					a_arch.EndObject();
				}
			}
			a_arch.EndArray();
		}
		else if (a_arch.IsLoading())
		{
			// 加算ポーズ導入前に保存されたアセットにはこの項目が無い
			m_additiveBones.clear();
		}
	}

	//======================================================================================
	// 読み込み
	//======================================================================================
	void AnimatorAsset::Load(const std::string& a_fileDir, const std::string& a_fileName, ResourceManager& a_resourceManager)
	{
		LoadInternal(a_fileDir, a_fileName, a_resourceManager);
	}

	void AnimatorAsset::Load(const std::string& a_filePath, ResourceManager& a_resourceManager)
	{
		auto _dir = Core::File::GetDirFromPath(a_filePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_filePath);
		LoadInternal(_dir, _fileName, a_resourceManager);
	}

	void AnimatorAsset::LoadInternal(const std::string& a_fileDir, const std::string& a_fileName, ResourceManager& a_resourceManager)
	{
		Release();

		// 形式は起動設定任せ(Auto)。JSON を読む設定なら .ojstet 優先、読まない設定(提出用)は .obstet のみ
		Persistence::Archive _arch(
			Persistence::Archive::EMode::Load, a_fileDir, a_fileName, "stet");

		// Animator固有ヘッダ
		_arch.Field("m_name", m_name);
		_arch.Field("m_modelGUID", m_modelGUID);
		m_modelHandle = a_resourceManager.LoadImmediate<Model>(m_modelGUID);

		// グラフ本体
		m_graph.LoadGraph(_arch);

		// 加算ポーズのボーン定義
		ArchiveAdditiveBones(_arch);

		// モデルから各ノードの再生アニメ参照を復元(GUID → ハンドル)
		auto* _pModel = a_resourceManager.Get(m_modelHandle);
		if (_pModel)
		{
			for (auto& [_hash, _node] : m_graph.Nodes())
			{
				_node.playAnimData = _pModel->GetAnimationHandleFromGUID(_node.animGUID);
			}
		}

		// 復元した座標はエディターが読み込み回数の変化を見て反映する
		++m_loadCount;
	}

	void AnimatorAsset::Release()
	{
		m_graph.Clear();
		m_name.clear();
		m_modelGUID = Core::DEFAULT_GUID;
		m_modelHandle = {};
		m_additiveBones.clear();
	}
}
