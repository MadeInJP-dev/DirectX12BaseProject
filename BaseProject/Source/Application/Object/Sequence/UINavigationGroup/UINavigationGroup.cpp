#include "UINavigationGroup.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/Input/InputDeviceManager.h"		// 最後に触ったデバイス(パッドを触ったら項目移動へ)
#include "Engine/EditorField/EditorField.h"

#include "Application/ECS/World/APPWorld.h"
#include "Application/InstanceResource/UINavigationResource.h"
#include "Application/Object/ObjectPicker.h"
#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	namespace
	{
		// 軸を倒したとみなす量。斜めに倒したときは大きいほうの向きを使う
		constexpr float AXIS_THRESHOLD = 0.5f;

		// 押しっぱなしで繰り返し始めるまでの時間と、その後の間隔(秒)
		constexpr float REPEAT_DELAY = 0.4f;
		constexpr float REPEAT_INTERVAL = 0.12f;

		// カーソルが動いたとみなす距離(UI の px)。机に触れた程度の揺れでは切り替えない
		constexpr float CURSOR_MOVE_THRESHOLD = 2.0f;

		// つながりをたどる上限。輪になっていても、どこかで切れていても止まるように
		constexpr int CHAIN_MAX = 256;

		//==================================================================================
		// 操作の切り替えと移動量を作る(フレームに1回だけ)
		//----------------------------------------------------------------------------------
		// どのグループから見ても同じ値になるよう、ワールドのリソースへ書く。
		// 2つ目以降のグループは、同じフレームなら何もしない。
		//
		//   項目移動へ : 移動(Tab・LB/RB・矢印・スティック・十字キー)・決定を押した / パッドを触り始めた
		//   カーソルへ : カーソルを動かした / マウスでクリックした
		//
		// マウスのクリックは Select がキーボード・マウス側から押されたことで見る
		// (決定と同じキーに割り当てられていれば、決定のほうを優先する)
		//==================================================================================
		void UpdateSharedInput(Engine::GameObject::ObjectContext& a_context, InstanceResource::UINavigationResource& a_nav)
		{
			if (a_nav.inputFrameIndex == a_context.frameIndex) return;
			a_nav.inputFrameIndex = a_context.frameIndex;
			a_nav.step = 0;

			if (!a_context.pServices || !a_context.pServices->pInputManager) return;
			const auto& _input = *a_context.pServices->pInputManager;

			using Engine::Input::EInputDevice;
			const EInputDevice _device = _input.GetInputDeviceManager().GetActiveDevice();

			Math::Vector2 _cursorPos = {};
			const bool _hasCursor = UIInteraction::CalcCursorUIPos(a_context, _cursorPos);

			// 最初のフレーム : パッドで来たなら選択を見せておく。マウスなら見せない
			if (!a_nav.isInitialized)
			{
				a_nav.isInitialized = true;
				a_nav.isNavMode = (_device == EInputDevice::Gamepad);
				a_nav.lastActiveDevice = _device;
				a_nav.lastCursorPos = _cursorPos;
				a_nav.hasLastCursorPos = _hasCursor;
			}

			//------------------------------------------------------------------
			// 移動量
			//
			// 軸は倒した瞬間に1つ動き、倒し続けると少し待ってから繰り返す。
			// 上が正の軸なので、上は前(-1)・下は次(+1)。右は次、左は前
			//------------------------------------------------------------------
			const Math::Vector2 _axis = _input.GetAxisState(Game::EGameAction::BottonMove);

			int _dir = 0;
			if (std::abs(_axis.y) >= std::abs(_axis.x))
			{
				if (std::abs(_axis.y) > AXIS_THRESHOLD) _dir = (_axis.y > 0.0f) ? -1 : 1;
			}
			else if (std::abs(_axis.x) > AXIS_THRESHOLD)
			{
				_dir = (_axis.x > 0.0f) ? 1 : -1;
			}

			if (_dir != a_nav.holdDir)
			{
				a_nav.holdDir = _dir;
				a_nav.repeatTimer = REPEAT_DELAY;
				a_nav.step = _dir;
			}
			else if (_dir != 0)
			{
				a_nav.repeatTimer -= a_context.dt;
				if (a_nav.repeatTimer <= 0.0f)
				{
					a_nav.repeatTimer += REPEAT_INTERVAL;
					a_nav.step = _dir;
				}
			}

			// 押すたびに1つ(押しっぱなしでは繰り返さない)
			if (_input.IsPress(Game::EGameAction::NextItem)) a_nav.step = 1;
			if (_input.IsPress(Game::EGameAction::PrevItem)) a_nav.step = -1;

			// 入力を別の用途に使っている間(キー割り当ての待ち受けなど)は動かさない。
			// 倒している向きは覚えたままにするので、止めが外れても倒しっぱなしで急に動き出さない
			if (a_nav.IsMoveLocked(a_context.frameIndex)) a_nav.step = 0;

			//------------------------------------------------------------------
			// 操作の切り替え
			//------------------------------------------------------------------
			const bool _isDecide = _input.IsPress(Game::EGameAction::Decide);

			const bool _isCursorMoved = _hasCursor && a_nav.hasLastCursorPos
				&& (Math::Vector2::DistanceSquared(_cursorPos, a_nav.lastCursorPos) > CURSOR_MOVE_THRESHOLD * CURSOR_MOVE_THRESHOLD);

			const bool _isMouseClick = (_device == EInputDevice::KeyboardMouse)
				&& _input.IsPress(Game::EGameAction::Select) && !_isDecide;

			const bool _isPadTouched = (_device == EInputDevice::Gamepad)
				&& (a_nav.lastActiveDevice != EInputDevice::Gamepad);

			if (_isCursorMoved || _isMouseClick)
			{
				a_nav.isNavMode = false;
			}
			else if (a_nav.step != 0 || _isDecide || _isPadTouched)
			{
				a_nav.isNavMode = true;
			}

			a_nav.lastCursorPos = _cursorPos;
			a_nav.hasLastCursorPos = _hasCursor;
			a_nav.lastActiveDevice = _device;
		}
	}

	//======================================================================================
	// 更新前 : 入力を見て選択を移し、選んでいる UI を名乗る
	//--------------------------------------------------------------------------------------
	// 選ばれているかを UI が見るのは Update なので、ここで済ませておけば
	// 配列の並び(グループと UI のどちらが先に回るか)に関係なく同じフレームに反映される。
	//
	// 入力で選択を動かすのは、前のフレームに取り合いに勝ったグループだけ。
	// このフレームの勝ち負けは、全員が名乗り終えるまで分からないため
	//======================================================================================
	void UINavigationGroup::PreUpdate(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld || !a_context.pWorld->HasResource<InstanceResource::UINavigationResource>()) return;
		auto& _nav = a_context.pWorld->RefResource<InstanceResource::UINavigationResource>();

		UpdateSharedInput(a_context, _nav);

		UIBase* _pCurrent = ResolveCurrent(a_context);
		m_isAvailable = (_pCurrent != nullptr);

		// 選べる UI が無い(隠れている)間は名乗らない。名乗るとカーソルを止めてしまう
		if (!_pCurrent)
		{
			m_isFocusShown = false;
			return;
		}

		const bool _isActive = _nav.WasOwner(a_context.frameIndex, this);
		if (_isActive)
		{
			if (!_nav.isNavMode)
			{
				// カーソル操作の間 : 乗っている UI を今選んでいるものにしておく。
				// キーへ持ち替えたとき、カーソルで指していたところから動けるように
				if (UIBase* _pHovered = FindHoveredInChain(a_context)) _pCurrent = _pHovered;
			}
			else if (_nav.step != 0 && m_isFocusShown)
			{
				// 選択を見せていなかったフレームからの最初の1回は、見せるだけで動かさない
				// (見えていない場所から、いきなり隣へ飛んだように見えないように)
				if (UIBase* _pNext = FindSelectable(a_context, _pCurrent, _nav.step)) _pCurrent = _pNext;
			}
		}

		m_currentGUID = _pCurrent->GetGUID();
		_nav.Claim(a_context.frameIndex, this, m_priority, _pCurrent);

		m_isFocusShown = _isActive && _nav.isNavMode;
	}

	void UINavigationGroup::SetCurrentGUID(const Core::GUID& a_guid)
	{
		m_currentGUID = a_guid;
		m_isCurrentRequested = true;
	}

	//======================================================================================
	// 今選ぶ UI を決め直す
	//======================================================================================
	UIBase* UINavigationGroup::ResolveCurrent(const Engine::GameObject::ObjectContext& a_context)
	{
		UIBase* _pFirst = Picker::Find<UIBase>(a_context.pObjectManager, m_firstGUID);
		UIBase* _pCurrent = Picker::Find<UIBase>(a_context.pObjectManager, m_currentGUID);

		// 出てきた直後は最初から(外から決めてあればそちら)
		if (!m_isAvailable && !m_isCurrentRequested) _pCurrent = _pFirst;
		m_isCurrentRequested = false;

		if (!_pCurrent) _pCurrent = _pFirst;
		if (IsSelectable(a_context, _pCurrent)) return _pCurrent;

		// 選んでいた UI が選べなくなった : その先へ進める
		if (_pCurrent)
		{
			if (UIBase* _pNext = FindSelectable(a_context, _pCurrent, 1)) return _pNext;
		}

		// つながりの外を指していたときのために、最初からも探す
		if (_pFirst && _pFirst != _pCurrent)
		{
			if (IsSelectable(a_context, _pFirst)) return _pFirst;
			return FindSelectable(a_context, _pFirst, 1);
		}

		return nullptr;
	}

	//======================================================================================
	// つながりをたどる
	//======================================================================================
	UIBase* UINavigationGroup::FindSelectable(const Engine::GameObject::ObjectContext& a_context, const UIBase* a_pFrom, int a_step) const
	{
		if (!a_pFrom || a_step == 0) return nullptr;

		const UIBase* _pWalk = a_pFrom;
		for (int _i = 0; _i < CHAIN_MAX; ++_i)
		{
			UIBase* _pNext = (a_step > 0) ? _pWalk->FindNavNext(a_context) : FindPrev(a_context, _pWalk);

			// 端で途切れた / 一周して戻ってきた
			if (!_pNext || _pNext == a_pFrom) return nullptr;

			if (IsSelectable(a_context, _pNext)) return _pNext;
			_pWalk = _pNext;
		}
		return nullptr;
	}

	UIBase* UINavigationGroup::FindPrev(const Engine::GameObject::ObjectContext& a_context, const UIBase* a_pFrom) const
	{
		if (!a_pFrom) return nullptr;

		// 前が指してあればそれ。指してあるのに引けない(消した UI を指したまま)ときは途切れとする
		if (a_pFrom->GetNavPrevGUID().IsValid()) return a_pFrom->FindNavPrev(a_context);

		// 空なら、最初から Next をたどって「Next が自分」のものを探す
		UIBase* _pFirst = Picker::Find<UIBase>(a_context.pObjectManager, m_firstGUID);
		UIBase* _pWalk = _pFirst;
		for (int _i = 0; _i < CHAIN_MAX && _pWalk; ++_i)
		{
			UIBase* _pNext = _pWalk->FindNavNext(a_context);
			if (_pNext == a_pFrom) return _pWalk;
			if (_pNext == _pFirst) break;
			_pWalk = _pNext;
		}
		return nullptr;
	}

	UIBase* UINavigationGroup::FindHoveredInChain(const Engine::GameObject::ObjectContext& a_context) const
	{
		UIBase* _pFirst = Picker::Find<UIBase>(a_context.pObjectManager, m_firstGUID);
		UIBase* _pWalk = _pFirst;
		for (int _i = 0; _i < CHAIN_MAX && _pWalk; ++_i)
		{
			if (_pWalk->IsHovered()) return _pWalk;

			_pWalk = _pWalk->FindNavNext(a_context);
			if (_pWalk == _pFirst) break;
		}
		return nullptr;
	}

	bool UINavigationGroup::IsSelectable(const Engine::GameObject::ObjectContext& a_context, const UIBase* a_pUI)
	{
		if (!a_pUI) return false;
		if (!a_pUI->HasInteraction() || !a_pUI->IsInteractable()) return false;

		return a_pUI->IsVisibleInHierarchy(a_context);
	}

	//======================================================================================
	// シリアライズ
	//======================================================================================
	void UINavigationGroup::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& /*a_context*/)
	{
		a_ar.GUIDField("FirstGUID", m_firstGUID);
		a_ar.Field("Priority", m_priority);
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void UINavigationGroup::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		auto* _pObjectManager = a_context.pObjectManager;

		Engine::EditorField::Header("UINavigationGroup");
		Engine::EditorField::HelpText("First から各 UI の Next / Prev をたどって選ぶ。Tab・LB/RB・矢印・スティック・十字キーで移り、Enter / A で押す");

		Picker::DrawCombo<UIBase>("First", _pObjectManager, m_firstGUID);
		Engine::EditorField::Tooltip("最初に選ぶ UI。前・次は各 UI のインスペクターの Navigation でつなぐ");

		Engine::EditorField::Field("Priority", m_priority);
		Engine::EditorField::Tooltip("同時に出ている他のグループより大きければ、こちらが動く(確認の箱は一覧より大きくする)");

		// 実行中の状態は表示のみ
		const UIBase* _pCurrent = Picker::Find<UIBase>(_pObjectManager, m_currentGUID);
		Engine::EditorField::Value("Current", "%s", _pCurrent ? Picker::MakeLabel(_pCurrent).c_str() : "None");

		DrawChainInspector(a_context);
	}

	//======================================================================================
	// つながりの確認
	//--------------------------------------------------------------------------------------
	// First から Next をたどった並びを出す。前と次が食い違っているところ・
	// 選べない UI・途中で切れているところが分かるようにする
	//======================================================================================
	void UINavigationGroup::DrawChainInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::TreeScope _tree("Chain");
		if (!_tree) return;

		UIBase* _pFirst = Picker::Find<UIBase>(a_context.pObjectManager, m_firstGUID);
		if (!_pFirst)
		{
			Engine::EditorField::WarningText("%s", m_firstGUID.IsValid() ? "First : シーンに無い UI を指しています" : "First が空です");
			return;
		}

		const UIBase* _pWalk = _pFirst;
		for (int _i = 0; _i < CHAIN_MAX && _pWalk; ++_i)
		{
			const std::string _label = std::to_string(_i);
			Engine::EditorField::Value(_label.c_str(), "%s", Picker::MakeLabel(_pWalk).c_str());

			if (!_pWalk->HasInteraction())
			{
				Engine::EditorField::WarningText("  押せない UI です(選べません。Next もたどれません)");
				return;
			}

			const Core::GUID& _nextGUID = _pWalk->GetNavNextGUID();
			if (!_nextGUID.IsValid())
			{
				Engine::EditorField::HelpText("ここで終わり(端で止まる)");
				return;
			}

			const UIBase* _pNext = _pWalk->FindNavNext(a_context);
			if (!_pNext)
			{
				Engine::EditorField::WarningText("  Next がシーンに無い UI を指しています");
				return;
			}

			// 次の UI の Prev が別のものを指していると、上・左で戻る先が食い違う
			const Core::GUID& _backGUID = _pNext->GetNavPrevGUID();
			if (_backGUID.IsValid() && _backGUID != _pWalk->GetGUID())
			{
				Engine::EditorField::WarningText("  次の UI の Prev がこの UI を指していません");
			}

			if (_pNext == _pFirst)
			{
				Engine::EditorField::HelpText("First へ戻る(一周する)");
				return;
			}

			_pWalk = _pNext;
		}

		Engine::EditorField::WarningText("たどる上限(%d)に達しました。First へ戻らない輪になっています", CHAIN_MAX);
	}
}
