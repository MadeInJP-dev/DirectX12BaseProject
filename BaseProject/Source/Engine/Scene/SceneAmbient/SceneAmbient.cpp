#include "SceneAmbient.h"

#include "Engine/ECS/System/SystemContext.h"	// EngineServices
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/EditorField/EditorField.h"

namespace Engine::Scene
{
	namespace
	{
		// GUID からテクスチャの読み込みを始める。未設定なら空のハンドル
		ResourceRef<Resource::Texture> RequestTexture(Resource::ResourceManager& a_resourceManager, const Core::GUID& a_guid)
		{
			if (!a_guid.IsValid()) return {};
			return a_resourceManager.RequestLoad<Resource::Texture>(a_guid);
		}

		// CB では int で持っている有効/無効を、チェックボックスで触る
		bool EnableField(const char* a_label, int& a_enable)
		{
			bool _isEnable = (a_enable != 0);
			if (!Engine::EditorField::Field(a_label, _isEnable)) return false;

			a_enable = _isEnable ? 1 : 0;
			return true;
		}
	}

	SceneAmbient::SceneAmbient()
	{
		// 置いた瞬間に真っ暗になると「壊れている」ように見えるため、平行光だけは入れておく
		// (AmbientData の素の既定値は環境光 0)
		m_ambient.ambientColorScale = { 0.0f, 0.0f, 0.0f };
		m_dlDir                     = { 0.5f, -1.0f, 0.5f };
		m_dlColor                   = { 4.0f, 4.0f, 4.0f };
	}

	SceneAmbient SceneAmbient::CreatePlain()
	{
		SceneAmbient _ambient = {};

		// メッシュのパーツが影側で真っ黒に潰れないよう、環境光を少し入れる
		_ambient.m_ambient.ambientColorScale = { 0.4f, 0.4f, 0.4f };

		// フォグの CB は既定値のままだと濃さが入っている(シーンで使う既定のため)。ここで明示的に落とす
		_ambient.m_ambient.heightFogEnable   = 0;
		_ambient.m_ambient.distanceFogEnable = 0;
		_ambient.m_sceneFog.density          = 0.0f;
		_ambient.m_groundDust.density        = 0.0f;
		_ambient.m_fogComposite.enable       = 0;

		// 空はテクスチャを持たせないので描かれない(GUID も実体も空のまま)
		return _ambient;
	}

	//======================================================================================
	// 保存・読み込み
	//
	// シーンファイルの "Ambient" グループの中身。
	// バイナリは順番に読むので、後から足すものは末尾へ置くこと
	//======================================================================================
	void SceneAmbient::Archive(Persistence::Archive& a_ar)
	{
		a_ar.Field("IsEnabled", m_isEnabled);

		// ---- 環境光・平行光 ----
		a_ar.Field("AmbientColor", m_ambient.ambientColorScale);
		a_ar.Field("DLDir", m_dlDir);
		a_ar.Field("DLColor", m_dlColor);

		// ---- 平行光の影 ----
		a_ar.Field("ShadowMode", m_shadow.mode);
		a_ar.Field("ShadowDistance", m_shadow.distance);
		a_ar.Field("ShadowCascadeCount", m_shadow.cascadeCount);
		a_ar.Field("ShadowSplitLambda", m_shadow.splitLambda);
		a_ar.Field("ShadowDepthBias", m_shadow.depthBias);
		a_ar.Field("ShadowNormalBias", m_shadow.normalBias);
		a_ar.Field("ShadowSoftness", m_shadow.softness);
		a_ar.Field("ShadowCasterDistance", m_shadow.casterDistance);

		// ---- 高さフォグ ----
		a_ar.Field("HeightFogColor", m_ambient.heightFogColor);
		a_ar.Field("HeightFogMaxRange", m_ambient.heightFogMaxRange);
		a_ar.Field("HeightFogHeight", m_ambient.heightFogHeight);
		a_ar.Field("HeightFogEnable", m_ambient.heightFogEnable);
		a_ar.Field("HeightFogDenseDown", m_ambient.heightFogDenseDown);

		// ---- 距離フォグ ----
		a_ar.Field("DistanceFogColor", m_ambient.distanceFogColor);
		a_ar.Field("DistanceFogMaxRange", m_ambient.distanceFogMaxRange);
		a_ar.Field("DistanceFogStart", m_ambient.distanceFogStart);
		a_ar.Field("DistanceFogEnable", m_ambient.distanceFogEnable);

		// ---- ボリュメトリックフォグ ----
		a_ar.Field("SceneFogColor", m_sceneFog.fogColor);
		a_ar.Field("SceneFogDensity", m_sceneFog.density);
		a_ar.Field("SceneFogMaxDistance", m_sceneFog.maxDistance);

		a_ar.Field("DustColor", m_groundDust.dustColor);
		a_ar.Field("DustDensity", m_groundDust.density);
		a_ar.Field("DustHeight", m_groundDust.height);
		a_ar.Field("DustNoiseScale", m_groundDust.noiseScale);
		a_ar.Field("DustStepSize", m_groundDust.stepSize);
		a_ar.GUIDField("FogNoiseTexGUID", m_fogNoiseTexGUID);

		a_ar.Field("FogCompositeIntensity", m_fogComposite.intensity);
		a_ar.Field("FogCompositeEnable", m_fogComposite.enable);

		// ---- 空 ----
		a_ar.GUIDField("SkyTexGUID", m_skyTexGUID);
		a_ar.Field("SkyExposure", m_sky.exposure);
		a_ar.Field("SkyHorizonHeight", m_sky.horizonHeight);
		a_ar.Field("SkyRadius", m_sky.radius);
		a_ar.Field("SkyRotationDeg", m_sky.rotationDeg);
		a_ar.Field("IsSkyDof", m_sky.isSkyDof);
		a_ar.Field("SkyDofScale", m_sky.dofScale);

		// ---- 後から足したもの(バイナリは順番に読むので、ここより上へ移さないこと) ----
		a_ar.Field("SceneFogAnisotropy", m_sceneFog.anisotropy);
		a_ar.Field("SceneFogLightScale", m_sceneFog.lightScale);
	}

	void SceneAmbient::RequestLoadAssets(Resource::ResourceManager& a_resourceManager)
	{
		m_skyTexRef = RequestTexture(a_resourceManager, m_skyTexGUID);
		m_fogNoiseTexRef = RequestTexture(a_resourceManager, m_fogNoiseTexGUID);
	}

	//======================================================================================
	// GraphicsEngine へ流し込む
	//
	// 送るだけで、どう使うかは各レンダーパスの担当。
	//   AmbientData            … ディファードライティング(環境光・高さ/距離フォグ)
	//   SkyData ＋ テクスチャ  … スカイパス / CoC(空のボケ)
	//   フォグ・ダスト・合成   … SceneVolumetricFogPass / SceneFogCompositePass
	//   平行光・影の設定       … LightManager(影 / GI / ディファード)
	//======================================================================================
	void SceneAmbient::Apply(Graphics::GraphicsEngine& a_ge, Handle<Graphics::DirectionalLight>& a_inoutDLHandle) const
	{
		auto* _pView = a_ge.RefSceneView();
		_pView->SetAmbientData(m_ambient);
		_pView->SetSkyData(m_sky);
		_pView->SetSkyTexture(m_skyTexRef);
		_pView->SetSceneFogData(m_sceneFog);
		_pView->SetGroundDustData(m_groundDust);
		_pView->SetFogNoiseTexture(m_fogNoiseTexRef);
		_pView->SetSceneFogCompositeData(m_fogComposite);

		//----------------------------------------------------------------------------
		// 平行光
		//
		// 席が無ければ取る。上限に達していると無効が返るので、そのフレームは何もしない
		//----------------------------------------------------------------------------
		auto* _pLightManager = a_ge.RefLightManager();
		if (!a_inoutDLHandle.IsValid())
		{
			a_inoutDLHandle = _pLightManager->AllocateDL();
		}

		if (auto* _pLight = _pLightManager->RefLight(a_inoutDLHandle))
		{
			_pLight->dir   = { m_dlDir.x, m_dlDir.y, m_dlDir.z };
			_pLight->color = { m_dlColor.x, m_dlColor.y, m_dlColor.z, 1.0f };

			// 色を 1.0 超えで持たせる従来の形をそのまま残すため、強さは掛けない。
			// (ポイントライトのように色と明るさを分けたくなったらここへ欄を足す)
			_pLight->brightness = 1.0f;
		}

		//----------------------------------------------------------------------------
		// 平行光の影
		//
		// パイプラインにはレイトレとシャドウマップの両方のパスが置いてあり、ここで選んだ側だけが働く
		//----------------------------------------------------------------------------
		_pLightManager->SetShadowSettings(m_shadow);
	}

	void SceneAmbient::ApplyNone(Graphics::GraphicsEngine& a_ge, Handle<Graphics::DirectionalLight>& a_inoutDLHandle)
	{
		a_ge.RefSceneView()->ClearAmbient();

		// 平行光の席を返す。
		// 返さないままにすると、環境設定の無いシーンでも前のシーンの太陽が残る
		if (a_inoutDLHandle.IsValid())
		{
			a_ge.RefLightManager()->RemoveLight(a_inoutDLHandle);
			a_inoutDLHandle = {};
		}
	}

	//======================================================================================
	// 編集UI
	//======================================================================================
	void SceneAmbient::DrawEdit(const ECS::EngineServices& a_services)
	{
		Engine::EditorField::Field("IsEnabled", m_isEnabled);
		Engine::EditorField::Tooltip("切ると、下に重なっているシーンの環境設定をそのまま使う(ポーズ画面など)");

		if (!m_isEnabled)
		{
			Engine::EditorField::HelpText("(このシーンの環境設定は使われません)");
			return;
		}

		DrawLightingEdit();
		DrawShadowEdit();
		DrawFogEdit();
		DrawVolumetricFogEdit(a_services);
		DrawSkyEdit(a_services);
	}

	void SceneAmbient::DrawLightingEdit()
	{
		Engine::EditorField::Header("Lighting");

		Engine::EditorField::Field("AmbientColor", m_ambient.ambientColorScale, 0.01f);
		Engine::EditorField::Field("DLColor", m_dlColor, 0.01f);
		Engine::EditorField::Field("DLDir", m_dlDir, 0.01f);
	}

	void SceneAmbient::DrawShadowEdit()
	{
		Engine::EditorField::Header("Shadow");

		// Raytracing : 主光源へレイを飛ばす / ShadowMap : 光源から見た深度と比べる
		Engine::EditorField::Field("ShadowMode", m_shadow.mode);

		// 影を落とす奥行き。シャドウマップは広げるほど1テクセルが粗くなる。
		// ボリュメトリックフォグはどちらの求め方でもここまで影を引く
		Engine::EditorField::Field("Distance", m_shadow.distance, 0.5f, 1.0f, 10000.0f);
		Engine::EditorField::Tooltip("影を落とす奥行き。ボリュメトリックフォグの影もここまで(Raytracing でも効く)");

		// ここから下はシャドウマップのときだけ効く
		if (m_shadow.mode != Graphics::EDirectionalShadowMode::ShadowMap) return;

		int _cascadeCount = static_cast<int>(m_shadow.cascadeCount);
		if (Engine::EditorField::Field("CascadeCount", _cascadeCount, 0.05f, 1,
			static_cast<int>(Graphics::MAX_SHADOW_CASCADES)))
		{
			m_shadow.cascadeCount = static_cast<uint32_t>(_cascadeCount);
		}

		// 0 で等間隔、1 で手前に寄せる(足元ほど細かくなる)
		Engine::EditorField::Slider("SplitLambda", m_shadow.splitLambda, 0.0f, 1.0f);

		// 縞(アクネ)が出たら上げる。上げすぎると影が接地面から浮く
		Engine::EditorField::Field("DepthBias", m_shadow.depthBias, 0.001f, 0.0f, 10.0f);
		Engine::EditorField::Field("NormalBias", m_shadow.normalBias, 0.01f, 0.0f, 10.0f);

		// 縁のぼかし幅(テクセル数)
		Engine::EditorField::Field("Softness", m_shadow.softness, 0.01f, 0.0f, 8.0f);

		// 画面の外(光源側)の遮蔽物をどこまで拾うか
		Engine::EditorField::Field("CasterDistance", m_shadow.casterDistance, 1.0f, 0.0f, 10000.0f);
	}

	void SceneAmbient::DrawFogEdit()
	{
		// enable が false の間はシェーダー側で計算ごとスキップされる
		Engine::EditorField::Header("HeightFog");
		{
			EnableField("HeightFogEnable", m_ambient.heightFogEnable);

			Engine::EditorField::ColorField("HeightFogColor", m_ambient.heightFogColor);
			Engine::EditorField::Field("HeightFogHeight", m_ambient.heightFogHeight, 0.1f);
			// 基準高さからこの距離だけ進むと 100%
			Engine::EditorField::Field("HeightFogMaxRange", m_ambient.heightFogMaxRange, 0.1f, 0.0f);

			// どちら側へ濃くしていくか
			static const char* DENSE_NAME[] = { "Upward", "Downward" };
			int _denseDown = m_ambient.heightFogDenseDown != 0 ? 1 : 0;
			if (Engine::EditorField::Combo("HeightFogDense", _denseDown, DENSE_NAME))
			{
				m_ambient.heightFogDenseDown = _denseDown;
			}
		}

		Engine::EditorField::Header("DistanceFog");
		{
			EnableField("DistanceFogEnable", m_ambient.distanceFogEnable);

			Engine::EditorField::ColorField("DistanceFogColor", m_ambient.distanceFogColor);
			Engine::EditorField::Field("DistanceFogStart", m_ambient.distanceFogStart, 0.1f, 0.0f);
			// この距離で 100%。開始距離より手前には下げられないようにしておく
			Engine::EditorField::Field("DistanceFogMaxRange", m_ambient.distanceFogMaxRange, 0.1f, m_ambient.distanceFogStart, FLT_MAX);
		}
	}

	//======================================================================================
	// ボリュメトリックフォグ
	//
	// 効くのは、カメラのパイプラインに SceneVolumetricFogPass と SceneFogCompositePass が
	// 置いてあるときだけ。パスの側に調整値は無い
	//======================================================================================
	void SceneAmbient::DrawVolumetricFogEdit(const ECS::EngineServices& a_services)
	{
		Engine::EditorField::Header("VolumetricFog");
		Engine::EditorField::HelpText("パイプラインに SceneVolumetricFogPass / SceneFogCompositePass があるときだけ効きます");

		// 合成 : メインカラーへ重ねるときの強さ
		EnableField("FogCompositeEnable", m_fogComposite.enable);
		Engine::EditorField::Tooltip("切るとフォグを重ねずにそのまま通す");
		Engine::EditorField::Field("FogCompositeIntensity", m_fogComposite.intensity, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("フォグの濃さに掛ける倍率");

		// シーンのフォグ
		Engine::EditorField::Header("SceneFog");
		Engine::EditorField::ColorField("SceneFogColor", m_sceneFog.fogColor);
		Engine::EditorField::Field("SceneFogDensity", m_sceneFog.density, 0.0001f, 0.0f);
		Engine::EditorField::Tooltip("濃さ(1m あたり)。0 ならシーンのフォグは掛からない");
		Engine::EditorField::Field("SceneFogMaxDistance", m_sceneFog.maxDistance, 1.0f, 0.0f);
		Engine::EditorField::Tooltip("空(何も描かれていない画素)へ向けて積分する距離(m)");
		Engine::EditorField::Slider("SceneFogAnisotropy", m_sceneFog.anisotropy, -0.9f, 0.9f);
		Engine::EditorField::Tooltip("平行光を散らす向きの偏り。正で光源の方向を見たときに明るく、0 で全方向に同じ");
		Engine::EditorField::Field("SceneFogLightScale", m_sceneFog.lightScale, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("フォグ・チリに届く光(環境光 + 平行光)に掛ける倍率。まぶしいときは下げる");
		Engine::EditorField::HelpText("フォグは環境光と平行光で照らされます。平行光の影は ShadowMode に合わせてシャドウマップ(CSM)かレイトレで落ち、Shadow の Distance まで届きます");

		// グラウンドダスト
		Engine::EditorField::Header("GroundDust");
		Engine::EditorField::ColorField("DustColor", m_groundDust.dustColor);
		Engine::EditorField::Field("DustDensity", m_groundDust.density, 0.01f, 0.0f);
		Engine::EditorField::Tooltip("濃さ(1m あたり)。0 ならチリは出ない");
		Engine::EditorField::Field("DustHeight", m_groundDust.height, 0.05f, 0.0f);
		Engine::EditorField::Tooltip("チリが立つ高さ(地面から。この高さで濃さが 0 になる)");
		Engine::EditorField::Field("DustStepSize", m_groundDust.stepSize, 0.01f, 0.01f);
		Engine::EditorField::Tooltip("チリの層の中をレイマーチする1歩の長さ(m)。層の中が長いと歩数の上限で伸びる");

		if (Engine::EditorField::AssetField(a_services, "NoiseTexture", "Texture", m_fogNoiseTexGUID))
		{
			if (a_services.pResourceManager)
			{
				m_fogNoiseTexRef = RequestTexture(*a_services.pResourceManager, m_fogNoiseTexGUID);
			}
		}
		Engine::EditorField::Field("DustNoiseScale", m_groundDust.noiseScale, 0.001f, 0.0f);
		Engine::EditorField::Tooltip("ノイズのワールド座標に掛ける倍率(大きいほど細かい)");

		if (!m_fogNoiseTexGUID.IsValid())
		{
			Engine::EditorField::HelpText("NoiseTexture 未設定 : チリはノイズなしで一様に立ちます");
		}
	}

	void SceneAmbient::DrawSkyEdit(const ECS::EngineServices& a_services)
	{
		Engine::EditorField::Header("Sky");

		// 正距円筒(横:縦 = 2:1)のテクスチャを想定している。
		// 選ぶだけで空になるので、スカイドームのモデルは置かなくてよい
		if (Engine::EditorField::AssetField(a_services, "Sky Texture", "Texture", m_skyTexGUID))
		{
			if (a_services.pResourceManager)
			{
				m_skyTexRef = RequestTexture(*a_services.pResourceManager, m_skyTexGUID);
			}
		}
		Engine::EditorField::Image(a_services, m_skyTexRef, 256, 128);

		if (!m_skyTexGUID.IsValid())
		{
			Engine::EditorField::WarningText("(Sky Texture 未設定 : 空は描かれません)");
		}

		// 露出 : 出力先がHDRなので 1.0 を超えて構わない。
		// 超えた分はブルームの抽出しきい値に乗り、最後にトーンマップで落ちる
		Engine::EditorField::Field("Exposure", m_sky.exposure, 0.01f, 0.0f, 100.0f);

		// 地平線の高さ = 仮想ドームの中心の高さ。カメラがここから離れると地平線が動く
		Engine::EditorField::Field("HorizonHeight", m_sky.horizonHeight, 0.1f);

		// 半径 : 小さいほどカメラの上下で地平線が強く動く。
		// 十分大きく取ると、ほぼ無限遠のスカイボックスと同じ見え方になる
		Engine::EditorField::Field("Radius", m_sky.radius, 1.0f, 0.01f, 100000.0f);
		if (m_sky.radius < 0.01f) m_sky.radius = 0.01f;

		// 方位の回転 : 空を回して太陽や雲の位置を平行光の向きに合わせる
		Engine::EditorField::Field("RotationDeg", m_sky.rotationDeg, 0.5f, -360.0f, 360.0f);
		if (m_sky.rotationDeg >= 360.0f) m_sky.rotationDeg -= 360.0f;
		if (m_sky.rotationDeg <= -360.0f) m_sky.rotationDeg += 360.0f;

		//----------------------------------------------------------------------
		// 空に被写界深度を掛けるか
		//
		// 空は深度が far のまま残るので、切っておかないと「一番遠いもの」として
		// 最大の奥ボケが掛かり、空だけがべったり滲む。
		// 掛けたいときだけ有効にして、倍率で強さを決める(1.0 で他の遠景と同じ)
		//----------------------------------------------------------------------
		EnableField("IsSkyDof", m_sky.isSkyDof);

		const bool _isSkyDof = (m_sky.isSkyDof != 0);
		{
			Engine::EditorField::DisabledScope _disabled(!_isSkyDof);
			Engine::EditorField::Field("SkyDofScale", m_sky.dofScale, 0.01f, 0.0f, 1.0f);
		}
		if (!_isSkyDof)
		{
			Engine::EditorField::HelpText("(空にはボケが掛かりません)");
		}
	}
}
