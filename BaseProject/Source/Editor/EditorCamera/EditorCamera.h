#pragma once

#include "Editor/EditorCommon.h"

namespace Engine::Graphics
{
	class GraphicsEngine;
}

namespace Editor
{
	//=======================================================================
	//
	// エディター専用のフリーカメラ
	//
	// Unity / Unreal と同じく、右クリックを押している間だけ操作できる。
	//   右ドラッグ : 視点回転
	//   W / A / S / D : 前後左右
	//   E / Q         : 上昇 / 下降(ワールドのY軸基準)
	//   Shift         : 加速
	//   ホイール      : 移動速度の調整
	//
	// 操作の開始判定はシーンビュー上にカーソルがある時だけ。
	// 一度始まったらカーソルが枠外へ出ても、ボタンを離すまで操作を続ける。
	//
	//=======================================================================
	class EditorCamera
	{
	public:

		// a_pGraphicsEngine : 射影とスクリーン座標の変換に使う描画解像度を読む先(借り物)
		void Init(const Engine::Graphics::GraphicsEngine* a_pGraphicsEngine);

		// a_dt : 実フレーム時間
		void Update(float a_dt);

		//-------------------------------------------------------------------
		// 行列取得 : GraphicsEngine へ割り込ませるために使う
		//-------------------------------------------------------------------
		// カメラのワールド行列(ビュー行列ではない)
		const Math::Matrix& GetWorldMatrix() const { return m_worldMat; }
		const Math::Matrix& GetProjMatrix()  const { return m_projMat; }

		//-------------------------------------------------------------------
		// 状態
		//-------------------------------------------------------------------
		// 無効にすると ECS 側のカメラがそのまま使われる
		bool IsEnable() const { return m_isEnable; }
		void SetEnable(bool a_isEnable) { m_isEnable = a_isEnable; }

		// メッシュレットのカリングだけはゲームのカメラで行うか(デバッグ用)。
		// 描くのはこのカメラからなので、ゲームカメラの視錐台の外が間引かれている様子を外から見られる
		bool IsCullByGameCamera() const { return m_isCullByGameCamera; }

		// 右クリック操作中かどうか(ギズモなど他の操作との競合を避ける用)
		bool IsControlling() const { return m_isControlling; }

		/// <summary>
		/// 操作中の状態を強制的に解除する
		/// </summary>
		/// <remarks>
		/// 操作の継続判定は ImGui の「右ボタンが押されているか」だけを見ている。
		/// プレイモード中は ImGui の入力が更新されないので、押したまま切り替えると
		/// 押しっぱなしのまま固まり、戻ってきた瞬間に暴れる。
		/// モードの切り替え時にここで断ち切る。
		/// </remarks>
		void CancelControl() { m_isControlling = false; }

		// シーンビューにカーソルが乗っているか。SceneViewPanel が毎フレーム設定する。
		void SetViewportHovered(bool a_isHovered) { m_isViewportHovered = a_isHovered; }

		/// <summary>
		/// 位置と向きを直接入れる(初期位置へ戻す用)
		/// </summary>
		void SetPose(const Math::Vector3& a_pos, float a_yawDeg, float a_pitchDeg);

		// パラメーター調整用UI
		void DrawEditUI();

		// マウス座標からスクリーン上の近平面からレイを飛ばす用の設定を作成
		Math::Ray ScreenPointToRay(const Math::Vector2& a_mousePos,float a_maxDistance = 1000);

	private:

		// 現在の姿勢からクォータニオンを作る
		Math::Quaternion CalcRotation() const;

		void UpdateRotation();
		void UpdateMove(float a_dt);
		void BuildMatrix();

	private:

		// 姿勢
		Math::Vector3 m_pos = { 0.0f, 3.0f, -10.0f };
		float m_yaw   = 0.0f;	// 度
		float m_pitch = 0.0f;	// 度

		// 操作感
		float m_moveSpeed   = 10.0f;	// メートル/秒
		float m_boostRate   = 4.0f;		// Shift を押している間の倍率
		float m_sensitivity = 0.15f;	// 度/ピクセル

		// 描画解像度の読み先
		const Engine::Graphics::GraphicsEngine* m_pGraphicsEngine = nullptr;

		// 射影
		float m_fovY  = 60.0f;
		float m_nearZ = 0.1f;
		float m_farZ  = 1000.0f;

		// 状態
		bool m_isEnable         = true;
		bool m_isCullByGameCamera = false;
		bool m_isControlling    = false;
		bool m_isViewportHovered = false;

		// 出力
		Math::Matrix m_worldMat = Math::Matrix::Identity();
		Math::Matrix m_projMat  = Math::Matrix::Identity();

		// 真上・真下を向くと視線とUpベクトルが平行になり行列が破綻するため手前で止める
		static constexpr float MAX_PITCH = 89.0f;
	};
}
