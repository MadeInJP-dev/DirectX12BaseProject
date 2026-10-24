//==========================================================================================
// UIBase のエディター用の処理(インスペクター・ギズモ)
//
// 実行時の処理(UIBase.cpp)と分けてある。中身の編集欄はそれぞれの持ち主が持つ :
//   アンカー       … UIAnchor::DrawInspector
//   カーソルへの反応 … UIInteraction::DrawSettingsInspector / DrawSoundInspector
//   項目移動の前・次 … ここ(指した相手の向かい側もつなぐため、他の UI を触る)
//   飾り1つ       … Decoration::DrawDecorationInspector
//   演出          … UITransition::DrawSettingsInspector
//   マテリアル     … アセット(UIMaterial)を指すだけ。中身はアセットのインスペクターで編集する
// ここはそれを並べ、UIBase でしか分からないもの(判定の範囲・取り合いの結果・飾りの一覧)を足す
//==========================================================================================
#include "UIBase.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/Option/OptionManager.h"		// ウィンドウ解像度(px)取得用
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
		if (!a_context.pServices->pOptionManager || !a_context.pServices->pResourceManager) return;

		// ウィンドウサイズの取得
		const auto& _winOp = a_context.pServices->pOptionManager->GetWindowOption();
		const float _w = static_cast<float>(_winOp.windowWidth);
		const float _h = static_cast<float>(_winOp.windowHeight);

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

		// 飾り全部に掛ける効果
		DrawMaterialInspector(a_context);

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
		// いま効いている判定を出す
		//
		// 幅0の矩形はどこにも当たらないので、乗らない原因がここだと分かるようにする
		//----------------------------------------------------------------------
		const float _scale = m_anchor.scale;

		Math::Vector2 _hitCenter = {};
		Math::Vector2 _hitSize = {};
		const bool _hasHitBounds = CalcDecorationBounds(_hitCenter, _hitSize, _interaction.isHitFollowAnim);

		if (_interaction.isHitFollowAnim && _hasHitBounds)
		{
			// 実行中は毎フレーム変わる。止まっているときは素の大きさと同じ
			Engine::EditorField::Value("Hit", "%.0f x %.0f (飾りの範囲/アニメ込み)", _hitSize.x * _scale, _hitSize.y * _scale);
		}
		else if (m_anchor.HasSize())
		{
			Engine::EditorField::Value("Hit", "%.0f x %.0f (PixelSize)", m_anchor.pixelSize.x, m_anchor.pixelSize.y);

			if (_interaction.isHitFollowAnim)
			{
				Engine::EditorField::HelpText("HitFollowAnim は立っていますが、測れる飾りが無いので PixelSize です");
			}
		}
		else if (_hasHitBounds)
		{
			Engine::EditorField::Value("Hit", "%.0f x %.0f (飾りの範囲)", _hitSize.x * _scale, _hitSize.y * _scale);
			Engine::EditorField::Tooltip("PixelSize が 0 なので飾りの範囲を使っています");
		}
		else
		{
			Engine::EditorField::ErrorText("Hit : なし");
			Engine::EditorField::HelpText("PixelSize も飾りの大きさも 0 です。カーソルに反応しません");
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
	// 項目移動の前・次
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
		Engine::EditorField::Tooltip("上・左で移る先。選んだ UI の Next はこの UI になる。空なら「Next がこの UI」のものを前とみなす");

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
		Engine::EditorField::Tooltip("下・右・Tab で移る先。選んだ UI の Prev はこの UI になる");

		// ---- つなぎ方の間違い ----
		auto _warnLink = [&](const char* a_label, const Core::GUID& a_guid)
			{
				if (!a_guid.IsValid()) return;

				const UIBase* _pTarget = FindUI(a_context, a_guid);
				if (!_pTarget)						Engine::EditorField::WarningText("%s : シーンに無い UI を指しています", a_label);
				else if (_pTarget == this)			Engine::EditorField::WarningText("%s : 自分を指しています", a_label);
				else if (!_pTarget->HasInteraction())	Engine::EditorField::WarningText("%s : 押せない UI です(選べません)", a_label);
			};
		_warnLink("Prev", _interaction.navPrevGUID);
		_warnLink("Next", _interaction.navNextGUID);
	}

	//======================================================================================
	// マテリアル
	//======================================================================================
	void UIBase::DrawMaterialInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Header("Material");

		Core::GUID _guid = m_materialGUID;
		if (Engine::EditorField::AssetField(*a_context.pServices, "UIMaterial", "UIMaterial", _guid))
		{
			SetMaterialGUID(a_context, _guid);
		}
		Engine::EditorField::Tooltip("飾り全部に掛ける効果(ディゾルブなど)。進み具合は Transition の Dissolve で動かす");

		if (m_materialGUID.IsValid() && !FindReadyMaterial(a_context.pServices->pResourceManager))
		{
			Engine::EditorField::WarningText("読み込み中です(終わるまでは効果なしで描きます)");
		}
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
		Engine::EditorField::HelpText("配列の順に描きます(下にあるものほど手前)");

		// どちらの組を編集するか(コントローラー用を持つときだけ選べる)
		DrawDecorationSetInspector(a_context);

		std::vector<Decoration::Decoration>& _decorationVec = RefEditDecorationVec();

		// ---- 追加 ----
		if (Engine::EditorField::CreateButton("Add Polygon"))
		{
			AddDecorationTo(_decorationVec, Decoration::EDecorationType::Polygon);
			m_editDecorationIndex = static_cast<int>(_decorationVec.size()) - 1;
		}
		Engine::EditorField::SameLine();
		if (Engine::EditorField::CreateButton("Add Image"))
		{
			AddDecorationTo(_decorationVec, Decoration::EDecorationType::Image);
			m_editDecorationIndex = static_cast<int>(_decorationVec.size()) - 1;
		}
		Engine::EditorField::SameLine();
		if (Engine::EditorField::CreateButton("Add Text"))
		{
			AddDecorationTo(_decorationVec, Decoration::EDecorationType::Text);
			m_editDecorationIndex = static_cast<int>(_decorationVec.size()) - 1;
		}

		// ---- 全削除 ----
		// 戻せないので Ctrl を押している間だけ効かせる
		if (!_decorationVec.empty())
		{
			Engine::EditorField::SameLine();
			if (Engine::EditorField::DeleteButton("Clear All") && Engine::EditorField::IsCtrlDown())
			{
				_decorationVec.clear();
				m_editDecorationIndex = -1;
			}
			Engine::EditorField::Tooltip("Ctrl+クリックで全部消す");
		}

		// 一覧を回している間に配列を触ると足元が崩れるので、操作は覚えておいて後でまとめて行う
		int _removeIndex = -1;
		int _swapIndex = -1;		// この番号と次の番号を入れ替える

		for (int _i = 0; _i < static_cast<int>(_decorationVec.size()); ++_i)
		{
			Decoration::Decoration& _decoration = _decorationVec[_i];

			Engine::EditorField::IDScope _id(_i);

			//----------------------------------------------------------------------
			// 1行ぶん : [X][↑][↓] 名前
			//
			// ボタンを先に置くこと。
			// Selectable は残りの幅を全部使うので、後ろへ並べると
			// ボタンが行の外まで押し出されて押せなくなる
			//----------------------------------------------------------------------
			if (Engine::EditorField::DeleteSmallButton("X")) _removeIndex = _i;
			Engine::EditorField::Tooltip("この飾りを消す");

			Engine::EditorField::SameLine();
			if (Engine::EditorField::ArrowButton("##Up", Engine::EditorField::EArrowDir::Up) && _i > 0) _swapIndex = _i - 1;

			Engine::EditorField::SameLine();
			if (Engine::EditorField::ArrowButton("##Down", Engine::EditorField::EArrowDir::Down) &&
				_i + 1 < static_cast<int>(_decorationVec.size()))
			{
				_swapIndex = _i;
			}

			// 開閉 : 開いているものだけ中身を出す
			Engine::EditorField::SameLine();
			const bool _isOpen = (m_editDecorationIndex == _i);
			const std::string _label =
				std::to_string(_i) + " : " + (_decoration.name.empty() ? "(no name)" : _decoration.name);

			if (Engine::EditorField::Selectable(_label.c_str(), _isOpen))
			{
				m_editDecorationIndex = _isOpen ? -1 : _i;
			}

			if (_isOpen)
			{
				{
					Engine::EditorField::IndentScope _indent;
					if (a_context.pServices) Decoration::DrawDecorationInspector(_decoration, *a_context.pServices);
				}
				Engine::EditorField::Line();
			}
		}

		if (_swapIndex >= 0)
		{
			std::swap(_decorationVec[_swapIndex], _decorationVec[_swapIndex + 1]);

			// 開いていたものを追いかける
			if (m_editDecorationIndex == _swapIndex)          m_editDecorationIndex = _swapIndex + 1;
			else if (m_editDecorationIndex == _swapIndex + 1) m_editDecorationIndex = _swapIndex;
		}

		if (_removeIndex >= 0)
		{
			_decorationVec.erase(_decorationVec.begin() + _removeIndex);

			// 消したぶん番号がずれる
			if (m_editDecorationIndex == _removeIndex)     m_editDecorationIndex = -1;
			else if (m_editDecorationIndex > _removeIndex) --m_editDecorationIndex;
		}
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
		if (!m_opGamepadDecorationVec)
		{
			if (Engine::EditorField::CreateButton("Add Gamepad Decorations"))
			{
				CreateGamepadDecorations();
				m_isEditGamepadSet = true;
				m_editDecorationIndex = -1;
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
			m_editDecorationIndex = -1;
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

	std::vector<Decoration::Decoration>& UIBase::RefEditDecorationVec()
	{
		return (m_isEditGamepadSet && m_opGamepadDecorationVec) ? *m_opGamepadDecorationVec : m_decorationVec;
	}

	//======================================================================================
	// ギズモ : シーンビュー上にドラッグ可能なハンドルを出してピクセル座標を編集する
	//======================================================================================
	bool UIBase::DrawGizmo(const Engine::GameObject::ObjectGizmoContext& a_ctx, Engine::GameObject::ObjectContext& a_context)
	{
		if (a_ctx.viewportSize.x <= 0.0f || a_ctx.viewportSize.y <= 0.0f) return false;
		if (!a_context.pServices || !a_context.pServices->pOptionManager) return false;

		// ウィンドウサイズの取得
		const auto& _winOp = a_context.pServices->pOptionManager->GetWindowOption();
		const float _w = static_cast<float>(_winOp.windowWidth);
		const float _h = static_cast<float>(_winOp.windowHeight);
		if (_w <= 0.0f || _h <= 0.0f) return false;

		// 当たり判定の範囲(線を引くだけなので、ハンドルの操作には関わらない)
		DrawHitRectGizmo(a_ctx, _w, _h);

		// 開いている飾りのハンドル。
		// アンカーのハンドルより先に置くこと : 重なったときは先に置いたほうが掴まれるので、
		// OffsetPos が 0 の飾り(アンカーと同じ位置)でも飾りのほうを動かせる
		DrawDecorationGizmo(a_ctx, _w, _h);

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

	//======================================================================================
	// ギズモ : 当たり判定の範囲
	//--------------------------------------------------------------------------------------
	// 判定と同じ矩形(CalcHitRect)を、余白(HitPadding)込みで囲む。
	// 「絵はここなのに押せない」を、見ただけで分かるようにするため。
	//   触れる   … 緑
	//   触れない … 灰(Interactable を切っている。矩形はあるが反応しない)
	// HitFollowAnim を立てていると、アニメで大きさが変わるのに合わせて枠も動く
	//======================================================================================
	void UIBase::DrawHitRectGizmo(const Engine::GameObject::ObjectGizmoContext& a_ctx, float a_screenW, float a_screenH) const
	{
		HitRect _rect = {};
		if (!CalcHitRect(_rect)) return;

		Math::Vector2 _cornerArray[4] = {};
		UIAnchor::CalcRectCorners(_rect.pixelPos, _rect.pixelSize, _rect.pivot, _rect.rotation, _rect.padding, _cornerArray);

		// ゲーム内ピクセル(左上原点) → シーンビュー上ピクセル
		for (Math::Vector2& _corner : _cornerArray)
		{
			_corner.x = a_ctx.viewportPos.x + (_corner.x / a_screenW) * a_ctx.viewportSize.x;
			_corner.y = a_ctx.viewportPos.y + (_corner.y / a_screenH) * a_ctx.viewportSize.y;
		}

		const bool _isInteractable = m_opInteraction && m_opInteraction->isInteractable;
		const Math::Color _color = _isInteractable
			? Math::Color(0.2f, 1.0f, 0.3f, 1.0f)
			: Math::Color(0.6f, 0.6f, 0.6f, 1.0f);

		Engine::EditorField::ScreenPolyline(_cornerArray, 4, _color, 1.5f, true);
	}

	//======================================================================================
	// ギズモ : 開いている飾りの位置
	//--------------------------------------------------------------------------------------
	// 飾りの OffsetPos はアンカーからのずれ(アンカーの回転・倍率を掛ける前)なので、
	// 画面上の位置からは回転を戻し、倍率で割って求める。
	// アンカーのハンドルと見分けられるよう、小さく出す
	//======================================================================================
	void UIBase::DrawDecorationGizmo(const Engine::GameObject::ObjectGizmoContext& a_ctx, float a_screenW, float a_screenH)
	{
		std::vector<Decoration::Decoration>& _decorationVec = RefEditDecorationVec();
		if (m_editDecorationIndex < 0 || m_editDecorationIndex >= static_cast<int>(_decorationVec.size())) return;

		Decoration::Decoration& _decoration = _decorationVec[m_editDecorationIndex];

		// 画面での置き場所(親の行列を掛けたもの)
		const UIAnchor _worldAnchor = CalcWorldAnchor();

		// 画面上の位置(ゲーム内ピクセル) → シーンビュー上ピクセル
		const Math::Vector2 _screenPos = _worldAnchor.ToScreenPos(_decoration.offsetPos);

		Math::Vector2 _handle = {};
		_handle.x = a_ctx.viewportPos.x + (_screenPos.x / a_screenW) * a_ctx.viewportSize.x;
		_handle.y = a_ctx.viewportPos.y + (_screenPos.y / a_screenH) * a_ctx.viewportSize.y;

		static const float HANDLE_RADIUS = 5.0f;

		Math::Vector2 _mouse = {};
		if (!Engine::EditorField::ScreenHandle("##UIDecorationGizmo", _handle, HANDLE_RADIUS, _mouse)) return;

		// シーンビュー上 → ゲーム内ピクセル
		const Math::Vector2 _dragScreen = {
			(_mouse.x - a_ctx.viewportPos.x) / a_ctx.viewportSize.x * a_screenW,
			(_mouse.y - a_ctx.viewportPos.y) / a_ctx.viewportSize.y * a_screenH
		};

		// アンカーからのずれへ戻す : 回転を戻してから倍率で割る
		const Math::Vector2 _local = UIAnchor::RotateDeg(_dragScreen - _worldAnchor.pixelPos, -_worldAnchor.rotation);
		if (_worldAnchor.scale > 1e-6f)
		{
			_decoration.offsetPos = _local / _worldAnchor.scale;
		}
	}

	//======================================================================================
	// 飾りを1つ選ぶコンボ
	//======================================================================================
	bool UIBase::DrawDecorationPicker(
		const char* a_label,
		uint32_t& a_inoutId,
		std::optional<Decoration::EDecorationType> a_opFilterType) const
	{
		// 見出し : 番号と名前。指している飾りが無くなっていたら分かるようにする
		const auto _makeLabel = [](const Decoration::Decoration& a_decoration)
			{
				return std::to_string(a_decoration.id) + " : " + (a_decoration.name.empty() ? "(no name)" : a_decoration.name);
			};

		// 並べるのは既定の組(番号は組をまたいで共有するので、コントローラー用の同じ役の飾りも同じ番号で指せる)
		const auto _itSelected = std::find_if(m_decorationVec.begin(), m_decorationVec.end(),
			[a_inoutId](const Decoration::Decoration& a_decoration) { return a_inoutId != 0 && a_decoration.id == a_inoutId; });

		std::string _preview = "(none)";
		if (_itSelected != m_decorationVec.end())
		{
			_preview = _makeLabel(*_itSelected);
		}
		else if (a_inoutId != 0)
		{
			_preview = "(missing " + std::to_string(a_inoutId) + ")";
		}

		bool _isChanged = false;

		Engine::EditorField::ComboScope _combo(a_label, _preview.c_str());
		if (!_combo) return false;

		if (Engine::EditorField::Selectable("(none)", a_inoutId == 0))
		{
			a_inoutId = 0;
			_isChanged = true;
		}

		for (const Decoration::Decoration& _decoration : m_decorationVec)
		{
			if (a_opFilterType && _decoration.GetType() != *a_opFilterType) continue;

			Engine::EditorField::IDScope _id(static_cast<int>(_decoration.id));
			if (Engine::EditorField::Selectable(_makeLabel(_decoration).c_str(), _decoration.id == a_inoutId))
			{
				a_inoutId = _decoration.id;
				_isChanged = true;
			}
		}

		return _isChanged;
	}
}
