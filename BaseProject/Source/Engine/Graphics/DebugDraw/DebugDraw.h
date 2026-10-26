#pragma once

#include "DebugDrawConfig.h"

namespace Engine::Graphics
{
	// デバッグラインの形状
	enum class EShapeType : UINT
	{
		Line,
		Box,
		Capsule,
		Sphere
	};

	// デバッグライン用データ : 形状1つぶん。DebugLinePass がまとめて描く
	struct DebugLineData
	{
		Math::Color		color;
		Math::Matrix worldMat;
		UINT shapeType;
	};

	/// <summary>
	/// デバッグ用のワイヤー表示を積む場所
	/// </summary>
	/// <remarks>
	/// 形状を1つ積むと DebugLineData が1つ増え、DebugLinePass がまとめて描く。
	///
	/// ここはエンジン層に置いてある。以前は MainEditor が持っていたが、
	/// それだとエンジンとアプリからエディターを名指しすることになり、
	/// 依存が逆流していた。積む先をエンジン側に移したことで、
	/// エディターは「表示するかどうか」を決めるだけになっている。
	///
	/// 積む側の経路 :
	///   ECSのシステム    : SystemContext の pServices->pDebugDraw
	///   GameObject       : ObjectContext の pServices->pDebugDraw
	///   エンジン内部     : GraphicsEngine::RefDebugDraw()
	///
	/// 中身は描き終わりに捨てられる(GraphicsEngine::EndFrame)。
	/// 積んだフレームのうちに描かれるので、消す処理を呼ぶ側が持つ必要はない。
	///
	/// 設定(DebugDrawConfig::drawWire)が off のときは1本も積まない。
	/// 空のままなら描画側も早期リターンするので、描画コマンドごと止まる。
	/// </remarks>
	class DebugDraw
	{
	public:

		//===================================================================
		// 積む
		//===================================================================

		void DrawLine(
			const Math::Vector3& a_startPos,
			const Math::Vector3& a_endPos,
			const Math::Color& a_color = Color::WHITE
		);

		void DrawBox(const Math::Matrix& a_worldMat, const Math::Color& a_color = Color::WHITE);
		void DrawBox(const DirectX::BoundingBox& a_aabb, const Math::Color& a_color = Color::WHITE);
		void DrawBox(const DirectX::BoundingOrientedBox& a_obb, const Math::Color& a_color = Color::WHITE);

		void DrawCapsule(const Math::Matrix& a_worldMat, const Math::Color& a_color = Color::WHITE);

		void DrawSphere(const Math::Matrix& a_worldMat, const Math::Color& a_color = Color::WHITE);
		void DrawSphere(const DirectX::BoundingSphere& a_sphere, const Math::Color& a_color = Color::WHITE);

		/// <summary>
		/// レイを線で出し、当たっていれば終点に球を置く
		/// </summary>
		void DrawRay(
			const Math::Vector3& a_startPos,
			const Math::Vector3& a_dir,
			float a_length,
			bool a_isHit,
			const Math::Color& a_color = Color::WHITE
		);

		/// <summary>
		/// カメラの視錐台を12本の線で出す
		/// </summary>
		/// <param name="a_viewProj">ビュー×射影(転置前)。深度は 0(近)～1(遠)</param>
		void DrawFrustum(const Math::Matrix& a_viewProj, const Math::Color& a_color = Color::WHITE);

		//===================================================================
		// 描く側が使う
		//===================================================================

		// 積んだものを捨てる。フレームの頭で1回だけ呼ぶ
		void Clear();

		// 設定(線を積むかどうか)
		const DebugDrawConfig& GetConfig() const { return m_config; }
		DebugDrawConfig& RefConfig() { return m_config; }

		// 今積める状態か(設定が on で、上限に達していない)。
		// 積む前に大量の走査をする側が、無駄に回さないために見る
		bool IsEnabled() const;

		const std::vector<DebugLineData>& GetLineDataVec() const { return m_lineDataVec; }

	private:

		// 積んでよいか : 設定と上限を見る
		bool CanPush();

	private:

		std::vector<DebugLineData> m_lineDataVec = {};
		UINT m_capacity = 10000;

		DebugDrawConfig m_config = {};
	};
}
