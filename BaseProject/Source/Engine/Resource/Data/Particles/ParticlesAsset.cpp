#include "ParticlesAsset.h"

#include "../../Manager/AssetDatabase/AssetDatabase.h"
#include "../../Manager/ResourceManager/ResourceManager.h"

namespace Engine::Resource
{
	void Engine::Resource::ParticlesAsset::Create(const std::string& a_name)
	{
		m_name = a_name;
	}
	void ParticlesAsset::Release()
	{
		m_name = "";
		m_guid = {};
		m_texGUID = {};
	}
	//======================================================================================
	// 保存と読み込みで共通の項目並び
	//--------------------------------------------------------------------------------------
	// ※ 追加は末尾に。バイナリは順次読みなので途中に挿すと既存データが全部ずれる
	//======================================================================================
	void ParticlesAsset::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("m_name", m_name);
		a_ar.Field("m_guid", m_guid);
		a_ar.Field("m_texGUID", m_texGUID);
		a_ar.Field("m_initialSpeedMin", m_initialSpeedMin);
		a_ar.Field("m_initialSpeedMax", m_initialSpeedMax);
		a_ar.Field("m_gravityPow", m_gravityPow);
		a_ar.Field("m_ligeTimeMin", m_lifeTimeMin);
		a_ar.Field("m_ligeTimeMax", m_lifeTimeMax);
		a_ar.Field("m_capacity", m_capacity);
		a_ar.Field("m_emissionRate", m_emissionRate);

		a_ar.Field("m_orientation", m_orientation);
		a_ar.Field("m_stretch", m_stretch);

		// ---- 寿命に沿った変化 ----
		a_ar.Field("m_drag", m_drag);
		a_ar.Field("m_endSizeScale", m_endSizeScale);
		a_ar.Field("m_startColor", m_startColor);
		a_ar.Field("m_endColor", m_endColor);
		a_ar.Field("m_fadeInRatio", m_fadeInRatio);
		a_ar.Field("m_fadeOutRatio", m_fadeOutRatio);

		a_ar.Field("m_blendMode", m_blendMode);
		a_ar.Field("m_simulationSpace", m_simulationSpace);

		// ---- 板の回転 ----
		a_ar.Field("m_rotationMin", m_rotationMin);
		a_ar.Field("m_rotationMax", m_rotationMax);
		a_ar.Field("m_angularVelocityMin", m_angularVelocityMin);
		a_ar.Field("m_angularVelocityMax", m_angularVelocityMax);

		// ---- 描く順 ----
		a_ar.Field("m_sortOrder", m_sortOrder);

		// ---- 奥の物との境目 ----
		a_ar.Field("m_softDepth", m_softDepth);

		// ---- 寿命の途中の色・サイズの曲がり具合 ----
		a_ar.Field("m_midColor", m_midColor);
		a_ar.Field("m_midColorTime", m_midColorTime);
		a_ar.Field("m_sizeCurvePow", m_sizeCurvePow);

		// ---- フリップブック ----
		a_ar.Field("m_flipbookCols", m_flipbookCols);
		a_ar.Field("m_flipbookRows", m_flipbookRows);
		a_ar.Field("m_flipbookMode", m_flipbookMode);
		a_ar.Field("m_isFlipbookBlend", m_isFlipbookBlend);

		// ---- 絵のチャンネルの読み方 ----
		a_ar.Field("m_texMode", m_texMode);
		a_ar.Field("m_emissiveTime", m_emissiveTime);
		a_ar.Field("m_emissiveColor", m_emissiveColor);
	}

	//======================================================================================
	// 読み込み後の後始末
	//======================================================================================
	void ParticlesAsset::OnLoaded(ResourceManager& a_resourceManager)
	{
		// キャパシティが0だとリソース生成ができないので最低値を入れておく
		if (m_capacity == 0)
		{
			m_capacity = 1000;
		}

		// 伸ばし倍率が0だと板が潰れて見えなくなるので最低値を入れておく
		if (m_stretch <= 0.0f)
		{
			m_stretch = 1.0f;
		}

		// サイズ倍率が0だと寿命の終わりで完全に消える。
		// 「消える前に縮む」演出としては有りなので0は許すが、負は板が裏返るので止める
		if (m_endSizeScale < 0.0f)
		{
			m_endSizeScale = 0.0f;
		}

		// フェードは寿命に対する割合。合計が1を超えると途中が出ないままになるので詰める
		m_fadeInRatio = std::clamp(m_fadeInRatio, 0.0f, 1.0f);
		m_fadeOutRatio = std::clamp(m_fadeOutRatio, 0.0f, 1.0f);
		if (m_fadeInRatio + m_fadeOutRatio > 1.0f)
		{
			m_fadeOutRatio = 1.0f - m_fadeInRatio;
		}

		// 減衰が負だと加速して発散する
		if (m_drag < 0.0f) m_drag = 0.0f;

		// ぼかす幅が負だと境目の手前まで透明になる
		if (m_softDepth < 0.0f) m_softDepth = 0.0f;

		// 途中の色の進み具合は 0〜1 の内側だけ意味がある(端は2色と同じ)
		m_midColorTime = std::clamp(m_midColorTime, 0.0f, 1.0f);

		// 曲がり具合が 0 以下だと、生まれた瞬間に終わりの大きさへ跳ぶ
		if (m_sizeCurvePow <= 0.0f) m_sizeCurvePow = 1.0f;

		// コマ数は 1 以上。多すぎると 1コマが潰れるので頭打ちにする
		m_flipbookCols = std::clamp(m_flipbookCols, 1, 64);
		m_flipbookRows = std::clamp(m_flipbookRows, 1, 64);

		if (m_emissiveTime < 0.0f) m_emissiveTime = 0.0f;

		// テクスチャのハンドル取得
		m_texHandle = a_resourceManager.LoadImmediate<Texture>(m_texGUID);
	}

	void ParticlesAsset::Save(const std::string& a_filePath)
	{
		auto _fileDir = Core::File::GetDirFromPath(a_filePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_filePath);
		Persistence::Archive _archi(Persistence::Archive::EMode::Save, _fileDir, _fileName, "ptic");

		Archive(_archi);
	}

	void ParticlesAsset::Load(const std::string& a_fileDir, const std::string& a_fileName, ResourceManager& a_resourceManager)
	{
		Persistence::Archive _archi(Persistence::Archive::EMode::Load, a_fileDir, a_fileName, "ptic");

		Archive(_archi);
		OnLoaded(a_resourceManager);
	}

	void ParticlesAsset::Load(const std::string& a_filePath, ResourceManager& a_resourceManager)
	{
		auto _fileDir = Core::File::GetDirFromPath(a_filePath);
		auto _fileName = Core::File::GetFileNameWithoutExtension(a_filePath);

		Load(_fileDir, _fileName, a_resourceManager);
	}
}
