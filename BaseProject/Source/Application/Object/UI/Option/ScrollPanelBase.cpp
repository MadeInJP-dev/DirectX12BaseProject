#include "ScrollPanelBase.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/GameObject/GameObjectManager/GameObjectManager.h"
#include "Engine/Input/InputManager/InputManager.h"
#include "Engine/EditorField/EditorField.h"

#include "Application/InstanceResource/UserDataResource.h"

#include "Application/Object/UI/UIBase.h"
#include "Application/Object/UI/Widget/UIGauge/UIGauge.h"

//==========================================================================================
// ScrollPanelBase
//
// 項目は「内容の座標」(パネルの上端を 0 とした縦の位置)で積み、
// 画面へ置くときにスクロールしたぶんだけ上へずらす。
//
//   画面の Y = 描画範囲の上端 + 内容の Y - スクロール位置
//
// スクロール位置は 0〜(内容の全長 - 見えている高さ)の間に詰める。
// 内容の全長は並べ終わった位置なので、項目を足しても減らしても勝手に合う。
//==========================================================================================
namespace App::Object
{
	//======================================================================================
	// 並べる
	//--------------------------------------------------------------------------------------
	// オブジェクト同士の PreUpdate の順は決まっていないので、項目のほうが先に回った
	// フレームは1フレーム前の位置で判定される。スクロール中に1フレーム遅れるだけで、
	// 押し間違いにはならない
	//======================================================================================
	void ScrollPanelBase::Layout(Engine::GameObject::ObjectContext& a_context, const PanelLayout& a_layout)
	{
		m_layout = a_layout;

		// 項目が減って全長が縮んだときに、範囲の外を指したままにしない
		SetScrollOffset(m_scrollOffset);

		// 上から順に積む。全長は積み終わった位置
		m_layoutCursorY = 0.0f;
		LayoutItems(a_context);
		m_contentLength = m_layoutCursorY;

		PlaceScrollBar(a_context);
	}

	void ScrollPanelBase::Update(Engine::GameObject::ObjectContext& a_context)
	{
		UpdateWheel(a_context);
		UpdateScrollBar(a_context);
		UpdateItems(a_context);
	}

	void ScrollPanelBase::SetVisible(Engine::GameObject::ObjectContext& a_context, bool a_isVisible)
	{
		// 隠したらつまみを離したことにする(出し直したときに掴んだままにならないように)
		if (!a_isVisible) m_isDragging = false;

		UIBase* _pRoot = FindUI(a_context, m_rootPanelGUID);
		if (_pRoot) _pRoot->SetVisible(a_isVisible);
	}

	//======================================================================================
	// スクロール
	//======================================================================================
	void ScrollPanelBase::SetScrollOffset(float a_offset)
	{
		m_scrollOffset = std::clamp(a_offset, 0.0f, GetMaxScrollOffset());
	}

	float ScrollPanelBase::GetMaxScrollOffset() const
	{
		return std::max(m_contentLength - m_layout.size.y, 0.0f);
	}

	void ScrollPanelBase::ScrollIntoView(float a_top, float a_bottom)
	{
		// 上にはみ出していれば上端を、下にはみ出していれば下端を、範囲の端へ合わせる
		if (a_top < m_scrollOffset)
		{
			SetScrollOffset(a_top);
		}
		else if (a_bottom > m_scrollOffset + m_layout.size.y)
		{
			SetScrollOffset(a_bottom - m_layout.size.y);
		}
	}

	//======================================================================================
	// 並べ方
	//======================================================================================
	void ScrollPanelBase::PlaceItem(Engine::GameObject::ObjectContext& a_context, const OptionItemUI& a_item)
	{
		UIBase* _pUI = FindUI(a_context, a_item.guid);
		if (!_pUI) return;

		// 行の左上(画面の座標)。内容はスクロールしたぶんだけ上へずれる
		const Math::Vector2 _topLeft = {
			m_layout.pos.x + a_item.offsetX,
			m_layout.pos.y + m_layoutCursorY - m_scrollOffset
		};

		// 大きさは項目ごとに違ってよいので、ここでは位置だけ入れる(判定はその UI の設定のまま)。
		// ピボットのずらしには行の大きさ(itemSize)を使う
		// 位置は画面の座標で決め、親から見た値へ直して入れる
		const UIAnchor& _anchor = _pUI->GetAnchor();
		_pUI->SetWorldPixelPos({
			_topLeft.x + m_layout.itemSize.x * _anchor.pivot.x,
			_topLeft.y + m_layout.itemSize.y * _anchor.pivot.y
		});

		_pUI->SetClipRect(MakeContentClipRect());
	}

	void ScrollPanelBase::AdvanceRow()
	{
		m_layoutCursorY += m_layout.itemSize.y + m_layout.itemSpacing;
	}

	void ScrollPanelBase::PlaceHeaderRow(Engine::GameObject::ObjectContext& a_context, const OptionItemUI& a_header)
	{
		if (!a_header.guid.IsValid()) return;

		PlaceItem(a_context, a_header);
		AdvanceRow();
	}

	//======================================================================================
	// 値を上げ下げする行
	//======================================================================================
	void ScrollPanelBase::PlaceStepper(Engine::GameObject::ObjectContext& a_context, const OptionStepperUI& a_stepper)
	{
		PlaceItem(a_context, a_stepper.gauge);
		PlaceItem(a_context, a_stepper.down);
		PlaceItem(a_context, a_stepper.up);
	}

	int ScrollPanelBase::UpdateStepper(Engine::GameObject::ObjectContext& a_context, const OptionStepperUI& a_stepper)
	{
		// TODO : 押しっぱなしで送り続ける・ゲージを掴んで動かす
		int _step = 0;

		UIBase* _pDown = FindUI(a_context, a_stepper.down.guid);
		if (_pDown && _pDown->IsClicked()) --_step;

		UIBase* _pUp = FindUI(a_context, a_stepper.up.guid);
		if (_pUp && _pUp->IsClicked()) ++_step;

		return _step;
	}

	void ScrollPanelBase::SetStepperValue(Engine::GameObject::ObjectContext& a_context, const OptionStepperUI& a_stepper, float a_value, float a_max)
	{
		if (auto* _pGauge = Core::TypeInfo::Cast<UIGauge>(FindUI(a_context, a_stepper.gauge.guid)))
		{
			_pGauge->SetValue(a_value, a_max);
		}
	}

	void ScrollPanelBase::ArchiveStepper(Engine::Persistence::Archive& a_ar, const std::string& a_name, OptionStepperUI& a_stepper)
	{
		ArchiveItemUI(a_ar, a_name + "Gauge", a_stepper.gauge);
		ArchiveItemUI(a_ar, a_name + "Down", a_stepper.down);
		ArchiveItemUI(a_ar, a_name + "Up", a_stepper.up);
	}

	//======================================================================================
	// 選択肢を切り替える行
	//======================================================================================
	void ScrollPanelBase::PlaceSelector(Engine::GameObject::ObjectContext& a_context, const OptionSelectorUI& a_selector)
	{
		PlaceItem(a_context, a_selector.display);
		PlaceItem(a_context, a_selector.prev);
		PlaceItem(a_context, a_selector.next);
	}

	int ScrollPanelBase::UpdateSelector(Engine::GameObject::ObjectContext& a_context, const OptionSelectorUI& a_selector)
	{
		int _step = 0;

		UIBase* _pPrev = FindUI(a_context, a_selector.prev.guid);
		if (_pPrev && _pPrev->IsClicked()) --_step;

		UIBase* _pNext = FindUI(a_context, a_selector.next.guid);
		if (_pNext && _pNext->IsClicked()) ++_step;

		return _step;
	}

	void ScrollPanelBase::SetSelectorText(Engine::GameObject::ObjectContext& a_context, const OptionSelectorUI& a_selector, const std::string& a_text)
	{
		SetUIText(a_context, a_selector.display.guid, a_selector.valueTextId, a_text);
	}

	void ScrollPanelBase::SetUIText(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid, uint32_t a_textId, const std::string& a_text)
	{
		if (a_textId == 0) return;

		UIBase* _pUI = FindUI(a_context, a_guid);
		if (!_pUI) return;

		Decoration::Decoration* _pDecoration = _pUI->FindDecorationById(a_textId);
		Decoration::TextData* _pText = _pDecoration ? _pDecoration->RefText() : nullptr;
		if (!_pText) return;

		// 飾りの文字は保存される値だが、毎フレーム書き直すので残っても害はない
		if (_pText->text != a_text) _pText->text = a_text;
	}

	//======================================================================================
	// プルダウンで1つ選ぶ行
	//======================================================================================
	void ScrollPanelBase::PlaceDropdown(Engine::GameObject::ObjectContext& a_context, const OptionDropdownUI& a_dropdown)
	{
		PlaceItem(a_context, a_dropdown.label);
		PlaceItem(a_context, a_dropdown.field);

		// 候補 : 行のすぐ下から縦に並べる。行と一緒にスクロールするが、切り抜きはしない
		// (パネルの下端に近い行で開いたとき、下の候補が切れて選べなくなるため)
		const Math::Vector2 _rowTopLeft = {
			m_layout.pos.x + a_dropdown.candidateOffsetX,
			m_layout.pos.y + m_layoutCursorY - m_scrollOffset + m_layout.itemSize.y
		};

		for (size_t _i = 0; _i < a_dropdown.candidateGUIDVec.size(); ++_i)
		{
			UIBase* _pCandidate = FindUI(a_context, a_dropdown.candidateGUIDVec[_i]);
			if (!_pCandidate) continue;

			const Math::Vector2 _topLeft = {
				_rowTopLeft.x,
				_rowTopLeft.y + a_dropdown.candidateSize.y * static_cast<float>(_i)
			};
			PlaceUIRect(*_pCandidate, _topLeft, a_dropdown.candidateSize);
			_pCandidate->ClearClipRect();
		}
	}

	void ScrollPanelBase::ArchiveDropdown(Engine::Persistence::Archive& a_ar, const std::string& a_name, OptionDropdownUI& a_dropdown)
	{
		ArchiveItemUI(a_ar, a_name + "Label", a_dropdown.label);
		ArchiveItemUI(a_ar, a_name + "Field", a_dropdown.field);
		a_ar.Field(a_name + "ValueTextId", a_dropdown.valueTextId);
		a_ar.GUIDVectorField(a_name + "Candidates", a_dropdown.candidateGUIDVec);
		a_ar.Field(a_name + "CandidateTextId", a_dropdown.candidateTextId);
		a_ar.Field(a_name + "CandidateOffsetX", a_dropdown.candidateOffsetX);
		a_ar.Field(a_name + "CandidateSize", a_dropdown.candidateSize);
	}

	void ScrollPanelBase::DrawDropdownInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label, OptionDropdownUI& a_dropdown)
	{
		Engine::EditorField::TreeScope _tree{ a_label };
		if (!_tree) return;

		DrawItemUIInspector(a_context, "Label", a_dropdown.label);
		Engine::EditorField::Tooltip("見出し(押せない UI)");

		DrawItemUIInspector(a_context, "Field", a_dropdown.field);
		Engine::EditorField::Tooltip("選択欄(UIButton)。押すと候補が開閉する。右端に ▼ の飾りを置く");

		// 名前を書き込む飾りは、選択欄の文字の飾りから選ぶ
		if (UIBase* _pField = FindUI(a_context, a_dropdown.field.guid))
		{
			_pField->DrawDecorationPicker("ValueText", a_dropdown.valueTextId, Decoration::EDecorationType::Text);
			Engine::EditorField::Tooltip("今の選択の名前を書き込む文字の飾り");
		}

		Engine::EditorField::Field("CandidateOffsetX", a_dropdown.candidateOffsetX, 0.5f);
		Engine::EditorField::Tooltip("候補の左端 : 行の左端からのずれ(px)");
		Engine::EditorField::Field("CandidateSize", a_dropdown.candidateSize, 0.5f, 0.0f, 4096.0f);
		Engine::EditorField::Field("CandidateTextId", a_dropdown.candidateTextId);
		Engine::EditorField::Tooltip("候補の名前を書き込む文字の飾りの番号(どの候補も同じ番号にしておく)");

		// 候補のボタン : 上から選択肢の並びで使う
		int _removeIndex = -1;
		for (size_t _i = 0; _i < a_dropdown.candidateGUIDVec.size(); ++_i)
		{
			Engine::EditorField::IDScope _id(static_cast<int>(_i));
			const std::string _label = "Candidate" + std::to_string(_i);
			DrawUIPicker(a_context, _label.c_str(), a_dropdown.candidateGUIDVec[_i]);
			Engine::EditorField::SameLine();
			if (Engine::EditorField::DeleteSmallButton("x")) _removeIndex = static_cast<int>(_i);
		}
		if (_removeIndex >= 0)
		{
			a_dropdown.candidateGUIDVec.erase(a_dropdown.candidateGUIDVec.begin() + _removeIndex);
		}
		if (Engine::EditorField::CreateSmallButton("Add Candidate"))
		{
			a_dropdown.candidateGUIDVec.push_back({});
		}
	}

	void ScrollPanelBase::ArchiveSelector(Engine::Persistence::Archive& a_ar, const std::string& a_name, OptionSelectorUI& a_selector)
	{
		ArchiveItemUI(a_ar, a_name + "Display", a_selector.display);
		a_ar.Field(a_name + "ValueTextId", a_selector.valueTextId);
		ArchiveItemUI(a_ar, a_name + "Prev", a_selector.prev);
		ArchiveItemUI(a_ar, a_name + "Next", a_selector.next);
	}

	void ScrollPanelBase::DrawSelectorInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label, OptionSelectorUI& a_selector)
	{
		Engine::EditorField::TreeScope _tree{ a_label };
		if (!_tree) return;

		DrawItemUIInspector(a_context, "Display", a_selector.display);
		Engine::EditorField::Tooltip("見出しと今の選択の名前を出す UI");

		// 名前を書き込む飾りは、Display の UI の文字の飾りから選ぶ
		if (UIBase* _pDisplay = FindUI(a_context, a_selector.display.guid))
		{
			_pDisplay->DrawDecorationPicker("ValueText", a_selector.valueTextId, Decoration::EDecorationType::Text);
			Engine::EditorField::Tooltip("今の選択の名前を書き込む文字の飾り");
		}

		DrawItemUIInspector(a_context, "Prev", a_selector.prev);
		DrawItemUIInspector(a_context, "Next", a_selector.next);
	}

	size_t ScrollPanelBase::StepIndex(size_t a_index, int a_step, size_t a_count)
	{
		if (a_count == 0) return 0;

		const int _count = static_cast<int>(a_count);
		const int _index = (static_cast<int>(a_index) + a_step) % _count;
		return static_cast<size_t>(_index < 0 ? _index + _count : _index);
	}

	void ScrollPanelBase::DrawStepperInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label, OptionStepperUI& a_stepper)
	{
		Engine::EditorField::TreeScope _tree{ a_label };
		if (!_tree) return;

		DrawItemUIInspector(a_context, "Gauge", a_stepper.gauge);
		Engine::EditorField::Tooltip("今の値を出す UIGauge(Source は Manual にしておく)");
		DrawItemUIInspector(a_context, "Down", a_stepper.down);
		DrawItemUIInspector(a_context, "Up", a_stepper.up);
	}

	//======================================================================================
	// ホイール
	//--------------------------------------------------------------------------------------
	// 範囲の上で回されたときだけ受け取る。
	// 出ているパネルは1つだけだが、範囲の外(タブや戻るボタンの上)で回したぶんまで
	// 拾うと、手元を見ていないところで一覧が動いてしまう
	//======================================================================================
	void ScrollPanelBase::UpdateWheel(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pInputManager) return;

		const float _wheel = a_context.pServices->pInputManager->GetWheelDelta();
		if (_wheel == 0.0f) return;

		Math::Vector2 _cursor = {};
		if (!UIInteraction::CalcCursorUIPos(a_context, _cursor)) return;
		if (!IsInsideLayout(_cursor)) return;

		// 奥へ回す(正)と上へ戻る
		AddScrollOffset(-_wheel * m_wheelStep);
	}

	//======================================================================================
	// スクロールバー
	//======================================================================================
	void ScrollPanelBase::UpdateScrollBar(Engine::GameObject::ObjectContext& a_context)
	{
		Math::Vector2 _cursor = {};
		const bool _hasCursor = UIInteraction::CalcCursorUIPos(a_context, _cursor);

		//------------------------------------------------------------------
		// つまみを掴んで動かす
		//
		// 押している間(IsPressed)はカーソルがつまみから外れても続くので、
		// 掴んだ瞬間の位置からの差だけで動かす。つまみの今の位置から測ると、
		// 端で止まったぶんがずれとして残り、戻したときに手元とつまみが離れる
		//------------------------------------------------------------------
		UIBase* _pThumb = FindUI(a_context, m_barThumbGUID);
		if (_pThumb && _pThumb->IsPressed() && _hasCursor)
		{
			if (!m_isDragging)
			{
				m_isDragging = true;
				m_dragStartCursorY = _cursor.y;
				m_dragStartOffset = m_scrollOffset;
			}

			const float _movable = GetThumbMovable();
			if (_movable > 0.0f)
			{
				const float _rate = (_cursor.y - m_dragStartCursorY) / _movable;
				SetScrollOffset(m_dragStartOffset + _rate * GetMaxScrollOffset());
			}
			return;
		}
		m_isDragging = false;

		//------------------------------------------------------------------
		// 溝を押したら、つまみの真ん中がそこへ来るように飛ぶ
		//
		// つまみは溝より手前(layer が大きい)に置くこと。重なったところは手前の
		// 1つだけが反応するので、つまみを掴んだつもりで溝が押されることはない
		//------------------------------------------------------------------
		UIBase* _pTrack = FindUI(a_context, m_barTrackGUID);
		if (_pTrack && _pTrack->IsClicked() && _hasCursor)
		{
			const float _movable = GetThumbMovable();
			if (_movable > 0.0f)
			{
				const float _thumbTop = _cursor.y - m_layout.pos.y - m_thumbLength * 0.5f;
				SetScrollOffset(_thumbTop / _movable * GetMaxScrollOffset());
			}
		}
	}

	void ScrollPanelBase::PlaceScrollBar(Engine::GameObject::ObjectContext& a_context)
	{
		const float _barX = m_layout.pos.x + m_layout.size.x - m_barWidth;

		// 項目が範囲に収まっていて動かせないときは、バーごと出さない。
		// 出したままだと、掴んでもホイールを回しても動かない壊れたバーに見える
		const bool _isScrollable = GetMaxScrollOffset() > 0.0f;

		// 溝 : 描画範囲の右端に縦いっぱい
		if (UIBase* _pTrack = FindUI(a_context, m_barTrackGUID))
		{
			_pTrack->SetVisible(_isScrollable);
			PlaceUIRect(*_pTrack, { _barX, m_layout.pos.y }, { m_barWidth, m_layout.size.y });
		}

		// つまみ : 溝の中を、スクロールの割合だけ下がる
		if (UIBase* _pThumb = FindUI(a_context, m_barThumbGUID))
		{
			_pThumb->SetVisible(_isScrollable);

			const float _maxOffset = GetMaxScrollOffset();
			const float _rate = (_maxOffset > 0.0f) ? (m_scrollOffset / _maxOffset) : 0.0f;
			const float _top = m_layout.pos.y + GetThumbMovable() * _rate;

			PlaceUIRect(*_pThumb, { _barX, _top }, { m_barWidth, m_thumbLength });
		}
	}

	float ScrollPanelBase::GetThumbMovable() const
	{
		return std::max(m_layout.size.y - m_thumbLength, 0.0f);
	}

	Engine::Graphics::UIClipRect ScrollPanelBase::MakeContentClipRect() const
	{
		// 右端のバーのぶんは除く(項目がバーの下へ潜り込まないように)
		Engine::Graphics::UIClipRect _clip = {};
		_clip.min = m_layout.pos;
		_clip.max = {
			m_layout.pos.x + std::max(m_layout.size.x - m_barWidth, 0.0f),
			m_layout.pos.y + m_layout.size.y
		};
		return _clip;
	}

	bool ScrollPanelBase::IsInsideLayout(const Math::Vector2& a_uiPos) const
	{
		if (a_uiPos.x < m_layout.pos.x || a_uiPos.y < m_layout.pos.y) return false;
		if (a_uiPos.x > m_layout.pos.x + m_layout.size.x) return false;
		if (a_uiPos.y > m_layout.pos.y + m_layout.size.y) return false;
		return true;
	}

	//======================================================================================
	// 引く
	//======================================================================================
	UIBase* ScrollPanelBase::FindUI(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid)
	{
		if (!a_guid.IsValid() || !a_context.pObjectManager) return nullptr;
		return Core::TypeInfo::Cast<UIBase>(a_context.pObjectManager->FindByGUID(a_guid));
	}

	void ScrollPanelBase::PlaceUIRect(UIBase& a_ui, const Math::Vector2& a_topLeft, const Math::Vector2& a_size)
	{
		// 位置は画面の座標で決め、親から見た値へ直して入れる
		UIAnchor& _anchor = a_ui.RefAnchor();
		_anchor.pixelSize = a_size;
		a_ui.SetWorldPixelPos({
			a_topLeft.x + a_size.x * _anchor.pivot.x,
			a_topLeft.y + a_size.y * _anchor.pivot.y
		});
	}

	InstanceResource::UserDataResource* ScrollPanelBase::FindUserData(Engine::GameObject::ObjectContext& a_context)
	{
		return InstanceResource::UserDataResource::Find(a_context.pWorld);
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void ScrollPanelBase::Archive(Engine::Persistence::Archive& a_ar)
	{
		// 基底のぶんと派生のぶんは区切りを分けてある。
		// どちらへフィールドを足しても互いの読み出しはずれない。足すときは各区切りの末尾へ
		{
			Engine::Persistence::ArchiveSection _section(a_ar, "ScrollPanelBase");

			a_ar.GUIDField("RootPanelGUID", m_rootPanelGUID);
			a_ar.GUIDField("BarTrackGUID", m_barTrackGUID);
			a_ar.GUIDField("BarThumbGUID", m_barThumbGUID);
			a_ar.Field("BarWidth", m_barWidth);
			a_ar.Field("ThumbLength", m_thumbLength);
			a_ar.Field("WheelStep", m_wheelStep);
		}

		{
			Engine::Persistence::ArchiveSection _section(a_ar, GetPanelName());
			ArchiveItems(a_ar);
		}
	}

	void ScrollPanelBase::ArchiveItemUI(Engine::Persistence::Archive& a_ar, const std::string& a_name, OptionItemUI& a_item)
	{
		a_ar.GUIDField(a_name + "GUID", a_item.guid);
		a_ar.Field(a_name + "OffsetX", a_item.offsetX);
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void ScrollPanelBase::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Header("Scroll");

		DrawUIPicker(a_context, "RootPanel", m_rootPanelGUID);
		Engine::EditorField::Tooltip("このパネルの出し入れの単位(UIElement など)。項目とバーはヒエラルキーでこの下に置く");

		DrawUIPicker(a_context, "BarTrack", m_barTrackGUID);
		Engine::EditorField::Tooltip("バーの溝。描画範囲の右端に縦いっぱいで置かれ、押すとつまみがそこへ飛ぶ");

		DrawUIPicker(a_context, "BarThumb", m_barThumbGUID);
		Engine::EditorField::Tooltip("バーのつまみ。掴んで動かす。溝より layer を大きくしておくこと");

		Engine::EditorField::Field("BarWidth", m_barWidth, 0.5f, 0.0f, 256.0f);
		Engine::EditorField::Field("ThumbLength", m_thumbLength, 0.5f, 1.0f, 1024.0f);
		Engine::EditorField::Tooltip("つまみの長さ(px)。飾りの大きさもこれに合わせて作ること");
		Engine::EditorField::Field("WheelStep", m_wheelStep, 1.0f, 0.0f, 1024.0f);
		Engine::EditorField::Tooltip("ホイール1ノッチで動く量(px)");

		// 実行中の状態は表示のみ
		Engine::EditorField::Value("ScrollOffset", "%.1f / %.1f", m_scrollOffset, GetMaxScrollOffset());
		Engine::EditorField::Value("ContentLength", "%.1f", m_contentLength);

		Engine::EditorField::Header("Items");
		DrawItemsInspector(a_context);
	}

	bool ScrollPanelBase::DrawItemUIInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label, OptionItemUI& a_item)
	{
		Engine::EditorField::IDScope _id(a_label);

		bool _isChanged = DrawUIPicker(a_context, a_label, a_item.guid);
		_isChanged |= Engine::EditorField::Field("OffsetX", a_item.offsetX, 0.5f);
		Engine::EditorField::Tooltip("行の左端からのずれ(px)");
		return _isChanged;
	}

	bool ScrollPanelBase::DrawUIPicker(Engine::GameObject::ObjectContext& a_context, const char* a_label, Core::GUID& a_inoutGUID)
	{
		// 今の選択 : 種類と GUID を並べて出す
		std::string _current = "None";
		if (UIBase* _pCurrent = FindUI(a_context, a_inoutGUID))
		{
			_current = std::string(_pCurrent->GetDisplayName()) + " " + a_inoutGUID.String();
		}
		else if (a_inoutGUID.IsValid())
		{
			_current = a_inoutGUID.String() + " (見つかりません)";
		}

		Engine::EditorField::ComboScope _combo(a_label, _current.c_str());
		if (!_combo) return false;

		bool _isChanged = false;

		if (Engine::EditorField::Selectable("None", !a_inoutGUID.IsValid()))
		{
			a_inoutGUID = {};
			_isChanged = true;
		}

		if (!a_context.pObjectManager) return _isChanged;

		const auto& _objectVec = a_context.pObjectManager->GetObjects();
		for (size_t _i = 0; _i < _objectVec.size(); ++_i)
		{
			UIBase* _pUI = Core::TypeInfo::Cast<UIBase>(_objectVec[_i].get());
			if (!_pUI) continue;

			// 同名でもIDがぶつからないようにする
			Engine::EditorField::IDScope _id(static_cast<int>(_i));

			const std::string _name = std::string(_pUI->GetDisplayName()) + " " + _pUI->GetGUID().String();
			const bool _isSelected = (a_inoutGUID == _pUI->GetGUID());
			if (Engine::EditorField::Selectable(_name.c_str(), _isSelected))
			{
				a_inoutGUID = _pUI->GetGUID();
				_isChanged = true;
			}
			if (_isSelected) Engine::EditorField::SetItemDefaultFocus();
		}
		return _isChanged;
	}
}
