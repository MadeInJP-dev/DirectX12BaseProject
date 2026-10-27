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

		// 整数(%)の設定を数値の行で動かす : 動かしたら整数に丸めて書き、true を返す
		bool UpdatePercent(Engine::GameObject::ObjectContext& a_context, NumericMutate& a_numeric,
			uint32_t& a_inoutValue, uint32_t a_min, uint32_t a_max, uint32_t a_step)
		{
			float _value = static_cast<float>(a_inoutValue);
			if (!a_numeric.Update(a_context, _value,
				static_cast<float>(a_min), static_cast<float>(a_max), static_cast<float>(a_step))) return false;

			const uint32_t _rounded = static_cast<uint32_t>(std::lround(_value));
			if (_rounded == a_inoutValue) return false;	// 掴んで動かしても、整数が変わるまでは書かない

			a_inoutValue = _rounded;
			return true;
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
		PlaceNumeric(a_context, m_brightnessNumeric);
		AdvanceRow();
		PlaceSelector(a_context, m_bloomSelector);
		AdvanceRow();

		AdvanceSpace(m_sectionSpace);

		PlaceHeaderRow(a_context, m_gameplayHeader);
		PlaceNumeric(a_context, m_screenShakeNumeric);
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
		if (UpdatePercent(a_context, m_brightnessNumeric, m_settings.brightness,
			Game::UserData::BRIGHTNESS_MIN, Game::UserData::BRIGHTNESS_MAX, BRIGHTNESS_STEP))
		{
			_isChanged = true;
		}

		// ---- ブルーム ----
		if (UpdateSelector(a_context, m_bloomSelector) != 0)
		{
			m_settings.isBloomEnable = !m_settings.isBloomEnable;
			_isChanged = true;
		}
		SetSelectorText(a_context, m_bloomSelector, ToOnOff(m_settings.isBloomEnable));

		// ---- 画面の揺れ ----
		if (UpdatePercent(a_context, m_screenShakeNumeric, m_settings.screenShake,
			0, Game::UserData::SCREEN_SHAKE_MAX, SCREEN_SHAKE_STEP))
		{
			_isChanged = true;
		}

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
		m_brightnessNumeric.Archive(a_ar, "Brightness");
		ArchiveSelector(a_ar, "Bloom", m_bloomSelector);

		a_ar.Field("SectionSpace", m_sectionSpace);

		ArchiveItemUI(a_ar, "GameplayHeader", m_gameplayHeader);
		m_screenShakeNumeric.Archive(a_ar, "ScreenShake");
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
			m_brightnessNumeric.DrawInspector(a_context, "Brightness");
			DrawSelectorInspector(a_context, "Bloom", m_bloomSelector);
		}

		Engine::EditorField::Field("SectionSpace", m_sectionSpace, 0.5f, 0.0f, 512.0f);
		Engine::EditorField::Tooltip("画面と遊びの間(px)");

		if (Engine::EditorField::TreeScope _tree{ "Gameplay", true })
		{
			DrawItemUIInspector(a_context, "GameplayHeader", m_gameplayHeader);
			Engine::EditorField::Tooltip("見出し(文字だけの UI)。未設定なら行を取らない");
			m_screenShakeNumeric.DrawInspector(a_context, "ScreenShake");
			DrawSelectorInspector(a_context, "HUD", m_hudSelector);
		}

		// 写しの今の値(表示のみ)
		Engine::EditorField::Value("Brightness", "%u%%", m_settings.brightness);
		Engine::EditorField::Value("Bloom", "%s", ToOnOff(m_settings.isBloomEnable));
		Engine::EditorField::Value("ScreenShake", "%u%%", m_settings.screenShake);
		Engine::EditorField::Value("HUD", "%s", ToOnOff(m_settings.isHUDVisible));
	}
}
