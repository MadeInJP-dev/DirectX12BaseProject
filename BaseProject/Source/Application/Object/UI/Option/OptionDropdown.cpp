#include "OptionDropdown.h"

#include "Engine/GameObject/BaseObject/BaseObject.h"	// ObjectContext
#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/Input/InputManager/InputManager.h"

#include "Application/Object/UI/UIBase.h"
#include "../../../Game/Core/InputSettings.h"	// 押す・戻るのアクション

namespace App::Object
{
	//======================================================================================
	// 更新
	//--------------------------------------------------------------------------------------
	// 押されたかは各 UI の IsClicked(押して離した瞬間)で見る。
	// 外を押したかだけは「押した瞬間」で見る。離した瞬間まで待つと、
	// 外で押し始めてから候補の上で離したときに、閉じるのと選ぶのが同じフレームに重なる
	//======================================================================================
	int OptionDropdown::Update(Engine::GameObject::ObjectContext& a_context, std::span<const char* const> a_names)
	{
		int _chosen = -1;

		UIBase* _pField = ScrollPanelBase::FindUI(a_context, m_ui.field.guid);
		auto* _pInputManager = a_context.pServices ? a_context.pServices->pInputManager : nullptr;

		if (_pField && _pField->IsClicked())
		{
			// 選択欄 : 押すたびに開閉する
			m_isOpen = !m_isOpen;
		}
		else if (m_isOpen)
		{
			// 候補 : 押されたものに決めて閉じる
			const size_t _count = std::min(a_names.size(), m_ui.candidateGUIDVec.size());
			for (size_t _i = 0; _i < _count; ++_i)
			{
				UIBase* _pCandidate = ScrollPanelBase::FindUI(a_context, m_ui.candidateGUIDVec[_i]);
				if (!_pCandidate || !_pCandidate->IsClicked()) continue;

				_chosen = static_cast<int>(_i);
				m_isOpen = false;
				break;
			}

			// 外を押した・戻るキー : 選ばずに閉じる
			if (m_isOpen && _pInputManager)
			{
				const bool _isPressOutside =
					_pInputManager->IsPress(Game::EGameAction::Select) && !IsCursorOnDropdown(a_context);
				const bool _isBack = _pInputManager->IsPress(Game::EGameAction::Back);

				if (_isPressOutside || _isBack) m_isOpen = false;
			}
		}

		ApplyCandidates(a_context, a_names);
		return _chosen;
	}

	void OptionDropdown::SetFieldText(Engine::GameObject::ObjectContext& a_context, const std::string& a_text) const
	{
		ScrollPanelBase::SetUIText(a_context, m_ui.field.guid, m_ui.valueTextId, a_text);
	}

	bool OptionDropdown::IsCursorOnDropdown(Engine::GameObject::ObjectContext& a_context) const
	{
		if (UIBase* _pField = ScrollPanelBase::FindUI(a_context, m_ui.field.guid))
		{
			if (_pField->IsHovered()) return true;
		}

		for (const Core::GUID& _guid : m_ui.candidateGUIDVec)
		{
			UIBase* _pCandidate = ScrollPanelBase::FindUI(a_context, _guid);
			if (_pCandidate && _pCandidate->IsHovered()) return true;
		}
		return false;
	}

	void OptionDropdown::ApplyCandidates(Engine::GameObject::ObjectContext& a_context, std::span<const char* const> a_names) const
	{
		for (size_t _i = 0; _i < m_ui.candidateGUIDVec.size(); ++_i)
		{
			UIBase* _pCandidate = ScrollPanelBase::FindUI(a_context, m_ui.candidateGUIDVec[_i]);
			if (!_pCandidate) continue;

			// 選択肢より多く置かれた候補は使わない
			const bool _isUsed = (_i < a_names.size());
			_pCandidate->SetVisible(m_isOpen && _isUsed);

			if (_isUsed)
			{
				ScrollPanelBase::SetUIText(a_context, m_ui.candidateGUIDVec[_i], m_ui.candidateTextId, a_names[_i]);
			}
		}
	}
}
