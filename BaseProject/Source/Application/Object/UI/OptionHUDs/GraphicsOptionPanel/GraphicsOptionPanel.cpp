#include "GraphicsOptionPanel.h"

#include "Engine/GameObject/BaseObject/BaseObject.h"	// ObjectContext
#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Window/NativeWindow.h"
#include "Engine/EditorField/EditorField.h"

#include "Application/InstanceResource/UserDataResource.h"

namespace App::Object
{
	namespace
	{
		const char* ToOnOff(bool a_isOn) { return a_isOn ? "On" : "Off"; }

		// 刻みで動かして範囲に詰める
		uint32_t StepValue(uint32_t a_value, int a_step, uint32_t a_stepSize, uint32_t a_min, uint32_t a_max)
		{
			const int _value = static_cast<int>(a_value) + a_step * static_cast<int>(a_stepSize);
			return static_cast<uint32_t>(std::clamp(_value, static_cast<int>(a_min), static_cast<int>(a_max)));
		}
	}

	void GraphicsOptionPanel::OnOpen(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		m_settings = _pUserDataResource->pUserData->GetGraphicsSettings();

		// 開いたまま閉じられていても、開き直したら閉じた状態から始める
		m_windowModeSelector.Close();
	}

	Engine::EWindowMode GraphicsOptionPanel::GetCurrentWindowMode(Engine::GameObject::ObjectContext& a_context) const
	{
		if (m_settings.opWindowMode) return m_settings.opWindowMode.value();

		// 選ばれていなければ、作り手の値で開いたウィンドウの今の状態
		if (a_context.pServices && a_context.pServices->pMainEngine)
		{
			if (const auto* _pWindow = a_context.pServices->pMainEngine->GetNativeWindow())
			{
				return _pWindow->GetWindowMode();
			}
		}
		return Engine::EWindowMode::Windowed;
	}

	//======================================================================================
	// 並べる
	//======================================================================================
	void GraphicsOptionPanel::LayoutItems(Engine::GameObject::ObjectContext& a_context)
	{
		PlaceHeaderRow(a_context, m_displayHeader);
		PlaceDropdown(a_context, m_windowModeSelector.GetUI());
		AdvanceRow();
		PlaceStepper(a_context, m_brightnessStepper);
		AdvanceRow();
		PlaceSelector(a_context, m_bloomSelector);
		AdvanceRow();

		AdvanceSpace(m_sectionSpace);

		PlaceHeaderRow(a_context, m_gameplayHeader);
		PlaceStepper(a_context, m_screenShakeStepper);
		AdvanceRow();
		PlaceSelector(a_context, m_hudSelector);
		AdvanceRow();
	}

	//======================================================================================
	// 操作
	//======================================================================================
	void GraphicsOptionPanel::UpdateItems(Engine::GameObject::ObjectContext& a_context)
	{
		bool _isChanged = false;

		// ---- ウィンドウモード : 選んだ時点で「選ばれている」扱いになる ----
		if (m_windowModeSelector.Update(a_context, GetCurrentWindowMode(a_context)))
		{
			m_settings.opWindowMode = m_windowModeSelector.GetValue();
			_isChanged = true;
		}

		// ---- 明るさ ----
		if (const int _step = UpdateStepper(a_context, m_brightnessStepper); _step != 0)
		{
			m_settings.brightness = StepValue(m_settings.brightness, _step, BRIGHTNESS_STEP,
				Game::UserData::BRIGHTNESS_MIN, Game::UserData::BRIGHTNESS_MAX);
			_isChanged = true;
		}
		SetStepperValue(a_context, m_brightnessStepper,
			static_cast<float>(m_settings.brightness), static_cast<float>(Game::UserData::BRIGHTNESS_MAX));

		// ---- ブルーム ----
		if (UpdateSelector(a_context, m_bloomSelector) != 0)
		{
			m_settings.isBloomEnable = !m_settings.isBloomEnable;
			_isChanged = true;
		}
		SetSelectorText(a_context, m_bloomSelector, ToOnOff(m_settings.isBloomEnable));

		// ---- 画面の揺れ ----
		if (const int _step = UpdateStepper(a_context, m_screenShakeStepper); _step != 0)
		{
			m_settings.screenShake = StepValue(m_settings.screenShake, _step, SCREEN_SHAKE_STEP,
				0, Game::UserData::SCREEN_SHAKE_MAX);
			_isChanged = true;
		}
		SetStepperValue(a_context, m_screenShakeStepper,
			static_cast<float>(m_settings.screenShake), static_cast<float>(Game::UserData::SCREEN_SHAKE_MAX));

		// ---- HUD ----
		if (UpdateSelector(a_context, m_hudSelector) != 0)
		{
			m_settings.isHUDVisible = !m_settings.isHUDVisible;
			_isChanged = true;
		}
		SetSelectorText(a_context, m_hudSelector, ToOnOff(m_settings.isHUDVisible));

		if (_isChanged) ApplySettings(a_context);
	}

	void GraphicsOptionPanel::ApplySettings(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pUserDataResource = FindUserData(a_context);
		if (!_pUserDataResource || !_pUserDataResource->pUserData) return;

		Game::UserData& _userData = *_pUserDataResource->pUserData;
		_userData.RefGraphicsSettings() = m_settings;

		// その場で効かせる。HUD と揺れは使う側が毎フレーム UserData を読む
		if (a_context.pServices && a_context.pServices->pMainEngine)
		{
			_userData.ApplyGraphicsSettings(*a_context.pServices->pMainEngine);
		}
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void GraphicsOptionPanel::ArchiveItems(Engine::Persistence::Archive& a_ar)
	{
		ArchiveItemUI(a_ar, "DisplayHeader", m_displayHeader);
		ArchiveDropdown(a_ar, "WindowMode", m_windowModeSelector.RefUI());
		ArchiveStepper(a_ar, "Brightness", m_brightnessStepper);
		ArchiveSelector(a_ar, "Bloom", m_bloomSelector);

		a_ar.Field("SectionSpace", m_sectionSpace);

		ArchiveItemUI(a_ar, "GameplayHeader", m_gameplayHeader);
		ArchiveStepper(a_ar, "ScreenShake", m_screenShakeStepper);
		ArchiveSelector(a_ar, "HUD", m_hudSelector);
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void GraphicsOptionPanel::DrawItemsInspector(Engine::GameObject::ObjectContext& a_context)
	{
		if (Engine::EditorField::TreeScope _tree{ "Display", true })
		{
			DrawItemUIInspector(a_context, "DisplayHeader", m_displayHeader);
			Engine::EditorField::Tooltip("見出し(文字だけの UI)。未設定なら行を取らない");
			DrawDropdownInspector(a_context, "WindowMode", m_windowModeSelector.RefUI());
			DrawStepperInspector(a_context, "Brightness", m_brightnessStepper);
			DrawSelectorInspector(a_context, "Bloom", m_bloomSelector);
		}

		Engine::EditorField::Field("SectionSpace", m_sectionSpace, 0.5f, 0.0f, 512.0f);
		Engine::EditorField::Tooltip("画面と遊びの間(px)");

		if (Engine::EditorField::TreeScope _tree{ "Gameplay", true })
		{
			DrawItemUIInspector(a_context, "GameplayHeader", m_gameplayHeader);
			Engine::EditorField::Tooltip("見出し(文字だけの UI)。未設定なら行を取らない");
			DrawStepperInspector(a_context, "ScreenShake", m_screenShakeStepper);
			DrawSelectorInspector(a_context, "HUD", m_hudSelector);
		}

		// 写しの今の値(表示のみ)
		Engine::EditorField::Value("Brightness", "%u%%", m_settings.brightness);
		Engine::EditorField::Value("Bloom", "%s", ToOnOff(m_settings.isBloomEnable));
		Engine::EditorField::Value("ScreenShake", "%u%%", m_settings.screenShake);
		Engine::EditorField::Value("HUD", "%s", ToOnOff(m_settings.isHUDVisible));
	}
}
