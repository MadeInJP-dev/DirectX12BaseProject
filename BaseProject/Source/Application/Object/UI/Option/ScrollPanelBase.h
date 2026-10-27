#pragma once

// 数値の行と右側のスクロールバーは部品を値で持つ
#include "Application/Object/UI/Widget/VariableMutate/NumericMutate.h"
#include "Application/Object/UI/Widget/VariableMutate/ScrollBar/ScrollBar.h"

namespace Engine::GameObject
{
	struct ObjectContext;
}

namespace App::InstanceResource
{
	struct UserDataResource;
}

namespace App::Object
{
	class UIBase;

	//===============================================================================================
	// パネルへ渡す並べ方
	//
	// 持つのは OptionHUD。どのパネルも同じ場所・同じ大きさで出すので、パネルごとには持たない
	//===============================================================================================
	struct PanelLayout
	{
		Math::Vector2 pos = {};			// 描画範囲の左上(px)
		Math::Vector2 size = {};		// 描画範囲の大きさ(px)。右端の BarWidth ぶんはスクロールバーが使う
		Math::Vector2 itemSize = {};	// 項目1つの大きさ(px)。行の送りと、ピボットの補正に使う
		float itemSpacing = 0.0f;		// 行と行の間(px)
	};

	//===============================================================================================
	// 行の中に置く UI 1つ
	//
	// 項目の見た目はシーンに置いた別の UI(UIButton・UIGauge など)で作り、ここでは GUID で指す。
	// 置き場所はパネルが毎フレーム決める(行の左上 + offsetX)ので、UI 側の PixelPos は上書きされる
	//===============================================================================================
	struct OptionItemUI
	{
		Core::GUID guid = {};
		float offsetX = 0.0f;		// 行の左端からのずれ(px)
	};

	//===============================================================================================
	// 選択肢を切り替える行 : 今の選択の名前と、前へ・次へのボタン
	//
	// ウィンドウモードやオン/オフのように「決まった選択肢から1つ選ぶ」項目に使う。
	// 名前は display の UI の文字の飾り(valueTextId)へ毎フレーム書き込む
	//===============================================================================================
	struct OptionSelectorUI
	{
		OptionItemUI display = {};	// 見出しと今の選択の名前を出す UI
		uint32_t valueTextId = 0;	// 今の選択の名前を書き込む文字の飾り(display の飾りの番号)。0 で書かない
		OptionItemUI prev = {};		// 前の選択肢へ
		OptionItemUI next = {};		// 次の選択肢へ
	};

	//===============================================================================================
	// プルダウンで1つ選ぶ行 : 見出し・選択欄と、開いたときに選択欄の下へ並ぶ候補
	//
	// 選択欄(右端に ▼ の飾りを置く)を押すと候補が開き、候補を押すとそれに決まって閉じる。
	// 候補のボタンは選択肢の数だけ置く(足りなければ、足りないぶんの選択肢は出せない)。
	// 候補は範囲で切り抜かない(パネルの下端で切れて選べなくならないように)。
	// 他の項目より手前に出るよう、候補の layer は大きくしておくこと
	//===============================================================================================
	struct OptionDropdownUI
	{
		OptionItemUI label = {};		// 見出し(押せない UI)
		OptionItemUI field = {};		// 選択欄(UIButton)。押すと候補が開閉する
		uint32_t valueTextId = 0;		// 今の選択の名前を書き込む文字の飾り(選択欄の飾りの番号)。0 で書かない

		std::vector<Core::GUID> candidateGUIDVec = {};		// 候補のボタン(UIButton)。上から選択肢の並びで使う
		uint32_t candidateTextId = 0;						// 候補の名前を書き込む文字の飾り(どの候補も同じ番号)
		float candidateOffsetX = 0.0f;						// 候補の左端 : 行の左端からのずれ(px)
		Math::Vector2 candidateSize = { 440.0f, 56.0f };	// 候補1つの大きさ(px)。選択欄の下へ縦に並ぶ
	};

	//===============================================================================================
	// 設定画面のパネルの土台 : スクロールと右側のスクロールバーを持つ
	//
	// パネル(InputOptionPanel など)はこれを継承し、自分が操作する項目だけを持つ。
	// 描画範囲は親(OptionHUD)から毎フレーム貰い、ここは「範囲の中で項目をどれだけずらすか」だけを決める。
	//
	//   OptionHUD の PreUpdate … Layout : 派生の LayoutItems で項目を上から積み、バーを置く
	//   OptionHUD の Update    … Update : ホイール・バーの操作、派生の UpdateItems で項目の操作
	//
	// ・項目もバーもシーンに置いた別の UI。ここは位置と切り抜きを毎フレーム入れるだけで、見た目は持たない。
	//   範囲の外へはみ出したぶんは切り抜き(UIBase::SetClipRect)で描かれず、押せもしない。
	// ・出し入れは根の UI(RootPanel)1つで行う。項目とバーはヒエラルキーでその下に置くこと。
	// ・右側のスクロールバーは ScrollBar(縦)。つまみは固定の長さで、内容の長さに合わせて伸び縮みはしない
	//   (飾りの大きさは飾り側が持っていて、アンカーの大きさを変えても絵は変わらないため)。
	// ・数値の行は NumericMutate(ボタン / スクロールバーをエディターで選ぶ)。置くのは PlaceNumeric
	//===============================================================================================
	class ScrollPanelBase
	{
	public:

		virtual ~ScrollPanelBase() = default;

		// パネルの名前 : 保存の区切り・インスペクターの見出しに使う。保存に使うので変えないこと
		virtual const char* GetPanelName() const = 0;

		// 開かれたとき : 表示用の写しを UserData から読み直す
		virtual void OnOpen(Engine::GameObject::ObjectContext& /*a_context*/) {}

		/// <summary>
		/// 入力を自分で受け取っている最中か(キー割り当ての待ち受けなど)
		/// </summary>
		/// <remarks>立っている間、OptionHUD は閉じるキーを見ない(押したキーで画面が閉じないように)</remarks>
		virtual bool IsCapturingInput() const { return false; }

		/// <summary>
		/// 並べる : 項目を上から積み、スクロールバーを置く
		/// </summary>
		/// <remarks>
		/// 項目の当たり判定より先に位置を決めたいので、OptionHUD の PreUpdate から呼ばれる。
		/// 出ているパネルだけが呼ばれる
		/// </remarks>
		void Layout(Engine::GameObject::ObjectContext& a_context, const PanelLayout& a_layout);

		// 更新 : ホイール・バーの操作と、項目の操作(出ているパネルだけ)
		void Update(Engine::GameObject::ObjectContext& a_context);

		// 出し入れ : 根の UI を切り替える(下に置いた項目とバーが一緒に出入りする)
		void SetVisible(Engine::GameObject::ObjectContext& a_context, bool a_isVisible);

		// アーカイブ : 基底のぶんと派生のぶん(ArchiveItems)を区切りを分けて書く
		void Archive(Engine::Persistence::Archive& a_ar);

		// インスペクター : バーの設定と、派生の項目(DrawItemsInspector)
		void DrawInspector(Engine::GameObject::ObjectContext& a_context);

		//-----------------------------------------------------------------------
		// スクロール
		//-----------------------------------------------------------------------

		// 現在位置(px)。0 で一番上
		float GetScrollOffset() const { return m_scrollOffset; }

		// 位置を入れる : 0〜最大の間に詰める
		void SetScrollOffset(float a_offset);
		void AddScrollOffset(float a_add) { SetScrollOffset(m_scrollOffset + a_add); }

		// 動かせる最大(px) : 内容の全長から見えている高さを引いたもの
		float GetMaxScrollOffset() const;

		/// <summary>
		/// 内容のこの範囲が見えるところまでスクロールする
		/// </summary>
		/// <param name="a_top">範囲の上端(内容の座標, px)</param>
		/// <param name="a_bottom">範囲の下端(内容の座標, px)</param>
		/// <remarks>選んだ項目への追従用(BottonMove で項目を順に移るときなど)。見えていれば動かさない</remarks>
		void ScrollIntoView(float a_top, float a_bottom);

		//-----------------------------------------------------------------------
		// 引く・エディター用(パネルの項目にも、OptionHUD のボタンにも使う)
		//
		// 中身は UIBase::FindUI / UIBase::SetWorldPixelRect / Picker::DrawCombo<UIBase>。
		// 設定画面の中からは呼び慣れたこちらの名前で使う
		//-----------------------------------------------------------------------

		// GUID からシーンの UI を引く(見つからなければ nullptr)
		static UIBase* FindUI(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid);

		/// <summary>
		/// 左上と大きさで UI を置く
		/// </summary>
		/// <remarks>
		/// 当たり判定(アンカーの大きさ)も合わせる。ピボットが左上でなくても、矩形の左上が
		/// a_topLeft に来るようにずらす。見た目(飾りの大きさ)は飾り側が持つので変わらない
		/// </remarks>
		static void PlaceUIRect(UIBase& a_ui, const Math::Vector2& a_topLeft, const Math::Vector2& a_size);

		/// <summary>
		/// シーンに置いた UI を1つ選ぶコンボ(エディター用)
		/// </summary>
		/// <returns>選び直したら true</returns>
		static bool DrawUIPicker(Engine::GameObject::ObjectContext& a_context, const char* a_label, Core::GUID& a_inoutGUID);

		//-----------------------------------------------------------------------
		// 選択肢を切り替える行の操作 : 派生と、部品(OptionEnumSelector)から使う
		//-----------------------------------------------------------------------

		// このフレームに押されたボタン : 前へなら -1、次へなら +1、どちらも押されていなければ 0
		static int UpdateSelector(Engine::GameObject::ObjectContext& a_context, const OptionSelectorUI& a_selector);

		// 今の選択の名前を書き込む
		static void SetSelectorText(Engine::GameObject::ObjectContext& a_context, const OptionSelectorUI& a_selector, const std::string& a_text);

		// 選択肢の添え字を a_step だけ進める。端を越えたら反対の端へ回る
		static size_t StepIndex(size_t a_index, int a_step, size_t a_count);

		// UI の文字の飾り(番号で指す)へ文字を書き込む。変わらなければ書かない
		static void SetUIText(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid, uint32_t a_textId, const std::string& a_text);

	protected:

		//-----------------------------------------------------------------------
		// 派生が書くもの
		//-----------------------------------------------------------------------

		// 項目を並べる : PlaceItem / AdvanceRow / AdvanceSpace を上から順に呼ぶ
		virtual void LayoutItems(Engine::GameObject::ObjectContext& a_context) = 0;

		// 項目の操作 : 押された・値が変わったら、その場で UserData へ書く
		virtual void UpdateItems(Engine::GameObject::ObjectContext& a_context) = 0;

		// 項目の保存(基底とは別の区切りの中で呼ばれる)
		virtual void ArchiveItems(Engine::Persistence::Archive& a_ar) = 0;

		// 項目のインスペクター
		virtual void DrawItemsInspector(Engine::GameObject::ObjectContext& a_context) = 0;

		//-----------------------------------------------------------------------
		// 派生から使うもの : 並べ方
		//-----------------------------------------------------------------------

		// 今の行へ UI を置く(スクロールと切り抜きを掛ける)
		void PlaceItem(Engine::GameObject::ObjectContext& a_context, const OptionItemUI& a_item);

		// 次の行へ送る : 項目1つぶん + 行間
		void AdvanceRow();

		// 決まった量だけ空ける(見出しの前の余白など)
		void AdvanceSpace(float a_pixel) { m_layoutCursorY += a_pixel; }

		// 見出しの行 : 置いて次の行へ送る。GUID が未設定なら行も取らない
		void PlaceHeaderRow(Engine::GameObject::ObjectContext& a_context, const OptionItemUI& a_header);

		// 今の行の上端(内容の座標, px) : ScrollIntoView へ渡す用
		float GetRowTop() const { return m_layoutCursorY; }

		// 今フレームの並べ方
		const PanelLayout& GetLayout() const { return m_layout; }

		//-----------------------------------------------------------------------
		// 派生から使うもの : 引く・保存する
		//-----------------------------------------------------------------------

		// ユーザーデータの入口を引く(ワールドに無ければ nullptr)
		static InstanceResource::UserDataResource* FindUserData(Engine::GameObject::ObjectContext& a_context);

		// 行の中の UI 1つを保存する(a_name を頭に付けたキーで書く)
		static void ArchiveItemUI(Engine::Persistence::Archive& a_ar, const std::string& a_name, OptionItemUI& a_item);

		// 行の中の UI 1つのインスペクター
		static bool DrawItemUIInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label, OptionItemUI& a_item);

		//-----------------------------------------------------------------------
		// 派生から使うもの : 数値の行(操作・保存・インスペクターは NumericMutate を直接呼ぶ)
		//-----------------------------------------------------------------------

		// 今の行へ数値の行を置く(スクロールと切り抜きを掛ける。行は送らない)
		void PlaceNumeric(Engine::GameObject::ObjectContext& a_context, NumericMutate& a_numeric);

		//-----------------------------------------------------------------------
		// 派生から使うもの : 選択肢を切り替える行
		//-----------------------------------------------------------------------

		// 今の行へ名前の UI と前へ・次へのボタンを置く(行は送らない)
		void PlaceSelector(Engine::GameObject::ObjectContext& a_context, const OptionSelectorUI& a_selector);

		// 保存(a_name を頭に付けたキーで書く : <name>DisplayGUID など)
		static void ArchiveSelector(Engine::Persistence::Archive& a_ar, const std::string& a_name, OptionSelectorUI& a_selector);

		// インスペクター
		static void DrawSelectorInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label, OptionSelectorUI& a_selector);

		//-----------------------------------------------------------------------
		// 派生から使うもの : プルダウンで1つ選ぶ行(操作は OptionDropdown)
		//-----------------------------------------------------------------------

		// 今の行へ見出しと選択欄を置き、候補を選択欄の下へ並べる(行は送らない)
		void PlaceDropdown(Engine::GameObject::ObjectContext& a_context, const OptionDropdownUI& a_dropdown);

		// 保存(a_name を頭に付けたキーで書く : <name>FieldGUID など)
		static void ArchiveDropdown(Engine::Persistence::Archive& a_ar, const std::string& a_name, OptionDropdownUI& a_dropdown);

		// インスペクター
		static void DrawDropdownInspector(Engine::GameObject::ObjectContext& a_context, const char* a_label, OptionDropdownUI& a_dropdown);

	private:

		// ホイール : 範囲の上で回されたぶんだけ動かす
		void UpdateWheel(Engine::GameObject::ObjectContext& a_context);

		// スクロールバー : 動かされた割合をスクロール位置へ
		void UpdateScrollBar(Engine::GameObject::ObjectContext& a_context);

		// スクロールバーを描画範囲の右端に縦いっぱいで置く
		void PlaceScrollBar(Engine::GameObject::ObjectContext& a_context);

		// 今のスクロール位置の割合(0〜1)。動かせないときは 0
		float GetScrollRate() const;

		// 今の行を「選ばれている行」として覚える(Layout の最後にそこまでスクロールする)
		void MarkFocusRow()
		{
			m_hasFocusRow = true;
			m_focusRowTop = m_layoutCursorY;
		}

		// 項目を切り抜く矩形 : 描画範囲からバーのぶんを除いたもの
		Engine::Graphics::UIClipRect MakeContentClipRect() const;

		// 点(UIのピクセル座標)が描画範囲の中か
		bool IsInsideLayout(const Math::Vector2& a_uiPos) const;

	private:

		//------------------------------------------------------------------------------
		// 設定(保存される)
		//------------------------------------------------------------------------------
		Core::GUID m_rootPanelGUID = {};	// 出し入れの単位(UIElement など)。項目とバーはこの下に置く

		ScrollBar m_scrollBar{ EScrollBarAxis::Vertical };	// 右側のスクロールバー(溝・つまみ・つまみの長さ)
		float m_barWidth = 16.0f;			// バーの幅(px)。描画範囲の右端からこの幅をバーが使う
		float m_wheelStep = 64.0f;			// ホイール1ノッチで動く量(px)

		//------------------------------------------------------------------------------
		// 状態(保存しない)
		//------------------------------------------------------------------------------
		PanelLayout m_layout = {};			// 今フレームの並べ方(OptionHUD から貰う)

		float m_scrollOffset = 0.0f;		// 現在位置(px)
		float m_contentLength = 0.0f;		// 項目の全長(px) : これを最大としてスクロールバーを決める
		float m_layoutCursorY = 0.0f;		// 並べている途中の行の上端(内容の座標, px)

		// 項目移動(キー・パッド)で選ばれている行 : 並べ終えたら見えるところまでスクロールする
		bool m_hasFocusRow = false;
		float m_focusRowTop = 0.0f;			// 上端(内容の座標, px)
	};
}
