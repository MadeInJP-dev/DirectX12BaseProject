#include "Engine/Graphics/DebugDraw/DebugDraw.h"

namespace Engine::Graphics
{
	void DebugDraw::DrawLine(
		const Math::Vector3& a_startPos,
		const Math::Vector3& a_endPos,
		const Math::Color& a_color
	)
	{
		if (!CanPush()) return;

		// 方向と長さを求める
		Math::Vector3 _dir = a_endPos - a_startPos;
		float _length = _dir.Length();

		// 長さがゼロに近い場合は描画をスキップ
		if (_length < 0.0001f) return;

		// 正規化された方向ベクトル
		Math::Vector3 _dirNorm = _dir / _length;

		// 真上・真下を向いている時のジンバルロックを防ぐためのUpベクトル
		Math::Vector3 _up = (std::abs(_dirNorm.y) > 0.999f) ? Math::Vector3::Right() : Math::Vector3::Up();

		// Z軸方向に伸びるようにスケール
		Math::Matrix _scaleMat = Math::Matrix::CreateScale(1.0f, 1.0f, _length);

		// 向きと位置を適用。
		// ラインメッシュはローカル +Z 方向に伸びているので、+Z を _dirNorm に向ける。
		// Math::Matrix::CreateWorld は渡した前方をそのまま Z 軸に据える(SimpleMath と違い反転しない)
		const Math::Matrix _worldMat = Math::Matrix::CreateWorld(a_startPos, _dirNorm, _up);

		// データ作成
		DebugLineData _data = {};
		_data.color = a_color;											// 色指定
		_data.shapeType = static_cast<UINT>(EShapeType::Line);			// 形状指定
		_data.worldMat = (_scaleMat * _worldMat).Transpose();			// 行列合成

		// 配列に追加
		m_lineDataVec.push_back(_data);
	}

	void DebugDraw::DrawBox(const Math::Matrix& a_worldMat, const Math::Color& a_color)
	{
		if (!CanPush()) return;

		// データ作成
		DebugLineData _data = {};
		_data.color = a_color;
		_data.shapeType = static_cast<UINT>(EShapeType::Box);
		_data.worldMat = a_worldMat.Transpose();
		m_lineDataVec.push_back(_data);
	}

	void DebugDraw::DrawBox(const DirectX::BoundingBox& a_aabb, const Math::Color& a_color)
	{
		// Extents は「中心からの半分の長さ」なので、全体サイズにするために 2倍 してスケールにする
		Math::Vector3 _scale = Math::Vector3(a_aabb.Extents) * 2.0f;

		Math::Matrix _worldMat =
			Math::Matrix::CreateScale(_scale) *
			Math::Matrix::CreateTranslation(a_aabb.Center);

		// 既存の行列受け取り用DrawBoxへ委譲
		DrawBox(_worldMat, a_color);
	}

	void DebugDraw::DrawBox(const DirectX::BoundingOrientedBox& a_obb, const Math::Color& a_color)
	{
		// OBBは回転も持っているので、クォータニオンから回転行列を作成して挟む
		Math::Vector3 _scale = Math::Vector3(a_obb.Extents) * 2.0f;

		Math::Matrix _worldMat =
			Math::Matrix::CreateScale(_scale) *
			Math::Matrix::CreateFromQuaternion(a_obb.Orientation) *
			Math::Matrix::CreateTranslation(a_obb.Center);

		DrawBox(_worldMat, a_color);
	}

	void DebugDraw::DrawCapsule(const Math::Matrix& a_worldMat, const Math::Color& a_color)
	{
		if (!CanPush()) return;

		// データ作成
		DebugLineData _data = {};
		_data.color = a_color;
		_data.shapeType = static_cast<UINT>(EShapeType::Capsule);
		_data.worldMat = a_worldMat.Transpose();
		m_lineDataVec.push_back(_data);
	}

	void DebugDraw::DrawSphere(const Math::Matrix& a_worldMat, const Math::Color& a_color)
	{
		if (!CanPush()) return;

		// データ作成
		DebugLineData _data = {};
		_data.color = a_color;
		_data.shapeType = static_cast<UINT>(EShapeType::Sphere);
		_data.worldMat = a_worldMat.Transpose();
		m_lineDataVec.push_back(_data);
	}

	void DebugDraw::DrawSphere(const DirectX::BoundingSphere& a_sphere, const Math::Color& a_color)
	{
		// HLSL側のスフィアが直径1.0（半径0.5）で作られているため、
		// Radiusに合わせるために直径分のスケールをかける
		float _scale = a_sphere.Radius * 2.0f;

		Math::Matrix _worldMat =
			Math::Matrix::CreateScale(_scale) *
			Math::Matrix::CreateTranslation(a_sphere.Center);

		DrawSphere(_worldMat, a_color);
	}

	void DebugDraw::DrawRay(
		const Math::Vector3& a_startPos,
		const Math::Vector3& a_dir,
		float a_length,
		bool a_isHit,
		const Math::Color& a_color
	)
	{
		if (!CanPush()) return;

		auto _endPos = a_startPos + (a_dir * a_length);
		DrawLine(a_startPos, _endPos, a_color);

		if (a_isHit)
		{
			auto _mat = Math::Matrix::CreateTranslation(_endPos);
			DrawSphere(_mat, Color::RED);
		}
	}

	void DebugDraw::DrawFrustum(const Math::Matrix& a_viewProj, const Math::Color& a_color)
	{
		if (!CanPush()) return;

		// クリップ空間の箱の8隅を、逆行列でワールドへ戻す
		// [0～3] 近平面 / [4～7] 遠平面。どちらも 左下 → 右下 → 右上 → 左上 の順
		const Math::Matrix _invViewProj = a_viewProj.Invert();

		Math::Vector3 _corners[8] = {};
		for (int _i = 0; _i < 8; ++_i)
		{
			const float _x = (_i == 1 || _i == 2 || _i == 5 || _i == 6) ? 1.0f : -1.0f;
			const float _y = ((_i % 4) >= 2) ? 1.0f : -1.0f;
			const float _z = (_i < 4) ? 0.0f : 1.0f;

			// 射影が混ざっているので w で割る TransformCoord を使う
			_corners[_i] = Math::Vector3::TransformCoord(Math::Vector3(_x, _y, _z), _invViewProj);
		}

		for (int _i = 0; _i < 4; ++_i)
		{
			const int _next = (_i + 1) % 4;
			DrawLine(_corners[_i], _corners[_next], a_color);				// 近平面の縁
			DrawLine(_corners[_i + 4], _corners[_next + 4], a_color);		// 遠平面の縁
			DrawLine(_corners[_i], _corners[_i + 4], a_color);				// 近と遠をつなぐ辺
		}
	}

	void DebugDraw::Clear()
	{
		m_lineDataVec.clear();
		m_lineDataVec.reserve(m_capacity);
	}

	bool DebugDraw::IsEnabled() const
	{
		return m_config.drawWire && m_lineDataVec.size() < m_capacity;
	}

	bool DebugDraw::CanPush()
	{
		// 設定で切られていれば1本も積まない。
		// 空のままなら RenderContext::DrawShape も早期リターンするので、
		// 描画コマンドごと止まる
		if (!m_config.drawWire) return false;

		if (m_lineDataVec.size() >= m_capacity)
		{
			ENGINE_LOG("これ以上のデバッグラインは描画できません");
			return false;
		}

		return true;
	}
}
