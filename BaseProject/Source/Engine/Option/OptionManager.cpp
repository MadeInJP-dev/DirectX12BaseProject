#include "OptionManager.h"

namespace Engine::Option
{
	OptionManager::OptionManager(){}
	void OptionManager::Init()
	{
		m_pOptionList.clear();
		m_pOptionList.push_back(&m_windowOption);
		m_pOptionList.push_back(&m_renderingOption);

		m_pOptionList.push_back(&m_buildConfig);
		m_pOptionList.push_back(&m_inputOption);
		m_pOptionList.push_back(&m_cursorOption);

		m_pOptionList.push_back(&m_debugDrawOption);
	}
	void OptionManager::Serialize()
	{
		Persistence::Archive _archive(Persistence::Archive::EMode::Save, "Asset/Data/Engine", "EngineData", "optn");
		Archive(_archive);
	}
	//======================================================================================
	// エンジン設定の読み込み
	//
	// ここだけは形式を JSON で固定している。ビルドモード(Development / Shipping)を
	// 決めているのがこのファイル自身で、読む時点ではまだモードが分からないため。
	// Auto にすると「モードを決めるためにモードを見る」ことになる。
	//======================================================================================
	void OptionManager::Deserialize()
	{
		Persistence::Archive _archive(
			Persistence::Archive::EMode::Load, 
			"Asset/Data/Engine",
			"EngineData", 
			"optn",
			Persistence::Archive::EArchiveFormat::Json
		);
		Archive(_archive);
	}
	void Engine::Option::OptionManager::DrawEdit(const ECS::EngineServices& a_services)
	{
		for (auto* _pOption : m_pOptionList)
		{
			Engine::EditorField::Line();
			if (Engine::EditorField::TreeScope _tree{ _pOption->GetName().c_str(), false, true })
			{
				_pOption->DrawEdit(a_services);
			}
		}
	}
	void OptionManager::Archive(Persistence::Archive& a_ar)
	{
		for (auto* _pOption : m_pOptionList)
		{
			if (a_ar.BeginGroup(_pOption->GetName().c_str()))
			{
				_pOption->Archive(a_ar);
				a_ar.EndGroup();
			}
		}
	}
}
