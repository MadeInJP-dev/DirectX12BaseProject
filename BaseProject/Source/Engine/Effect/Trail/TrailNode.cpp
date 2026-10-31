#include "Engine/Effect/Trail/TrailNode.h"

namespace Engine::Effect
{
	void TrailNode::SetStyle(const TrailStyle& a_style)
	{
		m_style = a_style;
		TrimToMaxPoints();
	}

	void TrailNode::AddPoint(const Math::Vector3& a_pos)
	{
		// 手放した後は増やさない(残りが消えていくだけ)
		if (m_isDetached) return;

		//------------------------------------------------------------------
		// 末尾の点は「今の位置」なので、毎フレーム動かす。
		// その1つ前の点から minVertexDistance 以上離れたら、末尾をそこへ残して新しい点を足す。
		// こうすると、ゆっくり動いても帯の先頭は持ち主に付いてきて、点はほどよい間隔で並ぶ
		//------------------------------------------------------------------
		const size_t _count = m_pointVec.size();
		if (_count < 2)
		{
			m_pointVec.push_back({ a_pos, 0.0f });
		}
		else
		{
			const Math::Vector3& _prevPos = m_pointVec[_count - 2].pos;
			const float _minDist = (std::max)(m_style.minVertexDistance, 0.0f);

			if ((a_pos - _prevPos).LengthSquared() >= _minDist * _minDist)
			{
				m_pointVec.push_back({ a_pos, 0.0f });
			}
			else
			{
				m_pointVec.back() = { a_pos, 0.0f };
			}
		}

		TrimToMaxPoints();
	}

	void TrailNode::Update(float a_dt)
	{
		for (auto& _point : m_pointVec)
		{
			_point.age += a_dt;
		}

		// 寿命を過ぎた点は尾(先頭)から並んでいるので、前から数えて切る
		const float _lifeTime = (std::max)(m_style.lifeTime, 0.0f);
		size_t _expiredCount = 0;
		while (_expiredCount < m_pointVec.size() && m_pointVec[_expiredCount].age >= _lifeTime)
		{
			++_expiredCount;
		}
		if (_expiredCount > 0)
		{
			m_pointVec.erase(m_pointVec.begin(), m_pointVec.begin() + static_cast<std::ptrdiff_t>(_expiredCount));
		}
	}

	void TrailNode::TrimToMaxPoints()
	{
		const size_t _maxPoints = static_cast<size_t>((std::max)(m_style.maxPoints, 2));
		if (m_pointVec.size() <= _maxPoints) return;

		const size_t _overCount = m_pointVec.size() - _maxPoints;
		m_pointVec.erase(m_pointVec.begin(), m_pointVec.begin() + static_cast<std::ptrdiff_t>(_overCount));
	}

	void TrailNode::BuildVertices(const Math::Vector3& a_cameraPos, std::vector<Graphics::TrailVertex>& a_outVertexVec) const
	{
		const size_t _count = m_pointVec.size();
		if (_count < 2) return;

		const float _lifeTime = (std::max)(m_style.lifeTime, 1e-4f);

		// 向きが決まらない点(進む向きとカメラの向きが重なる)は、1つ前の点の横向きを使う
		Math::Vector3 _prevSide = Math::Vector3(0.0f, 1.0f, 0.0f);

		for (size_t _i = 0; _i < _count; ++_i)
		{
			const TrailPoint& _point = m_pointVec[_i];

			//--------------------------------------------------------------
			// 帯の横向き : 進む向き × カメラへの向き
			//
			// 進む向きは前後の点の差。両端は隣の点との差で代える
			//--------------------------------------------------------------
			const Math::Vector3& _prevPos = m_pointVec[(_i == 0) ? 0 : _i - 1].pos;
			const Math::Vector3& _nextPos = m_pointVec[(_i + 1 < _count) ? _i + 1 : _i].pos;
			const Math::Vector3 _tangent = _nextPos - _prevPos;
			const Math::Vector3 _toCamera = a_cameraPos - _point.pos;

			Math::Vector3 _side = _tangent.Cross(_toCamera);
			if (_side.LengthSquared() > 1e-10f)
			{
				_side.Normalize();
				_prevSide = _side;
			}
			else
			{
				_side = _prevSide;
			}

			//--------------------------------------------------------------
			// 見た目は点の年齢で決める(0 = 先頭 / 1 = 尾)。
			// 点の番号で決めると、点が足されるたびに帯全体の色がずれて見える
			//--------------------------------------------------------------
			const float _t = std::clamp(_point.age / _lifeTime, 0.0f, 1.0f);

			const float _halfWidth = std::lerp(m_style.width, m_style.endWidth, _t) * 0.5f;
			const Math::Color _color = Math::Color::Lerp(m_style.headColor, m_style.tailColor, _t);
			const Math::Vector4 _outColor(
				_color.r * m_style.intensity,
				_color.g * m_style.intensity,
				_color.b * m_style.intensity,
				_color.a);

			Graphics::TrailVertex _left = {};
			_left.pos = _point.pos + _side * _halfWidth;
			_left.uv = Math::Vector2(_t, 0.0f);
			_left.color = _outColor;

			Graphics::TrailVertex _right = {};
			_right.pos = _point.pos - _side * _halfWidth;
			_right.uv = Math::Vector2(_t, 1.0f);
			_right.color = _outColor;

			a_outVertexVec.push_back(_left);
			a_outVertexVec.push_back(_right);
		}
	}
}
