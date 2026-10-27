#include "ScrollBar.h"

#include "Engine/EditorField/EditorField.h"

#include "Application/Object/ObjectPicker.h"
#include "Application/Object/UI/UIBase.h"

//==========================================================================================
// ScrollBar
//
// 割合とつまみの位置は
//
//   つまみの先頭 = 溝の先頭 + (溝の長さ - つまみの長さ) × 割合
//
// で結ぶ。割合が 0 でつまみが溝の先頭に、1 で末尾にぴったり収まる。
//==========================================================================================
namespace App::Object
{
	//======================================================================================
	// 置く
	//======================================================================================
	void ScrollBar::Place(Engine::GameObject::ObjectContext& a_context,
		const Math::Vector2& a_trackTopLeft, const Math::Vector2& a_trackSize,
		float a_rate, const Engine::Graphics::UIClipRect* a_pClip)
	{
		m_trackTopLeft = a_trackTopLeft;
		m_trackSize = a_trackSize;
		m_opClipRect = a_pClip ? std::optional<Engine::Graphics::UIClipRect>(*a_pClip) : std::nullopt;

		if (UIBase* _pTrack = UIBase::FindUI(a_context, m_trackGUID))
		{
			_pTrack->SetWorldPixelRect(m_trackTopLeft, m_trackSize);
			if (m_opClipRect) _pTrack->SetClipRect(*m_opClipRect);
			else _pTrack->ClearClipRect();
		}

		PlaceThumb(a_context, a_rate);
	}

	void ScrollBar::PlaceThumb(Engine::GameObject::ObjectContext& a_context, float a_rate)
	{
		UIBase* _pThumb = UIBase::FindUI(a_context, m_thumbGUID);
		if (!_pThumb) return;

		// 太さは溝と同じ、長さは決めた値。溝の中を割合だけ進む
		const float _along = GetThumbMovable() * std::clamp(a_rate, 0.0f, 1.0f);

		Math::Vector2 _topLeft = m_trackTopLeft;
		Math::Vector2 _size = m_trackSize;
		if (m_axis == EScrollBarAxis::Horizontal)
		{
			_topLeft.x += _along;
			_size.x = m_thumbLength;
		}
		else
		{
			_topLeft.y += _along;
			_size.y = m_thumbLength;
		}

		_pThumb->SetWorldPixelRect(_topLeft, _size);
		if (m_opClipRect) _pThumb->SetClipRect(*m_opClipRect);
		else _pThumb->ClearClipRect();
	}

	//======================================================================================
	// 操作
	//======================================================================================
	bool ScrollBar::Update(Engine::GameObject::ObjectContext& a_context, float& a_inoutRate)
	{
		UIBase* _pThumb = UIBase::FindUI(a_context, m_thumbGUID);
		UIBase* _pTrack = UIBase::FindUI(a_context, m_trackGUID);

		const bool _isThumbPressed = _pThumb && _pThumb->IsPressed();
		const bool _isTrackPressed = _pTrack && _pTrack->IsPressed();

		Math::Vector2 _cursor = {};
		if (!(_isThumbPressed || _isTrackPressed) || !UIInteraction::CalcCursorUIPos(a_context, _cursor))
		{
			m_isDragging = false;
			return false;
		}

		const float _movable = GetThumbMovable();
		if (_movable <= 0.0f) return false;

		const float _cursorAlong = GetAlongAxis(_cursor);
		const float _before = a_inoutRate;

		//------------------------------------------------------------------
		// 掴み始め
		//
		// 溝を押したときは、つまみの真ん中がカーソルへ来るように飛ばしてから掴む。
		// そのまま引けば続けて動かせる(押して離すだけなら飛ぶだけ)
		//------------------------------------------------------------------
		if (!m_isDragging)
		{
			m_isDragging = true;
			m_dragStartCursor = _cursorAlong;
			m_dragStartRate = a_inoutRate;

			if (!_isThumbPressed)
			{
				const float _thumbHead = _cursorAlong - GetAlongAxis(m_trackTopLeft) - m_thumbLength * 0.5f;
				m_dragStartRate = std::clamp(_thumbHead / _movable, 0.0f, 1.0f);
			}
		}

		//------------------------------------------------------------------
		// 掴んでいる間
		//
		// 押している間(IsPressed)はカーソルがつまみから外れても続くので、
		// 掴んだ瞬間の位置からの差だけで動かす。つまみの今の位置から測ると、
		// 端で止まったぶんがずれとして残り、戻したときに手元とつまみが離れる
		//------------------------------------------------------------------
		const float _rate = m_dragStartRate + (_cursorAlong - m_dragStartCursor) / _movable;
		a_inoutRate = SnapRate(std::clamp(_rate, 0.0f, 1.0f));

		return a_inoutRate != _before;
	}

	void ScrollBar::SetVisible(Engine::GameObject::ObjectContext& a_context, bool a_isVisible)
	{
		if (!a_isVisible) m_isDragging = false;

		if (UIBase* _pTrack = UIBase::FindUI(a_context, m_trackGUID)) _pTrack->SetVisible(a_isVisible);
		if (UIBase* _pThumb = UIBase::FindUI(a_context, m_thumbGUID)) _pThumb->SetVisible(a_isVisible);
	}

	//======================================================================================
	// 内部
	//======================================================================================
	float ScrollBar::GetAlongAxis(const Math::Vector2& a_value) const
	{
		return (m_axis == EScrollBarAxis::Horizontal) ? a_value.x : a_value.y;
	}

	float ScrollBar::GetThumbMovable() const
	{
		return std::max(GetAlongAxis(m_trackSize) - m_thumbLength, 0.0f);
	}

	float ScrollBar::SnapRate(float a_rate) const
	{
		if (!m_opPartitionOption || m_opPartitionOption->partitionCount == 0) return a_rate;

		const float _count = static_cast<float>(m_opPartitionOption->partitionCount);
		return std::round(a_rate * _count) / _count;
	}

	//======================================================================================
	// アーカイブ
	//======================================================================================
	void ScrollBar::Archive(Engine::Persistence::Archive& a_ar, const std::string& a_name)
	{
		a_ar.GUIDField(a_name + "TrackGUID", m_trackGUID);
		a_ar.GUIDField(a_name + "ThumbGUID", m_thumbGUID);
		a_ar.Field(a_name + "Axis", m_axis);
		a_ar.Field(a_name + "ThumbLength", m_thumbLength);

		// 分割 : 使うかどうかと分割数
		bool _isPartition = m_opPartitionOption.has_value();
		uint32_t _partitionCount = _isPartition ? m_opPartitionOption->partitionCount : PartitionOption{}.partitionCount;
		a_ar.Field(a_name + "IsPartition", _isPartition);
		a_ar.Field(a_name + "PartitionCount", _partitionCount);

		if (a_ar.IsLoading())
		{
			m_opPartitionOption = _isPartition ? std::optional<PartitionOption>(PartitionOption{ _partitionCount }) : std::nullopt;
		}
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void ScrollBar::DrawInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label)
	{
		Engine::EditorField::TreeScope _tree{ a_label };
		if (!_tree) return;

		Picker::DrawCombo<UIBase>("Track", a_context.pObjectManager, m_trackGUID);
		Engine::EditorField::Tooltip("バーの溝。押すとつまみがそこへ飛び、そのまま引ける");

		Picker::DrawCombo<UIBase>("Thumb", a_context.pObjectManager, m_thumbGUID);
		Engine::EditorField::Tooltip("バーのつまみ。掴んで動かす。溝の子に置き、溝より layer を大きくしておくこと");

		Engine::EditorField::Field("Axis", m_axis);
		Engine::EditorField::Field("ThumbLength", m_thumbLength, 0.5f, 1.0f, 1024.0f);
		Engine::EditorField::Tooltip("つまみの長さ(軸の向き, px)。飾りの大きさもこれに合わせて作ること");

		bool _isPartition = m_opPartitionOption.has_value();
		if (Engine::EditorField::Field("Partition", _isPartition))
		{
			m_opPartitionOption = _isPartition ? std::optional<PartitionOption>(PartitionOption{}) : std::nullopt;
		}
		Engine::EditorField::Tooltip("分割ごとに吸い付かせる(滑らかではなくカクカクと動く)");

		if (m_opPartitionOption)
		{
			Engine::EditorField::Field("PartitionCount", m_opPartitionOption->partitionCount);
			m_opPartitionOption->partitionCount = std::max(m_opPartitionOption->partitionCount, 1u);
		}

		Engine::EditorField::Value("Dragging", "%s", m_isDragging ? "Yes" : "No");
	}
}
