#include "SideMenu.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/EditorField/EditorField.h"

#include "../../ObjectPicker.h"
#include "../UIBase.h"
#include "../UIPanel/UIPanel.h"

namespace App::Object
{
	namespace
	{
		// a_pObject が a_rootGUID の下(子・孫)に居るか。自分自身も含む
		bool IsUnder(Engine::GameObject::GameObjectManager& a_objectManager, const Engine::GameObject::BaseObject* a_pObject, const Core::GUID& a_rootGUID)
		{
			// 親子が輪になっていても止まるよう、辿る深さに上限を設ける(UIBase::IsVisibleInHierarchy と同じ)
			constexpr int DEPTH_MAX = 32;

			const Engine::GameObject::BaseObject* _pCurrent = a_pObject;
			for (int _depth = 0; _depth < DEPTH_MAX && _pCurrent; ++_depth)
			{
				if (_pCurrent->GetGUID() == a_rootGUID) return true;

				const Core::GUID& _parentGUID = _pCurrent->GetParentGUID();
				if (!_parentGUID.IsValid()) return false;
				_pCurrent = a_objectManager.FindByGUID(_parentGUID);
			}
			return false;
		}
	}

	//======================================================================================
	// 更新 : カーソルの位置を見て開閉し、開き具合を進める
	//--------------------------------------------------------------------------------------
	// 「乗っている(IsHovered)」ではなく矩形の中かどうかで見る。
	// IsHovered は手前の1つにしか立たないので、メニューのボタンの上に居ると
	// 背景(Area)は乗っていない扱いになり、ボタンへ動かしただけで閉じてしまう
	//======================================================================================
	void SideMenu::Update(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;
		auto* _pInputManager = (a_context.pServices) ? a_context.pServices->pInputManager : nullptr;

		// プレイ中以外(エディター操作中・文字入力中)はカーソルを見ずに閉じておく
		const bool _isInputEnable = _pInputManager && _pInputManager->IsGameInputEnable();

		Math::Vector2 _cursorPos = {};
		if (_isInputEnable && _pObjectManager && UIInteraction::CalcCursorUIPos(a_context, _cursorPos))
		{
			const UIBase* _pTrigger = Picker::Find<UIBase>(_pObjectManager, m_triggerGUID);
			const UIBase* _pArea = Picker::Find<UIBase>(_pObjectManager, m_areaGUID);

			const bool _isOnTrigger = IsCursorInside(a_context, _pTrigger, _cursorPos);

			if (!m_isOpen)
			{
				// 閉じている : 開き口に乗ったら開く
				m_isOpen = _isOnTrigger;
			}
			else
			{
				// 開いている : 開き口とメニューの範囲の両方から出たら閉じる。
				// 範囲は滑っている途中なら今の位置(ずらし込み)で見る
				const bool _isOnArea = IsCursorInside(a_context, _pArea, _cursorPos);
				m_isOpen = _isOnTrigger || _isOnArea;
			}
		}
		else if (!_isInputEnable)
		{
			m_isOpen = false;
		}

		//------------------------------------------------------------------
		// 開き具合を目指す側へ進める(滑り込む/滑り出る)
		//------------------------------------------------------------------
		const float _target = (m_isOpen || m_isPreviewOpen) ? 1.0f : 0.0f;
		if (m_isPreviewOpen || m_slideTime <= 0.0f)
		{
			// エディターで配置を見ているとき・時間が 0 のときは一瞬で
			m_openRate = _target;
		}
		else
		{
			const float _step = a_context.dt / m_slideTime;
			m_openRate = (_target > m_openRate)
				? std::min(m_openRate + _step, _target)
				: std::max(m_openRate - _step, _target);
		}

		ApplySlide(a_context);
	}

	bool SideMenu::IsCursorInside(Engine::GameObject::ObjectContext& a_context, const UIBase* a_pUI, const Math::Vector2& a_cursorPos)
	{
		if (!a_pUI) return false;
		if (!a_pUI->IsVisibleInHierarchy(a_context)) return false;

		const UIAnchor& _anchor = a_pUI->GetAnchor();
		if (!_anchor.HasSize()) return false;

		return UIAnchor::IsPointInside(
			a_cursorPos,
			_anchor.pixelPos + a_pUI->GetRuntimeOffset(),
			_anchor.pixelSize * _anchor.scale,
			_anchor.pivot,
			_anchor.rotation);
	}

	//======================================================================================
	// 開き具合を表示と位置へ反映する
	//--------------------------------------------------------------------------------------
	// 入れ物の下の UI 全部へ同じずらしを掛ける。置き場所(PixelPos)は開いた形のまま。
	// ずらしは保存されないので、滑っている途中でシーンを保存しても配置は崩れない。
	//
	// 動きは出始めが速く、止まり際がゆっくり(OutCubic)。閉じるときは同じ曲線を逆にたどる
	//======================================================================================
	void SideMenu::ApplySlide(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;
		if (!_pObjectManager) return;

		UIBase* _pPanel = Picker::Find<UIBase>(_pObjectManager, m_panelGUID);
		if (!_pPanel) return;

		// 隠れきったら出さない。中のボタンも描かれず押せなくなる(UIPanel の下に置いてあれば)
		_pPanel->SetVisible(m_openRate > 0.0f);

		const float _eased = Decoration::Ease(Decoration::EEase::OutCubic, m_openRate);
		const Math::Vector2 _offset = { -(1.0f - _eased) * GetSlideDistance(a_context), 0.0f };

		for (const auto& _upObject : _pObjectManager->GetObjects())
		{
			UIBase* _pUI = Core::TypeInfo::Cast<UIBase>(_upObject.get());
			if (!_pUI) continue;
			if (!IsUnder(*_pObjectManager, _pUI, m_panelGUID)) continue;

			_pUI->SetRuntimeOffset(_offset);
		}
	}

	float SideMenu::GetSlideDistance(Engine::GameObject::ObjectContext& a_context) const
	{
		if (m_slideDistance > 0.0f) return m_slideDistance;

		// 指定が無ければ Area の幅ぶん : 左端に置いた背景が画面の外から出てくる
		const UIBase* _pArea = Picker::Find<UIBase>(a_context.pObjectManager, m_areaGUID);
		if (!_pArea) return 0.0f;

		const UIAnchor& _anchor = _pArea->GetAnchor();
		return _anchor.pixelSize.x * _anchor.scale;
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void SideMenu::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		a_ar.GUIDField("TriggerGUID", m_triggerGUID);
		a_ar.GUIDField("PanelGUID", m_panelGUID);
		a_ar.GUIDField("AreaGUID", m_areaGUID);

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		// 区切りを持たない古い .ob* には無いので読まない
		if (!a_ar.IsLegacyLayout())
		{
			a_ar.Field("SlideTime", m_slideTime);
			a_ar.Field("SlideDistance", m_slideDistance);
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void SideMenu::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;

		Engine::EditorField::Header("SideMenu");
		Engine::EditorField::HelpText("Trigger に乗ると Panel が横から滑り込み、Trigger と Area の両方から出たら滑り出て隠れる");

		Picker::DrawCombo<UIBase>("Trigger", _pObjectManager, m_triggerGUID);
		Engine::EditorField::Tooltip("開き口(三本線のボタンなど)。アンカーの PixelSize が範囲になる");

		Picker::DrawCombo<UIBase>("Panel", _pObjectManager, m_panelGUID);
		Engine::EditorField::Tooltip("メニューの入れ物(UIPanel)。中のボタンと Area はヒエラルキーでこの下に置く(下に置いたものが一緒に滑る)");
		if (UIBase* _pPanel = Picker::Find<UIBase>(_pObjectManager, m_panelGUID))
		{
			if (!Core::TypeInfo::Cast<UIPanel>(_pPanel))
			{
				Engine::EditorField::WarningText("UIPanel ではないので、下に置いた UI へ表示が伝わりません");
			}
		}

		Picker::DrawCombo<UIBase>("Area", _pObjectManager, m_areaGUID);
		Engine::EditorField::Tooltip("開いている間、カーソルが居れば閉じない範囲。裏のボタンを塞ぐため押せる UI(UIImage)にする");

		Engine::EditorField::Field("SlideTime", m_slideTime, 0.01f, 0.0f, 2.0f);
		Engine::EditorField::Tooltip("滑り込む/滑り出るのにかかる時間(秒)。0 で一瞬");
		Engine::EditorField::Field("SlideDistance", m_slideDistance, 1.0f, 0.0f, 4096.0f);
		Engine::EditorField::Tooltip("滑る距離(px)。0 なら Area の幅(画面の左端の外から出てくる)");

		// エディターでは乗せて開けないので、ここで開いて配置を見る(保存はされない)
		Engine::EditorField::Field("PreviewOpen", m_isPreviewOpen);
		Engine::EditorField::Tooltip("配置を見るために開いておく(保存はされない)");

		Engine::EditorField::Value("Open", "%s (%.2f)", m_isOpen ? "yes" : "no", m_openRate);
	}
}
