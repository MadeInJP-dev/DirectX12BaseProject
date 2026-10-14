#include "LoadingSequence.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/EditorField/EditorField.h"

#include "Application/Object/UI/Widget/UIGauge/UIGauge.h"

//==========================================================================================
// LoadingSequence
//
// 読み込みの進み具合(SceneManager::GetLoadProgress)をゲージへ流すだけの進行役。
//
// ・ゲージは GUID で引く(TitleSequence のボタンと同じ)
//     ポインタを持つとシーンの読み込み順に縛られるうえ、保存もできない。
// ・毎フレーム引き直す
//     ロード画面は出し入れされるだけで作り直されないので、引き直しても安い
//     (GameObjectManager::FindByGUID は対応表を引くだけ)。
//==========================================================================================
namespace App::Object
{
	void LoadingSequence::Update(Engine::GameObject::ObjectContext& a_context)
	{
		if (a_context.pServices && a_context.pServices->pSceneManager)
		{
			m_progress = a_context.pServices->pSceneManager->GetLoadProgress();
		}

		if (!a_context.pObjectManager) return;
		if (!m_gaugeGUID.IsValid()) return;

		auto* _pObject = a_context.pObjectManager->FindByGUID(m_gaugeGUID);
		if (!_pObject) return;

		auto* _pGauge = Core::TypeInfo::Cast<UIGauge>(_pObject);
		if (!_pGauge)
		{
			// 設定ミスに気付けるよう一度だけ知らせる
			if (!m_isWarned)
			{
				ENGINE_WARNING("[LoadingSequence] 指定されたGUIDは UIGauge ではありません");
				m_isWarned = true;
			}
			return;
		}

		_pGauge->SetValue(m_progress, 1.0f);
	}

	//======================================================================================
	// シリアライズ
	//======================================================================================
	void LoadingSequence::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		a_ar.GUIDField("GaugeGUID", m_gaugeGUID);
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void LoadingSequence::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Header("Progress Gauge");

		// 同じシーンに置いた UIGauge から選ぶ
		std::string _current = "None";
		if (m_gaugeGUID.IsValid()) _current = m_gaugeGUID.String();

		if (Engine::EditorField::ComboScope _combo{ "Gauge", _current.c_str() })
		{
			if (Engine::EditorField::Selectable("None", !m_gaugeGUID.IsValid()))
			{
				m_gaugeGUID = {};
				m_isWarned = false;
			}

			if (a_context.pObjectManager)
			{
				const auto& _objectVec = a_context.pObjectManager->GetObjects();
				for (size_t _i = 0; _i < _objectVec.size(); ++_i)
				{
					auto* _pGauge = Core::TypeInfo::Cast<UIGauge>(_objectVec[_i].get());
					if (!_pGauge) continue;

					// 同名でもIDがぶつからないようにする
					Engine::EditorField::IDScope _id(static_cast<int>(_i));

					const bool _isSelected = (m_gaugeGUID == _pGauge->GetGUID());
					if (Engine::EditorField::Selectable(_pGauge->GetGUID().String().c_str(), _isSelected))
					{
						m_gaugeGUID = _pGauge->GetGUID();
						m_isWarned = false;
					}
					if (_isSelected) Engine::EditorField::SetItemDefaultFocus();
				}
			}
		}
		Engine::EditorField::Tooltip("ゲージは値の取り元(Source)を Manual にしておくこと");

		// 実行中の状態は表示のみ
		Engine::EditorField::Header("Runtime");
		Engine::EditorField::Value("Progress", "%.2f", m_progress);
	}
}
