#pragma once
#include "Engine/Graphics/Frame/SceneView/AmbientData.h"
#include "Engine/Graphics/Frame/SceneView/CameraData.h"
#include "Engine/Graphics/Frame/SceneView/CameraEffectData.h"
#include "Engine/Graphics/Frame/SceneView/GroundImpulse.h"
#include "Engine/Graphics/Frame/SceneView/SceneFogData.h"
#include "Engine/Graphics/Frame/SceneView/SkyData.h"

namespace Engine::Resource
{
	class Texture;
}

namespace Engine::Graphics
{
	//==========================================================================================
	// シーンの見え方
	//
	// パスが読む「カメラと、その周りの描画設定」をまとめて持つ。
	//   ・カメラ(ビュー/射影と、GPUへ送る形 : 転置・TAAのジッター・前フレームの行列)
	//   ・エディターカメラなどの割り込み
	//   ・カメラ発の画面効果(被写界深度 / ラジアルブラー / 魚眼レンズ)
	//   ・シーンの環境設定(環境光 / フォグ / ボリュメトリックフォグ / 空)
	//
	// 値を入れるのはカメラのシステム(CamSetShaderSystem)とシーン(SceneManager が
	// Engine::Scene::SceneAmbient から流し込む)、読むのはレンダーパス。GPUへ送る形を作るのは GraphicsEngine::Execute の中(UpdateGPUCameraData)
	//==========================================================================================
	class SceneView
	{
	public:

		// 描画解像度(ジッターの幅を決める)を受け取る
		void Init(UINT a_renderWidth, UINT a_renderHeight);

		// GPUへ送るカメラを確定する : 割り込みを当ててから、転置・ジッター込みの形を作る
		void UpdateGPUCameraData();

		// フレームの終わり : 今フレームだけの画面効果を落とす
		void EndFrame();

		//--------------------------------------------------------------------------------------------
		// カメラ
		//
		// SetCameraMat / SetProjMat は ECS のカメラ(CamSetShaderSystem)を控えるだけ。
		// 描くカメラへ当てるのは UpdateGPUCameraData で、割り込みがあればそちらが勝つ
		//--------------------------------------------------------------------------------------------
		void SetCameraMat(const Math::Matrix& a_worldMat);
		void SetProjMat(const Math::Matrix& a_projMat);

		// GetCameraData    : GPUへ送る形(転置・ジッター込み)。パスはこちらを読む
		// GetCPUCameraData : 割り込みを当てた後の描くカメラ(転置・ジッターなし)
		const CameraData& GetCameraData() const;
		const CameraData& GetCPUCameraData() const;

		// カメラの割り込み(エディターカメラなど)
		// ECS側のカメラ設定は描画(PreDraw)の中で行われるため、
		// 単に SetCameraMat を先に呼んでも上書きされてしまう。
		// ここに積んでおくと、ECS側の設定が終わった後・GPUデータ作成の直前に適用される。
		//
		// a_isCullByGameCamera : メッシュレットのカリングだけは ECS のカメラで行う。
		//   描くのは割り込んだカメラからなので、間引かれた様子を外から確かめられる(デバッグ用)
		void SetCameraOverride(const Math::Matrix& a_worldMat, const Math::Matrix& a_projMat, bool a_isCullByGameCamera);
		void ClearCameraOverride();

		// 今フレーム、カリング用カメラが描くカメラと別になっているか(UpdateGPUCameraData で確定)
		bool IsCullCameraSeparated() const { return m_isCullCameraSeparated; }

		// カリング用カメラのビュー×射影(転置前・ジッターなし)。視錐台をワイヤーで出すとき用
		const Math::Matrix& GetCullViewProjMat() const { return m_cullViewProjMat; }

		// TAA用のジッターを掛けるか。設定の持ち主(オプション)から毎フレーム流し込んでもらう
		void SetJitterEnabled(bool a_isEnabled) { m_isJitterEnabled = a_isEnabled; }

		//--------------------------------------------------------------------------------------------
		// カメラ発の画面効果(被写界深度 / ラジアルブラー / 魚眼レンズ)
		//
		// どれもカメラの持ち物で、アクティブカメラのコンポーネントから
		// CamSetShaderSystem が毎フレーム詰める。速度に応じて動く値がここに乗る。
		//
		// パス側はアセットに保存した自分の値を既定として持っているので、
		// 「今フレーム、カメラから送られてきたか」を Is～Override() で見て、
		// 送られていればそちらを優先する。
		// 送られなかったフレームは EndFrame でフラグが落ちるので、
		// パスは自分の値へ戻る(効果が前フレームの値で固まらない)
		//--------------------------------------------------------------------------------------------
		void SetDoFData(const DoFOptionCB& a_data);
		const DoFOptionCB& GetDoFData() const;
		bool IsDoFOverride() const { return m_isDoFOverride; }

		void SetRadialBlurData(const RadialBlurOptionCB& a_data);
		const RadialBlurOptionCB& GetRadialBlurData() const;
		bool IsRadialBlurOverride() const { return m_isRadialBlurOverride; }

		void SetFishEyeData(const FishEyeOptionCB& a_data);
		const FishEyeOptionCB& GetFishEyeData() const;
		bool IsFishEyeOverride() const { return m_isFishEyeOverride; }

		//--------------------------------------------------------------------------------------------
		// グラウンドフィールドの衝撃
		//
		// アプリ側が毎フレーム積む。EndFrame で空になるので、続いている衝撃は毎フレーム積み直すこと。
		// GraphicsEngine::Execute がフレームぶんの構造体バッファへ詰め、GroundFieldPass が読む。
		// MAX_GROUND_IMPULSES を超えたぶんは捨てる
		//--------------------------------------------------------------------------------------------
		void AddGroundImpulse(const GroundImpulse& a_impulse);
		const std::vector<GroundImpulse>& GetGroundImpulses() const { return m_groundImpulseVec; }

		//--------------------------------------------------------------------------------------------
		// シーンの環境設定(環境光 / 高さ・距離フォグ / ボリュメトリックフォグ / 空)
		//
		// どれもシーン(Engine::Scene::SceneAmbient)の持ち物で、SceneManager が毎フレーム流し込む。
		// パスは受け取るだけで、自分では値を持たない。
		//
		// テクスチャ(空 / フォグのノイズ)は所有せずハンドルだけ預かる。
		// 持ち主のシーンが消えたら ClearAmbient で空のハンドルへ戻してもらう
		// (空の間、スカイパスは何も描かず、フォグはノイズなしで描く)。
		//--------------------------------------------------------------------------------------------
		void SetAmbientData(const AmbientData& a_data);
		const AmbientData& GetAmbientData() const;

		void SetSkyData(const SkyData& a_data);
		const SkyData& GetSkyData() const;

		void SetSkyTexture(const Handle<Resource::Texture>& a_handle);
		const Handle<Resource::Texture>& GetSkyTexture() const;

		// ボリュメトリックフォグ : シーンのフォグ / グラウンドダスト / ノイズ / 合成
		void SetSceneFogData(const SceneFogCB& a_data) { m_cbSceneFog = a_data; }
		const SceneFogCB& GetSceneFogData() const { return m_cbSceneFog; }

		void SetGroundDustData(const GroundDustCB& a_data) { m_cbGroundDust = a_data; }
		const GroundDustCB& GetGroundDustData() const { return m_cbGroundDust; }

		void SetFogNoiseTexture(const Handle<Resource::Texture>& a_handle) { m_fogNoiseTexHandle = a_handle; }
		const Handle<Resource::Texture>& GetFogNoiseTexture() const { return m_fogNoiseTexHandle; }

		void SetSceneFogCompositeData(const SceneFogCompositeCB& a_data) { m_cbSceneFogComposite = a_data; }
		const SceneFogCompositeCB& GetSceneFogCompositeData() const { return m_cbSceneFogComposite; }

		//--------------------------------------------------------------------------------------------
		// フォグを照らす点光源(1つだけ)
		//
		// シーンの環境設定ではなく演出(ボスの死亡など)の持ち物。積む側が毎フレーム積み、
		// EndFrame で消える(積まなくなれば次のフレームから消える)。
		// 積む側が複数あると最後に積んだものが勝つ。
		// 環境設定より後(シーンの描画の中)で積んでよい。読むのはレンダーグラフが回るとき
		//--------------------------------------------------------------------------------------------
		void SetFogPointLight(const FogPointLightCB& a_data) { m_cbFogPointLight = a_data; }
		const FogPointLightCB& GetFogPointLight() const { return m_cbFogPointLight; }

		// 環境設定を「何も無い」状態へ戻す : 環境光なし・フォグなし・空なし。
		// 環境設定を使うシーンが1つも無いときと、持ち主のシーンが消えたときに通す
		void ClearAmbient();

	private:

		// 描くカメラ(m_cbCamera)へ行列を当てる
		void ApplyCameraMat(const Math::Matrix& a_worldMat, const Math::Matrix& a_projMat);

		// カメラをGPU用データに変換
		void CreateGPUCameraData();

	private:

		// 描画解像度(ジッターの幅に使う)
		UINT m_renderWidth = 0;
		UINT m_renderHeight = 0;

		// カメラデータ(CPU側の値と、GPUへ送る形)。割り込みを当てた後の「描くカメラ」
		CameraData m_cbCamera = {};
		CameraData m_cbGPUCamera = {};

		// ECS のカメラ(SetCameraMat / SetProjMat で入ったまま)。割り込みでは上書きしない
		Math::Matrix m_gameCameraWorldMat = Math::Matrix::Identity();
		Math::Matrix m_gameCameraProjMat = Math::Matrix::Identity();
		bool m_isGameCameraSet = false;

		// カメラの割り込み用
		bool m_isCameraOverride = false;
		bool m_isCullByGameCamera = false;
		Math::Matrix m_cameraOverrideWorldMat = Math::Matrix::Identity();
		Math::Matrix m_cameraOverrideProjMat = Math::Matrix::Identity();

		// カリング用カメラ(UpdateGPUCameraData で確定)
		bool m_isCullCameraSeparated = false;
		Math::Matrix m_cullViewProjMat = Math::Matrix::Identity();

		// TAA : ジッターを掛けるかと、モーションベクター用の前フレームの行列
		bool m_isJitterEnabled = true;
		Math::Matrix m_prevViewMat = {};
		Math::Matrix m_prevProjMat = {};
		Math::Matrix m_prevNonJitteredViewProj = {};
		int m_totalFrameCount = 0;

		// 環境データ
		AmbientData m_cbAmbient = {};

		// スカイの設定と、引くスカイテクスチャ(所有はしない)
		SkyData m_cbSky = {};
		Handle<Resource::Texture> m_skyTexHandle = {};

		// ボリュメトリックフォグの設定と、ダストに掛けるノイズテクスチャ(所有はしない)
		SceneFogCB m_cbSceneFog = {};
		GroundDustCB m_cbGroundDust = {};
		SceneFogCompositeCB m_cbSceneFogComposite = {};
		Handle<Resource::Texture> m_fogNoiseTexHandle = {};

		// フォグを照らす点光源(EndFrame で消す)
		FogPointLightCB m_cbFogPointLight = {};

		// 被写界深度データ(アクティブカメラの FocusParamComponent から毎フレーム設定)
		DoFOptionCB m_cbDoF = {};

		// ラジアルブラーデータ(アクティブカメラの RadialBlurComponent から毎フレーム設定)
		RadialBlurOptionCB m_cbRadialBlur = {};

		// 魚眼レンズデータ(アクティブカメラの FishEyeComponent から毎フレーム設定)
		FishEyeOptionCB m_cbFishEye = {};

		// 今フレーム、カメラから画面効果の値が送られてきたか。
		// EndFrame で落とすので、送られなかったフレームはパス側の値が使われる
		bool m_isDoFOverride = false;
		bool m_isRadialBlurOverride = false;
		bool m_isFishEyeOverride = false;

		// 今フレームのグラウンドフィールドの衝撃(EndFrame で空にする)
		std::vector<GroundImpulse> m_groundImpulseVec = {};
	};
}
