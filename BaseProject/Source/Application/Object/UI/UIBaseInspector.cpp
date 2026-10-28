//==========================================================================================
// UIBase のエディター用の処理(インスペクター・ギズモ)
//
// 実行時の処理(UIBase.cpp)と分けてある。中身の編集欄はそれぞれの持ち主が持つ :
//   アンカー       … UIAnchor::DrawInspector
//   カーソルへの反応 … UIInteraction::DrawSettingsInspector / DrawSoundInspector
//   項目移動の前・次 … ここ(指した相手の向かい側もつなぐため、他の UI を触る)
//   飾り(UIマテリアル込み) … Decoration::UIDecoration::DrawInspector / DrawGizmo
//   演出          … UITransition::DrawSettingsInspector
// ここはそれを並べ、UIBase でしか分からないもの(判定を受けるか・取り合いの結果・飾りの組)を足す
//==========================================================================================
#include "UIBase.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/MainEngine.h"
#include "Engine/EditorField/EditorField.h"

#include "Application/Object/ObjectPicker.h"	// 項目移動の前・次を選ぶコンボ

namespace App::Object
{
	//======================================================================================
	// インスペクター
	//======================================================================================
	void UIBase::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices) return;
		if (!a_context.pServices->pMainEngine || !a_context.pServices->pResourceManager) return;

		// ウィンドウサイズの取得
		const auto* _pGraphicsEngine = a_context.pServices->pMainEngine->RefGraphicsEngine();
		const float _w = static_cast<float>(_pGraphicsEngine->GetRenderWidth());
		const float _h = static_cast<float>(_pGraphicsEngine->GetRenderHeight());

		// 表示するか : 出し分けを持つ画面(ホームなど)は進行役がここを切り替える
		// SetVisible を通すので、出し入れの演出を付けていればここで確かめられる
		bool _isVisible = m_isVisible;
		if (Engine::EditorField::Field("Visible", _isVisible)) SetVisible(_isVisible);
		Engine::EditorField::Tooltip("切ると描画も入力も止まる");

		// 自分は出す指示なのに、上のパネルが隠れているせいで出ていないことが分かるようにする
		if (m_isVisible && !IsVisibleInHierarchy(a_context))
		{
			Engine::EditorField::WarningText("上にある親が隠れているため出ていません");
		}

		// HUD か : プレイヤーが設定画面で HUD を消すと一緒に消える
		Engine::EditorField::Field("IsHUD", m_isHUD);
		Engine::EditorField::Tooltip("プレイヤーが設定画面で HUD を消したとき、一緒に描かれなくなる(照準・ゲージなど)");

		// 子の状態にも反応するか : 行の中の部品を選ぶと、行の飾りも Hovered / Pressed で反応する
		Engine::EditorField::Field("ReactToChildren", m_isReactToChildren);
		Engine::EditorField::Tooltip("下の UI が乗られている・選ばれている・押されている間、自分の飾りもその状態で反応する(行ごと光らせるなど)");

		// 当たり判定を受けるか : 当たったかどうかは飾りのボディが答える
		Engine::EditorField::Field("HitTarget", m_isHitTarget);
		Engine::EditorField::Tooltip("立てると、ボディ(Hit Body)を持つ飾りにカーソルが重なっているかで当たりを取る。切るとカーソルが乗らない");

		// 置き場所
		m_anchor.DrawInspector(_w, _h);

		// アンカーの位置・回転・倍率は親から見た値。親が居るときは画面での値も出しておく
		if (GetParent())
		{
			const UIAnchor _worldAnchor = CalcWorldAnchor();
			Engine::EditorField::HelpText("Screen : Pos(%.1f, %.1f) Rot %.1f Scale %.2f Layer %.1f",
				_worldAnchor.pixelPos.x, _worldAnchor.pixelPos.y, _worldAnchor.rotation, _worldAnchor.scale, _worldAnchor.layer);
			Engine::EditorField::Tooltip("上の値は親(%s)から見たもの。こちらは親の行列を掛けた画面での値", GetParent()->GetDisplayName());
		}

		// カーソルへの反応(押せる UI だけ)
		if (m_opInteraction) DrawInteractionInspector(a_context);

		// 項目移動の前・次(押せる UI だけ)
		if (m_opInteraction) DrawNavigationInspector(a_context);

		// 出し入れと合図の演出
		DrawTransitionInspector();

		Engine::EditorField::Line();

		// 飾り
		DrawDecorationListInspector(a_context);
	}

	//======================================================================================
	// カーソルへの反応
	//======================================================================================
	void UIBase::DrawInteractionInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIInteraction& _interaction = *m_opInteraction;

		Engine::EditorField::Header("Interaction");

		_interaction.DrawSettingsInspector();

		//----------------------------------------------------------------------
		// 当たり判定の状態
		//
		// 押せる UI なのに当たる場所が無い、が分かるようにする
		//----------------------------------------------------------------------
		if (!m_isHitTarget)
		{
			Engine::EditorField::WarningText("Hit : HitTarget が切れているのでカーソルに反応しません");
		}
		else if (!GetDecoration().HasHitBody())
		{
			Engine::EditorField::ErrorText("Hit : なし");
			Engine::EditorField::HelpText("ボディ(Hit Body)を持つ飾りがありません。飾りのどれかに Hit Body を付けてください");
		}
		else
		{
			Engine::EditorField::Value("Hit", "%s", "飾りのボディ(シーンビューの緑の枠)");
		}

		_interaction.DrawSoundInspector(a_context);

		// 重なりの取り合いの結果。
		// 「矩形には入っているのに反応しない」の原因がここだと分かるようにする
		if (_interaction.isCursorInside && !_interaction.IsCursorOwner(a_context, this))
		{
			Engine::EditorField::WarningText("Cursor : 手前の別UIに取られています(Layer %.1f)", CalcWorldAnchor().layer);
		}
	}

	//======================================================================================
	// 項目移動の前・次・左・右
	//--------------------------------------------------------------------------------------
	// 片側だけつなぎ忘れないように、指した相手の向かい側(Next を選んだら相手の Prev)も
	// この UI へ向ける。指し替えたときは、前に指していた相手の向かい側が自分なら外す。
	// 最後の Next を最初へ向ければ、端で止まらずに一周する
	//======================================================================================
	void UIBase::DrawNavigationInspector(Engine::GameObject::ObjectContext& a_context)
	{
		UIInteraction& _interaction = *m_opInteraction;
		auto* _pObjectManager = a_context.pObjectManager;

		Engine::EditorField::Header("Navigation");
		Engine::EditorField::HelpText("キー・パッドで選ぶときの前と次。選択を動かすのは UINavigationGroup(最初の UI を指す)");

		// ---- 前 ----
		Core::GUID _prevGUID = _interaction.navPrevGUID;
		if (Picker::DrawCombo<UIBase>("Prev", _pObjectManager, _prevGUID))
		{
			UIBase* _pOld = FindUI(a_context, _interaction.navPrevGUID);
			if (_pOld && _pOld != this && _pOld->GetNavNextGUID() == GetGUID()) _pOld->SetNavNextGUID({});

			_interaction.navPrevGUID = _prevGUID;

			UIBase* _pNew = FindUI(a_context, _prevGUID);
			if (_pNew && _pNew != this) _pNew->SetNavNextGUID(GetGUID());
		}
		Engine::EditorField::Tooltip("上・LB で移る先(Left が空なら左も)。選んだ UI の Next はこの UI になる。空なら「Next がこの UI」のものを前とみなす");

		// ---- 次 ----
		Core::GUID _nextGUID = _interaction.navNextGUID;
		if (Picker::DrawCombo<UIBase>("Next", _pObjectManager, _nextGUID))
		{
			UIBase* _pOld = FindUI(a_context, _interaction.navNextGUID);
			if (_pOld && _pOld != this && _pOld->GetNavPrevGUID() == GetGUID()) _pOld->SetNavPrevGUID({});

			_interaction.navNextGUID = _nextGUID;

			UIBase* _pNew = FindUI(a_context, _nextGUID);
			if (_pNew && _pNew != this) _pNew->SetNavPrevGUID(GetGUID());
		}
		Engine::EditorField::Tooltip("下・Tab で移る先(Right が空なら右も)。選んだ UI の Prev はこの UI になる");

		// ---- 左・右 ----
		// 上下と違って向かい側は付けない(一覧の項目どれからでも同じ詳細へ移る、のように多対1になるため)
		Picker::DrawCombo<UIBase>("Left", _pObjectManager, _interaction.navLeftGUID);
		Engine::EditorField::Tooltip("左で移る先。空なら Prev へ移る。自分を選ぶと左へは動かない(端で止まる)");

		Picker::DrawCombo<UIBase>("Right", _pObjectManager, _interaction.navRightGUID);
		Engine::EditorField::Tooltip("右で移る先。空なら Next へ移る。自分を選ぶと右へは動かない(端で止まる)");

		// ---- つなぎ方の間違い ----
		// 左右は「自分 = 端で止まる」なので、自分を指していても間違いにしない
		auto _warnLink = [&](const char* a_label, const Core::GUID& a_guid, bool a_isSelfAllowed = false)
			{
				if (!a_guid.IsValid()) return;

				const UIBase* _pTarget = FindUI(a_context, a_guid);
				if (!_pTarget)						Engine::EditorField::WarningText("%s : シーンに無い UI を指しています", a_label);
				else if (_pTarget == this)
				{
					if (a_isSelfAllowed)			Engine::EditorField::HelpText("%s : 自分(この向きへは動かない)", a_label);
					else							Engine::EditorField::WarningText("%s : 自分を指しています", a_label);
				}
				else if (!_pTarget->HasInteraction())	Engine::EditorField::WarningText("%s : 押せない UI です(選べません)", a_label);
			};
		_warnLink("Prev", _interaction.navPrevGUID);
		_warnLink("Next", _interaction.navNextGUID);
		_warnLink("Left", _interaction.navLeftGUID, true);
		_warnLink("Right", _interaction.navRightGUID, true);
	}

	//======================================================================================
	// 出し入れと合図の演出
	//--------------------------------------------------------------------------------------
	// 確認用のボタンは Visible を変えずに流すだけ。Hide を流し終えると元どおり出る
	//======================================================================================
	void UIBase::DrawTransitionInspector()
	{
		Engine::EditorField::Header("Transition");

		m_transition.DrawSettingsInspector();

		if (Engine::EditorField::SmallButton("Play Show")) m_transition.PlayShow();
		Engine::EditorField::SameLine();
		if (Engine::EditorField::SmallButton("Play Hide")) m_transition.PlayHide();
		Engine::EditorField::SameLine();
		if (Engine::EditorField::SmallButton("Play Pulse")) m_transition.PlayPulse();
	}

	//======================================================================================
	// 飾りの一覧
	//--------------------------------------------------------------------------------------
	// 描く順は配列順なので、並べ替えがそのまま重なり順になる。
	// 開いている1つだけ中身を出す形にしてあるのは、飾りが増えると
	// 全部展開したときにインスペクターが縦に流れて使えなくなるため
	//======================================================================================
	void UIBase::DrawDecorationListInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Header("Decorations");

		// どちらの組を編集するか(コントローラー用を持つときだけ選べる)
		DrawDecorationSetInspector(a_context);

		if (!a_context.pServices) return;

		// 片方の組にだけ足したエレメントが、もう片方の別のエレメントと同じ番号にならないように
		SyncDecorationIdFloors();

		Engine::EditorField::IDScope _id(m_isEditGamepadSet ? "GamepadSet" : "KeyboardSet");
		RefEditDecoration().DrawInspector(*a_context.pServices);
	}

	//======================================================================================
	// 飾りの組(キーボード用 / コントローラー用)
	//--------------------------------------------------------------------------------------
	// コントローラー用は既定の組の写しから作る(番号ごと写すので、ゲージの中身などを
	// 番号で指したまま出し分けられる)。外すと戻せないので Ctrl を押している間だけ効かせる。
	//
	// 編集する組を切り替えると、確かめ用の表示もその組へ合わせる(編集している絵が見えるように)。
	// 実行時と同じく最後に触ったデバイスに従わせたいときは Preview を Auto へ戻す
	//======================================================================================
	void UIBase::DrawDecorationSetInspector(Engine::GameObject::ObjectContext& a_context)
	{
		if (!m_opGamepadDecoration)
		{
			if (Engine::EditorField::CreateButton("Add Gamepad Decorations"))
			{
				CreateGamepadDecorations();
				m_isEditGamepadSet = true;
				m_decorationPreview = EDecorationPreview::Gamepad;
				UpdateShownDecorationSet(a_context);
			}
			Engine::EditorField::Tooltip("コントローラーを触っている間だけ出す飾りの組を作る(今の飾りの写しから)");
			return;
		}

		// ---- 編集する組 ----
		bool _isEditGamepadSet = m_isEditGamepadSet;
		if (Engine::EditorField::RadioButton("Keyboard", !_isEditGamepadSet)) _isEditGamepadSet = false;
		Engine::EditorField::SameLine();
		if (Engine::EditorField::RadioButton("Gamepad", _isEditGamepadSet)) _isEditGamepadSet = true;
		Engine::EditorField::Tooltip("編集する飾りの組。Gamepad はコントローラーを触っている間だけ出る");

		if (_isEditGamepadSet != m_isEditGamepadSet)
		{
			m_isEditGamepadSet = _isEditGamepadSet;
			m_decorationPreview = m_isEditGamepadSet ? EDecorationPreview::Gamepad : EDecorationPreview::KeyboardMouse;
		}

		// ---- 外す ----
		Engine::EditorField::SameLine();
		if (Engine::EditorField::DeleteButton("Remove Gamepad") && Engine::EditorField::IsCtrlDown())
		{
			RemoveGamepadDecorations();
			m_decorationPreview = EDecorationPreview::Auto;
			UpdateShownDecorationSet(a_context);
			return;
		}
		Engine::EditorField::Tooltip("Ctrl+クリックでコントローラー用の組を外す(どちらのデバイスでも既定の組を出す)");

		// ---- 確かめ用の表示(保存しない) ----
		Engine::EditorField::Field("Preview", m_decorationPreview);
		Engine::EditorField::Tooltip("Auto は実行時と同じく、最後に触ったデバイスに従う");

		// 止めている間は PreUpdate が回らないので、ここでも決め直す
		UpdateShownDecorationSet(a_context);

		Engine::EditorField::HelpText("Showing : %s", IsShowingGamepadDecorations() ? "Gamepad" : "Keyboard");
	}

	Decoration::UIDecoration& UIBase::RefEditDecoration()
	{
		return (m_isEditGamepadSet && m_opGamepadDecoration) ? *m_opGamepadDecoration : m_decoration;
	}

	//======================================================================================
	// ギズモ : シーンビュー上にドラッグ可能なハンドルを出してピクセル座標を編集する
	//======================================================================================
	bool UIBase::DrawGizmo(const Engine::GameObject::ObjectGizmoContext& a_ctx, Engine::GameObject::ObjectContext& a_context)
	{
		if (a_ctx.viewportSize.x <= 0.0f || a_ctx.viewportSize.y <= 0.0f) return false;
		if (!a_context.pServices || !a_context.pServices->pMainEngine) return false;

		// ウィンドウサイズの取得
		const auto* _pGraphicsEngine = a_context.pServices->pMainEngine->RefGraphicsEngine();
		const float _w = static_cast<float>(_pGraphicsEngine->GetRenderWidth());
		const float _h = static_cast<float>(_pGraphicsEngine->GetRenderHeight());
		if (_w <= 0.0f || _h <= 0.0f) return false;

		// 飾りのボディの枠と、開いている飾りのハンドル。
		// アンカーのハンドルより先に置くこと : 重なったときは先に置いたほうが掴まれるので、
		// OffsetPos が 0 の飾り(アンカーと同じ位置)でも飾りのほうを動かせる
		RefEditDecoration().DrawGizmo(a_ctx, MakeParentTransform(), MakeParentOption(), _w, _h);

		// ゲーム内ピクセル(左上原点) から シーンビュー上ピクセルへ。
		// ハンドルは画面でのピボット位置を指す(アンカーの値は親から見たものなので、親の行列を掛けてから)
		const Math::Vector2 _pixelPos = GetWorldPixelPos();

		Math::Vector2 _handle = {};
		_handle.x = a_ctx.viewportPos.x + (_pixelPos.x / _w) * a_ctx.viewportSize.x;
		_handle.y = a_ctx.viewportPos.y + (_pixelPos.y / _h) * a_ctx.viewportSize.y;

		// ギズモハンドルの半径 : ピクセル
		static const float HANDLE_RADIUS = 9.0f;

		// ドラッグ中はマウス位置からピクセル座標を逆算して更新
		Math::Vector2 _mouse = {};
		if (Engine::EditorField::ScreenHandle("##UIGizmo", _handle, HANDLE_RADIUS, _mouse))
		{
			const float _u = (_mouse.x - a_ctx.viewportPos.x) / a_ctx.viewportSize.x;	// 0..1
			const float _v = (_mouse.y - a_ctx.viewportPos.y) / a_ctx.viewportSize.y;	// 0..1
			SetWorldPixelPos({ std::clamp(_u * _w, 0.0f, _w), std::clamp(_v * _h, 0.0f, _h) });
		}

		return true;
	}
}
