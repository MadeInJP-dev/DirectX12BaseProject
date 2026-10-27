#pragma once

// 切り抜きの矩形を値で持つ
#include "Engine/Graphics/Frame/DrawList/UIData.h"

namespace Engine::GameObject
{
	struct ObjectContext;
}

namespace App::Object
{
	// スクロールバーの向き
	enum class EScrollBarAxis : uint32_t
	{
		Horizontal,		// 横 : 左が 0、右が 1(数値の行)
		Vertical,		// 縦 : 上が 0、下が 1(一覧のスクロール)
	};

	//========================================================================================
	// スクロールバー
	//
	// 0〜1 の割合を、溝とつまみで動かす部品。
	// 何の割合か(スクロール位置・音量など)は持ち主が決め、ここは割合を動かすだけ。
	//
	// ・溝とつまみはシーンに置いた別の UI(UIElement など)で、ここは GUID で指す。
	//   見た目は各 UI の飾りが持ち、ここは位置・判定の大きさ・切り抜きを毎フレーム入れるだけ。
	//   つまみは溝より手前(layer が大きい)に置くこと。重なったところは手前の1つだけが反応する
	// ・つまみを掴んで動かす / 溝を押すとつまみの真ん中がそこへ飛ぶ
	// ・分割数を入れると、割合が分割ごとに吸い付く(滑らかではなくカクカクと動く)
	//
	// 使い手 : 設定画面のパネル(縦のスクロール)・NumericMutate(横の数値)
	//========================================================================================
	class ScrollBar final
	{
	public:

		ScrollBar() = default;
		explicit ScrollBar(EScrollBarAxis a_axis) : m_axis(a_axis) {}

		/// <summary>
		/// 置く : 溝を矩形で置き、つまみを今の割合の位置へ置く
		/// </summary>
		/// <param name="a_trackTopLeft">溝の左上(画面の座標, px)</param>
		/// <param name="a_trackSize">溝の大きさ(px)。つまみの太さも同じになる</param>
		/// <param name="a_rate">今の割合(0〜1)</param>
		/// <param name="a_pClip">切り抜く矩形(無ければ切り抜かない)</param>
		void Place(Engine::GameObject::ObjectContext& a_context,
			const Math::Vector2& a_trackTopLeft, const Math::Vector2& a_trackSize,
			float a_rate, const Engine::Graphics::UIClipRect* a_pClip = nullptr);

		// つまみだけを置き直す(Place で入れた溝の矩形のまま)。操作した直後に今の値へ合わせる用
		void PlaceThumb(Engine::GameObject::ObjectContext& a_context, float a_rate);

		/// <summary>
		/// 操作 : つまみのドラッグと、溝を押したときの飛び
		/// </summary>
		/// <param name="a_inoutRate">今の割合。動かしたら書き換える(分割数があれば吸い付いた値)</param>
		/// <returns>割合を動かしたら true</returns>
		bool Update(Engine::GameObject::ObjectContext& a_context, float& a_inoutRate);

		// 出し入れ : 溝を切り替える(つまみは溝の子に置くので一緒に出入りする)
		void SetVisible(Engine::GameObject::ObjectContext& a_context, bool a_isVisible);

		// つまみを掴んでいるか
		bool IsDragging() const { return m_isDragging; }

		// 項目移動(キー・パッド)で溝かつまみが選ばれているか
		bool IsNavFocused(const Engine::GameObject::ObjectContext& a_context) const;

		// 掴んでいたことを忘れる(隠したとき・出し直したときに掴んだままにならないように)
		void CancelDrag() { m_isDragging = false; }

		// アーカイブ : a_name を頭に付けたキーで書く(<name>TrackGUID など)
		void Archive(Engine::Persistence::Archive& a_ar, const std::string& a_name);

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label);

	private:

		// 軸の向きの成分を取り出す
		float GetAlongAxis(const Math::Vector2& a_value) const;

		// つまみが動ける距離(px) : 溝の長さからつまみの長さを引いたもの
		float GetThumbMovable() const;

		// 分割数があれば、割合を分割ごとに吸い付かせる
		float SnapRate(float a_rate) const;

	private:

		//------------------------------------------------------------------------------
		// 分割設定 : 割り当てると滑らかではなくカクカクとした動きになる
		//------------------------------------------------------------------------------
		struct PartitionOption
		{
			uint32_t partitionCount = 10;		// 分割数 : 割合が 1/分割数 ごとにすいつく
		};

	private:

		//------------------------------------------------------------------------------
		// 必須設定(保存される)
		//------------------------------------------------------------------------------
		Core::GUID m_trackGUID = {};		// バーの溝。押すとつまみがそこへ飛ぶ
		Core::GUID m_thumbGUID = {};		// バーのつまみ。掴んで動かす(溝より手前の layer に置く)

		EScrollBarAxis m_axis = EScrollBarAxis::Vertical;
		float m_thumbLength = 48.0f;		// つまみの長さ(軸の向き, px)

		//------------------------------------------------------------------------------
		// 任意設定(保存される)
		//------------------------------------------------------------------------------
		std::optional<PartitionOption> m_opPartitionOption = std::nullopt;

		//------------------------------------------------------------------------------
		// 状態(保存しない)
		//------------------------------------------------------------------------------
		Math::Vector2 m_trackTopLeft = {};	// 今フレームの溝の左上(Place で入る)
		Math::Vector2 m_trackSize = {};		// 今フレームの溝の大きさ
		std::optional<Engine::Graphics::UIClipRect> m_opClipRect = std::nullopt;	// 今フレームの切り抜き

		// つまみのドラッグ : 掴んだ瞬間の位置からの差で動かす
		bool m_isDragging = false;
		float m_dragStartCursor = 0.0f;		// 掴んだときのカーソル(軸の向き, px)
		float m_dragStartRate = 0.0f;		// 掴んだときの割合
	};
}
