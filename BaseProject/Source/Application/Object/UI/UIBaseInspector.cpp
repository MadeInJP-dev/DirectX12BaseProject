//==========================================================================================
// UIBase のエディター用の処理(インスペクター・ギズモ)
//
// 実行時の処理(UIBase.cpp)と分けてある。中身の編集欄はそれぞれの持ち主が持つ :
//   アンカー       … UIAnchor::DrawInspector
//   カーソルへの反応 … UIInteraction::DrawSettingsInspector / DrawSoundInspector
//   飾り1つ       … Decoration::DrawDecorationInspector
//   演出          … UITransition::DrawSettingsInspector
// ここはそれを並べ、UIBase でしか分からないもの(判定の範囲・取り合いの結果・飾りの一覧)を足す
//==========================================================================================
#include "UIBase.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/Option/OptionManager.h"		// ウィンドウ解像度(px)取得用
#include "Engine/EditorField/EditorField.h"

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
			Engine::EditorField::WarningText("上にある UIPanel が隠れているため出ていません");
		}

		// HUD か : プレイヤーが設定画面で HUD を消すと一緒に消える
		Engine::EditorField::Field("IsHUD", m_isHUD);
		Engine::EditorField::Tooltip("プレイヤーが設定画面で HUD を消したとき、一緒に描かれなくなる(照準・ゲージなど)");

		// 置き場所
		m_anchor.DrawInspector(_w, _h);

		// カーソルへの反応(押せる UI だけ)
		if (m_opInteraction) DrawInteractionInspector(a_context);

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
			Engine::EditorField::WarningText("Cursor : 手前の別UIに取られています(Layer %.1f)", m_anchor.layer);
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

		// ---- 追加 ----
		if (Engine::EditorField::CreateButton("Add Polygon"))
		{
			AddDecoration(Decoration::EDecorationType::Polygon);
			m_editDecorationIndex = static_cast<int>(m_decorationVec.size()) - 1;
		}
		Engine::EditorField::SameLine();
		if (Engine::EditorField::CreateButton("Add Image"))
		{
			AddDecoration(Decoration::EDecorationType::Image);
			m_editDecorationIndex = static_cast<int>(m_decorationVec.size()) - 1;
		}
		Engine::EditorField::SameLine();
		if (Engine::EditorField::CreateButton("Add Text"))
		{
			AddDecoration(Decoration::EDecorationType::Text);
			m_editDecorationIndex = static_cast<int>(m_decorationVec.size()) - 1;
		}

		// ---- 全削除 ----
		// 戻せないので Ctrl を押している間だけ効かせる
		if (!m_decorationVec.empty())
		{
			Engine::EditorField::SameLine();
			if (Engine::EditorField::DeleteButton("Clear All") && Engine::EditorField::IsCtrlDown())
			{
				m_decorationVec.clear();
				m_editDecorationIndex = -1;
			}
			Engine::EditorField::Tooltip("Ctrl+クリックで全部消す");
		}

		// 一覧を回している間に配列を触ると足元が崩れるので、操作は覚えておいて後でまとめて行う
		int _removeIndex = -1;
		int _swapIndex = -1;		// この番号と次の番号を入れ替える

		for (int _i = 0; _i < static_cast<int>(m_decorationVec.size()); ++_i)
		{
			Decoration::Decoration& _decoration = m_decorationVec[_i];

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
				_i + 1 < static_cast<int>(m_decorationVec.size()))
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
			std::swap(m_decorationVec[_swapIndex], m_decorationVec[_swapIndex + 1]);

			// 開いていたものを追いかける
			if (m_editDecorationIndex == _swapIndex)          m_editDecorationIndex = _swapIndex + 1;
			else if (m_editDecorationIndex == _swapIndex + 1) m_editDecorationIndex = _swapIndex;
		}

		if (_removeIndex >= 0)
		{
			m_decorationVec.erase(m_decorationVec.begin() + _removeIndex);

			// 消したぶん番号がずれる
			if (m_editDecorationIndex == _removeIndex)     m_editDecorationIndex = -1;
			else if (m_editDecorationIndex > _removeIndex) --m_editDecorationIndex;
		}
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
		// pixelPos はピボットのスクリーン座標なので、ハンドルはそのままピボット位置を指す。
		Math::Vector2& _pixelPos = m_anchor.pixelPos;

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
			_pixelPos.x = std::clamp(_u * _w, 0.0f, _w);
			_pixelPos.y = std::clamp(_v * _h, 0.0f, _h);
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
		if (m_editDecorationIndex < 0 || m_editDecorationIndex >= static_cast<int>(m_decorationVec.size())) return;

		Decoration::Decoration& _decoration = m_decorationVec[m_editDecorationIndex];

		// 画面上の位置(ゲーム内ピクセル) → シーンビュー上ピクセル
		const Math::Vector2 _screenPos = m_anchor.ToScreenPos(_decoration.offsetPos);

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
		const Math::Vector2 _local = UIAnchor::RotateDeg(_dragScreen - m_anchor.pixelPos, -m_anchor.rotation);
		if (m_anchor.scale > 1e-6f)
		{
			_decoration.offsetPos = _local / m_anchor.scale;
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

		std::string _preview = "(none)";
		if (const Decoration::Decoration* _pSelected = FindDecorationById(a_inoutId))
		{
			_preview = _makeLabel(*_pSelected);
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
