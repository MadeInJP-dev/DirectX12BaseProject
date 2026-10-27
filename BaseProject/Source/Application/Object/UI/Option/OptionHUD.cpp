#include "OptionHUD.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Scene/SceneManager/SceneManager.h"
#include "Engine/EditorField/EditorField.h"

#include "Application/InstanceResource/UserDataResource.h"
#include "Application/Game/UserData/UserData.h"

#include "Application/Object/UI/Option/GameOptionPanel/GameOptionPanel.h"
#include "Application/Object/UI/Option/GraphicsOptionPanel/GraphicsOptionPanel.h"
#include "Application/Object/UI/Option/SoundOptionPanel/SoundOptionPanel.h"
#include "Application/Object/UI/Option/InputOptionPanel/InputOptionPanel.h"

namespace App::Object
{
	namespace
	{
		// 配置の基準にした画面の大きさ(px)。アンカーの初期位置は画面の真ん中
		constexpr float BASE_SCREEN_WIDTH = 1920.0f;
		constexpr float BASE_SCREEN_HEIGHT = 1080.0f;

		// 画面全体の枠の大きさ(px)。上下左右に 60px ずつ空ける
		constexpr float FRAME_WIDTH = 1800.0f;
		constexpr float FRAME_HEIGHT = 960.0f;
	}

	OptionHUD::OptionHUD() : UIBase(false)
	{
		// 並びはタブの並び(上から)と保存の順になる。足すときは末尾へ
		m_upPanelVec.push_back(std::make_unique<GameOptionPanel>());
		m_upPanelVec.push_back(std::make_unique<GraphicsOptionPanel>());
		m_upPanelVec.push_back(std::make_unique<SoundOptionPanel>());
		m_upPanelVec.push_back(std::make_unique<InputOptionPanel>());

		m_tabButtonGUIDVec.resize(m_upPanelVec.size());
		m_pSelectPanel = m_upPanelVec.front().get();

		//------------------------------------------------------------------
		// 初期の配置と背景
		//
		// 置いた直後からクラスの冒頭の図の形になるようにしておく。
		// 保存されたシーンを読むときは、ここで入れた値も飾りも読んだもので置き換わる
		//------------------------------------------------------------------
		m_anchor.pixelPos = { BASE_SCREEN_WIDTH * 0.5f, BASE_SCREEN_HEIGHT * 0.5f };
		m_anchor.pixelSize = { FRAME_WIDTH, FRAME_HEIGHT };
		m_anchor.editSize = m_anchor.pixelSize;

		Decoration::Decoration& _frame = AddDecoration(Decoration::EDecorationType::Polygon);
		_frame.name = "Frame";
		_frame.pixelSize = { FRAME_WIDTH, FRAME_HEIGHT };
		_frame.color = Math::Color(0.05f, 0.05f, 0.08f, 0.85f);
		if (Decoration::QuadStyle* _pQuad = _frame.RefQuad())
		{
			_pQuad->edgePixel = 4.0f;
			_pQuad->edgeColor = Math::Color(0.9f, 0.15f, 0.2f, 1.0f);
		}
	}

	//======================================================================================
	// 更新前 : 並べる
	//--------------------------------------------------------------------------------------
	// 項目の当たり判定(各 UI の PreUpdate)より先に位置を決めたいので、ここで並べる。
	// 出ていないときは全部隠れているので並べない
	//======================================================================================
	void OptionHUD::PreUpdate(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::PreUpdate(a_context);

		if (!IsVisibleInHierarchy(a_context)) return;

		PlaceButtons(a_context);

		if (m_pSelectPanel) m_pSelectPanel->Layout(a_context, MakePanelLayout());
	}

	void OptionHUD::PlaceButtons(Engine::GameObject::ObjectContext& a_context)
	{
		// 並べる先は画面の座標で決める(項目側が親から見た値へ直す)
		const Math::Vector2 _origin = GetWorldPixelPos();

		// 戻る : 左下
		if (UIBase* _pBack = ScrollPanelBase::FindUI(a_context, m_backButtonGUID))
		{
			ScrollPanelBase::PlaceUIRect(*_pBack, _origin + m_backButtonOffset, m_backButtonSize);
		}

		// タブ : 左上から縦に並べる。並びはパネルの並びと同じ
		const float _tabStep = m_tabSize.y + m_tabSpacing;
		for (size_t _i = 0; _i < m_tabButtonGUIDVec.size(); ++_i)
		{
			UIBase* _pTab = ScrollPanelBase::FindUI(a_context, m_tabButtonGUIDVec[_i]);
			if (!_pTab) continue;

			const Math::Vector2 _topLeft = {
				_origin.x + m_tabOffset.x,
				_origin.y + m_tabOffset.y + _tabStep * static_cast<float>(_i)
			};
			ScrollPanelBase::PlaceUIRect(*_pTab, _topLeft, m_tabSize);
		}
	}

	//======================================================================================
	// 更新
	//======================================================================================
	void OptionHUD::Update(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::Update(a_context);

		// 開いた瞬間(シーンを重ねた最初のフレーム)に、各パネルが UserData から値を読み直す。
		// 自分の表示で見ているので、エディターで出し直したときも読み直される
		const bool _isOpen = IsVisibleInHierarchy(a_context);
		if (_isOpen && !m_wasOpen) OnOpen(a_context);
		m_wasOpen = _isOpen;

		ApplyVisibility(a_context, _isOpen);
		if (!_isOpen || m_isClosing) return;

		//==================================================================
		// カーソルの固定を切る
		//------------------------------------------------------------------
		// プレイ中は視点操作のためにカーソルを毎フレーム画面中央へ戻している。
		// そのままではカーソルを動かせず、タブや項目を狙えない
		//==================================================================
		if (m_isReleaseCursorLock && a_context.pServices && a_context.pServices->pInputManager)
		{
			if (a_context.pServices->pInputManager->IsCursorLockActive())
			{
				a_context.pServices->pInputManager->SetCursorCentered(false);
			}
		}

		UpdateButtons(a_context);
		if (m_isClosing) return;

		if (m_pSelectPanel) m_pSelectPanel->Update(a_context);
	}

	void OptionHUD::OnOpen(Engine::GameObject::ObjectContext& a_context)
	{
		for (auto& _upPanel : m_upPanelVec)
		{
			_upPanel->OnOpen(a_context);
		}
	}

	//======================================================================================
	// 閉じる
	//--------------------------------------------------------------------------------------
	// 値は変えた瞬間に UserData へ入っているので、ここはファイルへ書くだけ。
	// 1つ変えるたびに書くと、感度を連打したときにファイルを書き続けることになる。
	//
	// 外すのは一番上のシーン(Pop)。更新されるのは一番上のシーンだけなので、
	// ここが動いている = 設定画面のシーンが一番上に居る
	//======================================================================================
	void OptionHUD::ReserveClose(Engine::GameObject::ObjectContext& a_context)
	{
		if (m_isClosing) return;
		m_isClosing = true;

		auto* _pUserDataResource = InstanceResource::UserDataResource::Find(a_context.pWorld);
		if (_pUserDataResource && _pUserDataResource->pUserData)
		{
			_pUserDataResource->pUserData->Save();
		}

		if (a_context.pServices && a_context.pServices->pSceneManager)
		{
			a_context.pServices->pSceneManager->ReserveChangeScene(
				Core::DEFAULT_GUID, Engine::Scene::ESceneChangeType::Pop);
		}
	}

	void OptionHUD::UpdateButtons(Engine::GameObject::ObjectContext& a_context)
	{
		// 閉じる : 戻るボタンか閉じるキー。
		// パネルがキーを受け取っている最中(割り当ての待ち受け)は、キーのほうは見ない
		UIBase* _pBack = ScrollPanelBase::FindUI(a_context, m_backButtonGUID);
		const bool _isBackClicked = _pBack && _pBack->IsClicked();

		bool _isClosePressed = false;
		const bool _isCapturing = m_pSelectPanel && m_pSelectPanel->IsCapturingInput();
		if (!_isCapturing && a_context.pServices && a_context.pServices->pInputManager)
		{
			_isClosePressed = a_context.pServices->pInputManager->IsPress(m_closeAction);
		}

		if (_isBackClicked || _isClosePressed)
		{
			ReserveClose(a_context);
			return;
		}

		// タブ : 押されたパネルへ切り替える。
		// キー・パッドでは選んだだけで切り替える(右で、出ているそのパネルの行へ移れるように)
		for (size_t _i = 0; _i < m_tabButtonGUIDVec.size(); ++_i)
		{
			UIBase* _pTab = ScrollPanelBase::FindUI(a_context, m_tabButtonGUIDVec[_i]);
			if (_pTab && (_pTab->IsClicked() || _pTab->IsNavFocused()))
			{
				SelectPanel(_i);
				break;
			}
		}
	}

	void OptionHUD::ApplyVisibility(Engine::GameObject::ObjectContext& a_context, bool a_isOpen)
	{
		auto _setVisible = [&](const Core::GUID& a_guid, bool a_isVisible)
			{
				if (UIBase* _pUI = ScrollPanelBase::FindUI(a_context, a_guid)) _pUI->SetVisible(a_isVisible);
			};

		_setVisible(m_backButtonGUID, a_isOpen);
		for (const Core::GUID& _tabGUID : m_tabButtonGUIDVec)
		{
			_setVisible(_tabGUID, a_isOpen);
		}

		// パネルは選んでいる1つだけ出す
		for (auto& _upPanel : m_upPanelVec)
		{
			_upPanel->SetVisible(a_context, a_isOpen && _upPanel.get() == m_pSelectPanel);
		}
	}

	//======================================================================================
	// パネル
	//======================================================================================
	void OptionHUD::SelectPanel(size_t a_index)
	{
		if (a_index >= m_upPanelVec.size()) return;
		m_pSelectPanel = m_upPanelVec[a_index].get();
	}

	int OptionHUD::GetSelectIndex() const
	{
		for (size_t _i = 0; _i < m_upPanelVec.size(); ++_i)
		{
			if (m_upPanelVec[_i].get() == m_pSelectPanel) return static_cast<int>(_i);
		}
		return -1;
	}

	PanelLayout OptionHUD::MakePanelLayout() const
	{
		// アンカーの位置を基準にする : OptionHUD を動かせば範囲も一緒に動く。
		// 回転・倍率は掛けない(切り抜きが軸に沿った矩形しか扱えないため)
		PanelLayout _layout = {};
		_layout.pos = GetWorldPixelPos() + m_panelOffset;
		_layout.size = m_panelSize;
		_layout.itemSize = m_itemSize;
		_layout.itemSpacing = m_itemSpacing;
		return _layout;
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void OptionHUD::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// 飾り・アンカーなどの共通ぶん
		UIBase::Archive(a_ar, a_context);

		// ここから下は OptionHUD のぶん。足すときはこの区切りの末尾へ
		Engine::Persistence::ArchiveSection _section(a_ar, "OptionHUD");

		// ボタン・入力
		a_ar.GUIDField("BackButtonGUID", m_backButtonGUID);
		a_ar.GUIDVectorField("TabButtonGUIDs", m_tabButtonGUIDVec);
		Game::ActionField(a_ar, "CloseActionName", m_closeAction);
		a_ar.Field("IsReleaseCursorLock", m_isReleaseCursorLock);

		// 配置
		a_ar.Field("TabOffset", m_tabOffset);
		a_ar.Field("TabSize", m_tabSize);
		a_ar.Field("TabSpacing", m_tabSpacing);
		a_ar.Field("BackButtonOffset", m_backButtonOffset);
		a_ar.Field("BackButtonSize", m_backButtonSize);
		a_ar.Field("PanelOffset", m_panelOffset);
		a_ar.Field("PanelSize", m_panelSize);
		a_ar.Field("ItemSize", m_itemSize);
		a_ar.Field("ItemSpacing", m_itemSpacing);

		// タブはパネルと同じ並び。パネルを足す前のデータを読んでも添え字がずれないよう長さを合わせる
		m_tabButtonGUIDVec.resize(m_upPanelVec.size());

		// パネルごとにまとめる(JSON ではパネルの名前のグループになる)
		for (auto& _upPanel : m_upPanelVec)
		{
			if (!a_ar.BeginGroup(_upPanel->GetPanelName())) continue;
			_upPanel->Archive(a_ar);
			a_ar.EndGroup();
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void OptionHUD::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIBase::DrawInspector(a_context);

		Engine::EditorField::Header("Option");
		Engine::EditorField::HelpText("設定画面の専用シーンに1つだけ置く。開く側が Push し、戻るで Pop する");

		ScrollPanelBase::DrawUIPicker(a_context, "BackButton", m_backButtonGUID);
		Engine::EditorField::Tooltip("押すと保存して設定画面のシーンを外す(UIButton)");

		Engine::EditorField::Field("Close Action", m_closeAction);
		Engine::EditorField::Tooltip("これを押しても閉じる");
		Engine::EditorField::Field("ReleaseCursorLock", m_isReleaseCursorLock);
		Engine::EditorField::Tooltip("開いている間はカーソルの中央固定を切る(閉じても戻さない)");

		//------------------------------------------------------------------
		// 配置 : すべてアンカーの位置からのずれ(px, 基準は 1920x1080)
		//------------------------------------------------------------------
		if (Engine::EditorField::TreeScope _tree{ "Layout", true })
		{
			Engine::EditorField::Field("TabOffset", m_tabOffset, 1.0f);
			Engine::EditorField::Tooltip("一番上のタブの左上");
			Engine::EditorField::Field("TabSize", m_tabSize, 1.0f, 0.0f, 4096.0f);
			Engine::EditorField::Field("TabSpacing", m_tabSpacing, 0.5f, 0.0f, 512.0f);

			Engine::EditorField::Field("BackButtonOffset", m_backButtonOffset, 1.0f);
			Engine::EditorField::Tooltip("戻るボタンの左上");
			Engine::EditorField::Field("BackButtonSize", m_backButtonSize, 1.0f, 0.0f, 4096.0f);

			Engine::EditorField::Field("PanelOffset", m_panelOffset, 1.0f);
			Engine::EditorField::Tooltip("パネルの描画範囲の左上");
			Engine::EditorField::Field("PanelSize", m_panelSize, 1.0f, 0.0f, 8192.0f);
			Engine::EditorField::Tooltip("パネルの描画範囲の大きさ。右端の BarWidth ぶんはスクロールバーが使う");

			Engine::EditorField::Field("ItemSize", m_itemSize, 1.0f, 0.0f, 8192.0f);
			Engine::EditorField::Tooltip("項目1つの大きさ。行の送りと、項目のピボットのずらしに使う");
			Engine::EditorField::Field("ItemSpacing", m_itemSpacing, 0.5f, 0.0f, 512.0f);
		}

		//------------------------------------------------------------------
		// パネル
		//------------------------------------------------------------------
		// 選んでいるパネル。エディターでは中身を触る・並びを確かめるために切り替える
		std::vector<const char*> _nameVec;
		_nameVec.reserve(m_upPanelVec.size());
		for (const auto& _upPanel : m_upPanelVec) _nameVec.push_back(_upPanel->GetPanelName());

		int _selectIndex = GetSelectIndex();
		if (Engine::EditorField::Combo("SelectPanel", _selectIndex, _nameVec.data(), static_cast<int>(_nameVec.size())))
		{
			if (_selectIndex >= 0) SelectPanel(static_cast<size_t>(_selectIndex));
		}

		for (size_t _i = 0; _i < m_upPanelVec.size(); ++_i)
		{
			ScrollPanelBase& _panel = *m_upPanelVec[_i];

			Engine::EditorField::IDScope _id(_panel.GetPanelName());
			if (!Engine::EditorField::CollapsingHeader(_panel.GetPanelName())) continue;

			ScrollPanelBase::DrawUIPicker(a_context, "TabButton", m_tabButtonGUIDVec[_i]);
			Engine::EditorField::Tooltip("押すとこのパネルへ切り替える(UIButton)。位置と大きさは Layout のタブの値で決まる");

			_panel.DrawInspector(a_context);
		}
	}
}
