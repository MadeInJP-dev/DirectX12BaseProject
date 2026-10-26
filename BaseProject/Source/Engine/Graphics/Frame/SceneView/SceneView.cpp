#include "Engine/Graphics/Frame/SceneView/SceneView.h"

namespace Engine::Graphics
{
	void SceneView::Init(UINT a_renderWidth, UINT a_renderHeight)
	{
		m_renderWidth = a_renderWidth;
		m_renderHeight = a_renderHeight;

		// 環境設定は、シーンが流し込むまで無し
		ClearAmbient();
	}

	//==========================================================================================
	// 環境設定を「何も無い」状態へ戻す
	//
	// フォグの CB は既定値のままだと濃さが入っている(シーンで使う既定として持たせているため)。
	// 「無し」はここで明示的に 0 へ落とす
	//==========================================================================================
	void SceneView::ClearAmbient()
	{
		m_cbAmbient = {};
		m_cbAmbient.ambientColorScale = { 0,0,0 };

		m_cbSky = {};
		m_skyTexHandle = {};

		m_cbSceneFog = {};
		m_cbSceneFog.density = 0.0f;

		m_cbGroundDust = {};
		m_cbGroundDust.density = 0.0f;

		m_cbSceneFogComposite = {};
		m_cbSceneFogComposite.enable = 0;

		m_fogNoiseTexHandle = {};
	}

	//==========================================================================================
	// GPUへ送るカメラを確定する(Execute の中、描画要求を積み終えた後に1回)
	//
	// ECS側のカメラ設定は描画(PreDraw)の中で行われるので、
	// エディターカメラなどの割り込みは必ずその後・GPUデータを作る直前に当てる。
	//
	// ECS が入れた行列(m_gameCamera～)は割り込みで上書きしない。
	// 上書きしてしまうと、カリングをゲームカメラで行うときに引く先が無くなるうえ、
	// ECS が送ってこなかったフレームには前フレームの割り込みがゲームカメラとして残る
	//==========================================================================================
	void SceneView::UpdateGPUCameraData()
	{
		if (m_isCameraOverride)
		{
			ApplyCameraMat(m_cameraOverrideWorldMat, m_cameraOverrideProjMat);
		}
		else
		{
			ApplyCameraMat(m_gameCameraWorldMat, m_gameCameraProjMat);
		}

		CreateGPUCameraData();

		//----------------------------------------------------------------------------------
		// カリング用カメラ
		//
		// 普段は描くカメラそのもの。
		// 割り込み中に「カリングをゲームカメラで行う」が立っていれば、ECS のカメラで間引き、
		// 描くのは割り込んだカメラ(エディターのフリーカメラ)から。
		// こうするとゲームカメラの視錐台の外にあるメッシュレットが消えている様子を外から見られる。
		// ゲームカメラが一度も送られていなければ比べる相手がいないので、描くカメラのまま
		//----------------------------------------------------------------------------------
		m_isCullCameraSeparated = m_isCameraOverride && m_isCullByGameCamera && m_isGameCameraSet;

		if (m_isCullCameraSeparated)
		{
			const Math::Vector4 _gamePos = {
				m_gameCameraWorldMat._41, m_gameCameraWorldMat._42, m_gameCameraWorldMat._43, 1.0f };

			// ジッターは描く側の都合なので、カリングにはジッターなしの行列を使う(描くカメラと同じ扱い)
			m_cullViewProjMat = m_gameCameraWorldMat.Invert() * m_gameCameraProjMat;
			m_cbGPUCamera.SetCullCamera(_gamePos, m_cullViewProjMat);
		}
		else
		{
			m_cullViewProjMat = m_cbCamera.viewMat * m_cbCamera.projMat;
			m_cbGPUCamera.UseSelfAsCullCamera();
		}
	}

	//==========================================================================================
	// フレームの終わり
	//
	// 画面効果は毎フレーム、アクティブカメラが設定し直す。
	// ここで落としておけば、カメラが居ない/その設定を持たないフレームは
	// 前フレームの値で効き続けることがない。
	// フラグを下ろすと、パスはアセットに保存した自分の値へ戻る
	//==========================================================================================
	void SceneView::EndFrame()
	{
		m_cbDoF = {};
		m_isDoFOverride = false;

		m_cbRadialBlur = {};
		m_isRadialBlurOverride = false;

		m_cbFishEye = {};
		m_isFishEyeOverride = false;

		m_groundImpulseVec.clear();

		m_cbFogPointLight = {};
	}

	void SceneView::AddGroundImpulse(const GroundImpulse& a_impulse)
	{
		// バッファは上限ぶんしか確保していないので、溢れたぶんは捨てる
		if (m_groundImpulseVec.size() >= MAX_GROUND_IMPULSES) return;
		m_groundImpulseVec.push_back(a_impulse);
	}

	// ECS のカメラを控えるだけ。描くカメラへ当てるのは UpdateGPUCameraData
	void SceneView::SetCameraMat(const Math::Matrix& a_worldMat)
	{
		m_gameCameraWorldMat = a_worldMat;
		m_isGameCameraSet = true;
	}
	void SceneView::SetProjMat(const Math::Matrix& a_projMat)
	{
		m_gameCameraProjMat = a_projMat;
	}
	void SceneView::ApplyCameraMat(const Math::Matrix& a_worldMat, const Math::Matrix& a_projMat)
	{
		// 座標を代入
		m_cbCamera.pos = { a_worldMat._41,a_worldMat._42,a_worldMat._43 ,1 };

		// ビュー行列・逆ビュー行列をセット
		m_cbCamera.viewMat = a_worldMat.Invert();
		m_cbCamera.viewInvMat = a_worldMat;

		m_cbCamera.projMat = a_projMat;
		m_cbCamera.projInvMat = a_projMat.Invert();
	}
	void SceneView::SetDoFData(const DoFOptionCB& a_data)
	{
		m_cbDoF = a_data;

		// 今フレームはカメラが決めた値を使う
		m_isDoFOverride = true;
	}
	const DoFOptionCB& SceneView::GetDoFData() const
	{
		return m_cbDoF;
	}
	void SceneView::SetRadialBlurData(const RadialBlurOptionCB& a_data)
	{
		m_cbRadialBlur = a_data;

		// 今フレームはカメラが決めた値を使う
		m_isRadialBlurOverride = true;
	}
	const RadialBlurOptionCB& SceneView::GetRadialBlurData() const
	{
		return m_cbRadialBlur;
	}
	void SceneView::SetFishEyeData(const FishEyeOptionCB& a_data)
	{
		m_cbFishEye = a_data;

		// 今フレームはカメラが決めた値を使う
		m_isFishEyeOverride = true;
	}
	const FishEyeOptionCB& SceneView::GetFishEyeData() const
	{
		return m_cbFishEye;
	}
	void SceneView::SetCameraOverride(const Math::Matrix& a_worldMat, const Math::Matrix& a_projMat, bool a_isCullByGameCamera)
	{
		m_isCameraOverride = true;
		m_cameraOverrideWorldMat = a_worldMat;
		m_cameraOverrideProjMat = a_projMat;
		m_isCullByGameCamera = a_isCullByGameCamera;
	}
	void SceneView::ClearCameraOverride()
	{
		m_isCameraOverride = false;
		m_isCullByGameCamera = false;
	}
	const CameraData& SceneView::GetCameraData() const
	{
		return m_cbGPUCamera;
	}
	const CameraData& SceneView::GetCPUCameraData() const
	{
		return m_cbCamera;
	}
	void SceneView::SetAmbientData(const AmbientData& a_data)
	{
		m_cbAmbient = a_data;
	}
	const AmbientData& SceneView::GetAmbientData() const
	{
		return m_cbAmbient;
	}
	void SceneView::SetSkyData(const SkyData& a_data)
	{
		m_cbSky = a_data;
	}
	const SkyData& SceneView::GetSkyData() const
	{
		return m_cbSky;
	}
	void SceneView::SetSkyTexture(const Handle<Resource::Texture>& a_handle)
	{
		m_skyTexHandle = a_handle;
	}
	const Handle<Resource::Texture>& SceneView::GetSkyTexture() const
	{
		return m_skyTexHandle;
	}

	void SceneView::CreateGPUCameraData()
	{
		// リセット
		m_cbGPUCamera = {};

		// ジッターオフセット計算
		float _jitterX = 0.0f;
		float _jitterY = 0.0f;

		// ジッターオンオフ(SceneViewConfig::useJitter。OFFならジッター0でTAAはブレンドのみ)
		if (m_config.useJitter && m_renderWidth > 0 && m_renderHeight > 0)
		{
			// ハルトンシーケンスのテーブル（ピクセル中心地からのオフセット値 -0.5f ～ 0.5f）
			static const float HALTON_X[16] = {
				0.000000f, -0.250000f,  0.250000f, -0.375000f,
				0.125000f, -0.125000f,  0.375000f, -0.437500f,
				0.062500f, -0.187500f,  0.312500f, -0.312500f,
				0.187500f, -0.062500f,  0.437500f, -0.468750f
			};
			static const float HALTON_Y[16] = {
				0.000000f,  0.166667f, -0.166667f,  0.500000f,
			   -0.500000f, -0.277778f,  0.055556f,  0.388889f,
			   -0.388889f, -0.055556f,  0.277778f,  0.444444f,
			   -0.222222f,  0.111111f, -0.444444f,  0.222222f
			};
			uint32_t _sampleIndex = m_totalFrameCount % 16;

			// プロジェクション空間（NDC）のサイズに変換 : NDCは幅が２(-1～1)だから2倍
			_jitterX = (HALTON_X[_sampleIndex] / static_cast<float>(m_renderWidth)) * 2.0f;
			_jitterY = (HALTON_Y[_sampleIndex] / static_cast<float>(m_renderHeight)) * 2.0f;
		}

		// カメラの行列を一時的に取得
		Math::Matrix _viewMat = m_cbCamera.viewMat;
		Math::Matrix _projMat = m_cbCamera.projMat;
		Math::Matrix _invViewMat = m_cbCamera.viewInvMat;
		Math::Matrix _invProjMat = m_cbCamera.projInvMat;

		// モーションベクター用のジッターなしViewProjを計算
		Math::Matrix _nonJitteredViewProj = _viewMat * _projMat;
		Math::Matrix _nonJitteredInvViewProj = _nonJitteredViewProj.Invert();

		// 描画用のジッターあり投影行列を作成
		Math::Matrix _jitteredProjMat = _projMat;
		_jitteredProjMat._31 += _jitterX;
		_jitteredProjMat._32 += _jitterY;

		// 描画用のジッターありViewProjとその逆行列を計算
		Math::Matrix _jitteredViewProj = _viewMat * _jitteredProjMat;
		Math::Matrix _invJitteredProj = _jitteredProjMat.Invert();
		Math::Matrix _invJitteredViewProj = _jitteredViewProj.Invert();

		// GPU転送用バッファへの詰め込み
		m_cbGPUCamera.pos = m_cbCamera.pos;

		// 通常の描画（SV_Positionの計算）にはジッターありを使う
		m_cbGPUCamera.viewMat = _viewMat.Transpose();
		m_cbGPUCamera.projMat = _jitteredProjMat.Transpose();
		m_cbGPUCamera.viewInvMat = _invViewMat.Transpose();
		m_cbGPUCamera.projInvMat = _invJitteredProj.Transpose();
		m_cbGPUCamera.viewProjMat = _jitteredViewProj.Transpose();
		m_cbGPUCamera.invViewProjMat = _invJitteredViewProj.Transpose();

		// モーションベクターの計算にはジッターなしを使う
		m_cbGPUCamera.nonJitteredProj = _projMat.Transpose();
		m_cbGPUCamera.nonJitteredViewProj = _nonJitteredViewProj.Transpose();
		m_cbGPUCamera.nonJitteredInvViewProj = _nonJitteredInvViewProj.Transpose();

		// 過去フレームのジッターなし行列の処理
		m_cbGPUCamera.prevView = m_prevViewMat.Transpose();
		m_cbGPUCamera.prevProj = m_prevProjMat.Transpose();
		m_cbGPUCamera.prevViewProj = m_prevNonJitteredViewProj.Transpose();

		// 次のフレームのためにジッターなしデータを保存
		m_prevViewMat = _viewMat;
		m_prevProjMat = _projMat;
		m_prevNonJitteredViewProj = _nonJitteredViewProj;

		// 完成したデータから、フラスタム平面を求める
		m_cbGPUCamera.ExtractFrustumPlanes(_nonJitteredViewProj);

		// フレームカウントを進める
		m_totalFrameCount++;
	}
}
