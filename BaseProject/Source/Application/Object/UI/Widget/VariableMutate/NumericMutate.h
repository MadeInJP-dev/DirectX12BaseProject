#pragma once

#include "ScrollBar/ScrollBar.h"

namespace Engine::GameObject
{
	struct ObjectContext;
}

namespace App::Object
{
	// 数値の操作方法(エディターで選ぶ)
	enum class ENumericMutateMode : uint32_t
	{
		Button,		// 下げる・上げるボタンで刻みずつ
		ScrollBar,	// 横のスクロールバーを掴んで動かす
	};

	//===============================================================================================
	// 可変可能な数値をいじるためのUIウィジェット
	//
	// エディターからボタン操作、スクロールバー操作が選べる
	// 受け取った値を任意の操作方法で処理するのみで値を使っての処理はメインのクラスでやるため
	// 数値の操作以外の処理はしない。
	//
	// ・見た目はシーンに置いた別の UI で作り、ここは GUID で指す。
	//     Gauge     : 今の値を出す UIGauge(Source は Manual)。行の土台で、他の UI はこの子に置く
	//     Down / Up : ボタン操作のときの下げる・上げるボタン
	//     Bar       : スクロールバー操作のときの溝とつまみ(溝はゲージの子、つまみは溝の子)
	//   使わないほうの操作方法の UI は隠す(切り替えても置いた UI は消えない)。
	// ・範囲と刻みは持ち主が Update に渡す(何の値かはここでは知らない)。
	//   ボタンは刻みずつ動かす。スクロールバーは範囲の中を滑らかに動かす(分割はバーの設定)
	// ・置く場所は持ち主が行の左上と大きさで渡す。各 UI の横の位置はそこからのずれ(OffsetX)
	//===============================================================================================
	class NumericMutate
	{
	public:

		/// <summary>
		/// 並べる : 行の左上と大きさから、今の操作方法の UI を置く
		/// </summary>
		/// <param name="a_rowTopLeft">行の左上(画面の座標, px)</param>
		/// <param name="a_rowSize">行の大きさ(px)。ピボットのずらしと、バーの縦の真ん中合わせに使う</param>
		/// <param name="a_pClip">切り抜く矩形(無ければ切り抜かない)</param>
		void Place(Engine::GameObject::ObjectContext& a_context,
			const Math::Vector2& a_rowTopLeft, const Math::Vector2& a_rowSize,
			const Engine::Graphics::UIClipRect* a_pClip = nullptr);

		/// <summary>
		/// 操作 : 値を受け取り、操作されたら書き換える。ゲージにも今の値を出す
		/// </summary>
		/// <param name="a_inoutValue">今の値。操作されたら範囲に詰めた新しい値</param>
		/// <param name="a_min">範囲の下限</param>
		/// <param name="a_max">範囲の上限</param>
		/// <param name="a_step">ボタン1回で動く量</param>
		/// <returns>値を変えたら true</returns>
		bool Update(Engine::GameObject::ObjectContext& a_context, float& a_inoutValue, float a_min, float a_max, float a_step);

		// 今の操作方法
		ENumericMutateMode GetMode() const { return m_mode; }

		// 項目移動(キー・パッド)で行の UI のどれかが選ばれているか(選んだ行へスクロールを追わせる用)
		bool IsNavFocused(const Engine::GameObject::ObjectContext& a_context) const;

		// アーカイブ : a_name を頭に付けたキーで書く(<name>GaugeGUID・<name>BarTrackGUID など)
		void Archive(Engine::Persistence::Archive& a_ar, const std::string& a_name);

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label);

	private:

		// 行の中に置く UI 1つ : GUID と、行の左端からのずれ
		struct RowItem
		{
			Core::GUID guid = {};
			float offsetX = 0.0f;
		};

		// 行の中へ UI を置く(縦はピボットに合わせて行の大きさでずらす)
		void PlaceRowItem(Engine::GameObject::ObjectContext& a_context, const RowItem& a_item) const;

		// ボタン操作 : このフレームに押されたボタン(下げるなら -1、上げるなら +1)
		int UpdateButtons(Engine::GameObject::ObjectContext& a_context) const;

		/// <summary>
		/// スクロールバー操作のキー・パッド : バーが項目移動で選ばれている間、左右で刻みずつ動かす
		/// </summary>
		/// <returns>このフレームに動かす向き(下げるなら -1、上げるなら +1)</returns>
		/// <remarks>選ばれている間は左右での項目移動を止める(上下では行を移れる)。押しっぱなしで繰り返す</remarks>
		int UpdateNavStep(Engine::GameObject::ObjectContext& a_context);

		// 操作方法に合わせて UI を出し入れする
		void ApplyModeVisible(Engine::GameObject::ObjectContext& a_context);

		// 行の中の UI 1つの保存・インスペクター
		static void ArchiveRowItem(Engine::Persistence::Archive& a_ar, const std::string& a_name, RowItem& a_item);
		static void DrawRowItemInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label, RowItem& a_item);

	private:

		//------------------------------------------------------------------------------
		// 設定(保存される)
		//------------------------------------------------------------------------------
		ENumericMutateMode m_mode = ENumericMutateMode::Button;

		RowItem m_gauge = {};		// 今の値を出す UIGauge(行の土台)

		// ボタン操作
		RowItem m_down = {};		// 下げるボタン
		RowItem m_up = {};			// 上げるボタン

		// スクロールバー操作
		ScrollBar m_scrollBar{ EScrollBarAxis::Horizontal };
		float m_barOffsetX = 0.0f;						// 溝の左端 : 行の左端からのずれ(px)
		Math::Vector2 m_barSize = { 400.0f, 40.0f };	// 溝の大きさ(px)。縦は行の真ん中に合わせる

		//------------------------------------------------------------------------------
		// 状態(保存しない)
		//------------------------------------------------------------------------------
		Math::Vector2 m_rowTopLeft = {};	// 今フレームの行の左上(Place で入る)
		Math::Vector2 m_rowSize = {};		// 今フレームの行の大きさ
		std::optional<Engine::Graphics::UIClipRect> m_opClipRect = std::nullopt;
		float m_rate = 0.0f;				// 最後に Update で受け取った値の、範囲の中での割合(つまみを置く位置)

		// キー・パッドの左右(UpdateNavStep)
		int m_navHoldDir = 0;				// 倒している向き(-1 で左、+1 で右)
		float m_navRepeatTimer = 0.0f;		// 押しっぱなしで次に動くまでの残り(秒)
	};
}
