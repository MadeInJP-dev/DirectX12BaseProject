#include "BaseObject.h"
namespace Engine::GameObject
{
	void BaseObject::Release(ObjectContext& /*a_context*/)
	{

	}
	void Engine::GameObject::BaseObject::Update(ObjectContext& /*a_context*/)
	{

	}
	void BaseObject::Draw(ObjectContext & /*a_context*/)
	{

	}

	//======================================================================================
	// 親子
	//======================================================================================
	bool BaseObject::SetParent(BaseObject* a_pParent, bool a_isKeepWorld)
	{
		if (a_pParent == m_pParent) return true;

		// 自分自身・自分の子孫を親にすると輪ができて、行列を辿る処理が終わらなくなる
		if (a_pParent == this) return false;
		if (a_pParent && a_pParent->IsDescendantOf(this)) return false;

		// 付け替える前の親の行列は、外す前に取っておく
		const Math::Matrix _oldParentWorld = m_pParent ? m_pParent->CalcWorldMatrix() : Math::Matrix::Identity();

		if (m_pParent)
		{
			std::vector<BaseObject*>& _siblingVec = m_pParent->m_childVec;
			_siblingVec.erase(std::remove(_siblingVec.begin(), _siblingVec.end(), this), _siblingVec.end());
		}

		m_pParent = a_pParent;
		if (m_pParent) m_pParent->m_childVec.push_back(this);

		if (a_isKeepWorld)
		{
			const Math::Matrix _newParentWorld = m_pParent ? m_pParent->CalcWorldMatrix() : Math::Matrix::Identity();
			RebaseLocal(_oldParentWorld, _newParentWorld);
		}
		return true;
	}

	bool BaseObject::IsDescendantOf(const BaseObject* a_pAncestor) const
	{
		if (!a_pAncestor) return false;

		// 輪は SetParent が作らせないので、根まで辿れば必ず止まる
		for (const BaseObject* _pCurrent = m_pParent; _pCurrent; _pCurrent = _pCurrent->m_pParent)
		{
			if (_pCurrent == a_pAncestor) return true;
		}
		return false;
	}

	void BaseObject::DetachFromHierarchy()
	{
		if (m_pParent)
		{
			std::vector<BaseObject*>& _siblingVec = m_pParent->m_childVec;
			_siblingVec.erase(std::remove(_siblingVec.begin(), _siblingVec.end(), this), _siblingVec.end());
			m_pParent = nullptr;
		}

		for (BaseObject* _pChild : m_childVec)
		{
			if (_pChild) _pChild->m_pParent = nullptr;
		}
		m_childVec.clear();
	}

	//======================================================================================
	// 行列
	//======================================================================================
	Math::Matrix BaseObject::CalcWorldMatrix() const
	{
		// 行ベクトルの掛け方なので、自分のローカルを左に置いて親を右から掛けていく
		Math::Matrix _world = CalcLocalMatrix();
		for (const BaseObject* _pParent = m_pParent; _pParent; _pParent = _pParent->m_pParent)
		{
			_world = _world * _pParent->CalcLocalMatrix();
		}
		return _world;
	}
}
