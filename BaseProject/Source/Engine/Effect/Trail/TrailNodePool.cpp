#include "Engine/Effect/Trail/TrailNodePool.h"

#include "Engine/Graphics/Frame/DrawSubmitter/DrawSubmitter.h"

namespace Engine::Effect
{
	Handle<TrailNode> TrailNodePool::Acquire()
	{
		return m_pool.Add(TrailNode{});
	}

	void TrailNodePool::Detach(Handle<TrailNode>& a_handle)
	{
		if (auto* _pNode = m_pool.Ref(a_handle))
		{
			_pNode->Detach();
		}
		a_handle = {};
	}

	void TrailNodePool::Update(float a_dt)
	{
		auto& _nodeVec = m_pool.RefAll();
		for (size_t _i = 0; _i < _nodeVec.size(); ++_i)
		{
			if (!_nodeVec[_i].has_value()) continue;

			TrailNode& _node = _nodeVec[_i].value();
			_node.Update(a_dt);

			// 手放されて、残りの点も消えきったら返す
			if (_node.IsDetached() && _node.IsEmpty())
			{
				const uint16_t _index = static_cast<uint16_t>(_i);
				m_pool.Remove(Handle<TrailNode>(_index, m_pool.GetGeneration(_index)));
			}
		}
	}

	void TrailNodePool::Submit(const Math::Vector3& a_cameraPos, Graphics::DrawSubmitter& a_submitter)
	{
		for (const auto& _slot : m_pool.GetAll())
		{
			if (!_slot.has_value()) continue;

			const TrailNode& _node = _slot.value();
			const auto& _pointVec = _node.GetPointVec();
			if (_pointVec.size() < 2) continue;

			m_workVertexVec.clear();
			_node.BuildVertices(a_cameraPos, m_workVertexVec);

			// 並べ替えは帯の真ん中の点で測る
			const Math::Vector3 _sortPos = _pointVec[_pointVec.size() / 2].pos;

			const auto& _style = _node.GetStyle();
			a_submitter.SubmitTrail(m_workVertexVec, _style.textureHandle, _style.isAdditive, _sortPos);
		}
	}

	void TrailNodePool::Release()
	{
		m_pool.Release();
		m_workVertexVec.clear();
	}

	size_t TrailNodePool::GetAliveCount() const
	{
		size_t _count = 0;
		for (const auto& _slot : m_pool.GetAll())
		{
			if (_slot.has_value()) ++_count;
		}
		return _count;
	}
}
