#include "GameOptionPanel.h"

#include "Engine/GameObject/BaseObject/BaseObject.h"	// ObjectContext
#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/EditorField/EditorField.h"

#include "Application/InstanceResource/UserDataResource.h"

namespace App::Object
{
	void GameOptionPanel::OnOpen(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		m_settings = _pUserDataResource->pUserData->GetGameSettings();

		// 開いたまま閉じられていても、開き直したら閉じた状態から始める
		m_fpsRateSelector.Close();
	}

	Engine::EFPSRate GameOptionPanel::GetCurrentFPSRate(Engine::GameObject::ObjectContext& a_context) const
	{
		if (m_settings.opFPSRate) return m_settings.opFPSRate.value();

		// 選ばれていなければ、エンジンが今使っている値(作り手の値)
		if (a_context.pServices && a_context.pServices->pMainEngine)
		{
			return a_context.pServices->pMainEngine->GetFPSRate();
		}
		return Engine::EFPSRate::FPS_60;
	}

	//======================================================================================
	// 並べる
	//======================================================================================
	void GameOptionPanel::LayoutItems(Engine::GameObject::ObjectContext& a_context)
	{
		PlaceHeaderRow(a_context, m_performanceHeader);
		PlaceDropdown(a_context, m_fpsRateSelector.GetUI());
		AdvanceRow();
	}

	//======================================================================================
	// 操作
	//======================================================================================
	void GameOptionPanel::UpdateItems(Engine::GameObject::ObjectContext& a_context)
	{
		if (!m_fpsRateSelector.Update(a_context, GetCurrentFPSRate(a_context))) return;

		// 選んだ時点で「選ばれている」扱いになり、作り手の値より優先される
		m_settings.opFPSRate = m_fpsRateSelector.GetValue();

		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		Game::UserData& _userData = *_pUserDataResource->pUserData;
		_userData.RefGameSettings() = m_settings;

		if (a_context.pServices && a_context.pServices->pMainEngine)
		{
			_userData.ApplyGameSettings(*a_context.pServices->pMainEngine);
		}
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void GameOptionPanel::ArchiveItems(Engine::Persistence::Archive& a_ar)
	{
		ArchiveItemUI(a_ar, "PerformanceHeader", m_performanceHeader);
		ArchiveDropdown(a_ar, "FPSRate", m_fpsRateSelector.RefUI());
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void GameOptionPanel::DrawItemsInspector(Engine::GameObject::ObjectContext& a_context)
	{
		DrawItemUIInspector(a_context, "PerformanceHeader", m_performanceHeader);
		Engine::EditorField::Tooltip("見出し(文字だけの UI)。未設定なら行を取らない");
		DrawDropdownInspector(a_context, "FPSRate", m_fpsRateSelector.RefUI());

		const std::string _name = m_settings.opFPSRate
			? m_fpsRateSelector.GetName(m_settings.opFPSRate.value())
			: std::string("(作り手の値)");
		Engine::EditorField::Value("FPSRate", "%s", _name.c_str());
	}
}
