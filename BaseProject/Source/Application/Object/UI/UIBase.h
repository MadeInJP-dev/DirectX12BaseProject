#pragma once

#include "../../../Engine/GameObject/BaseObject/BaseObject.h"
#include "Application/Object/UI/Decoration/Decoration.h"
#include "Application/Object/UI/Decoration/DecorationDraw.h"
#include "Application/Object/UI/Core/UIAnchor.h"
#include "Application/Object/UI/Core/UIInteraction.h"
#include "Application/Object/UI/Core/UITransition.h"

namespace App::Object
{
	//======================================================================================
	// UIの土台
	//
	// UI 1つは次の3つを組み合わせたもの。それぞれの中身は別のファイルにある。
	//
	//   UIAnchor      … 画面のどこに、どの向き・どの大きさで置くか(位置・大きさ・回転・倍率・色・Z順・湾曲)
	//   Decoration    … そこから相対で出す絵(板ポリ・画像・文字)。配列で何枚でも重ねられる
	//                   コントローラー用の配列を別に持たせると、コントローラーを触っている間はそちらを出す
	//   UIInteraction … カーソルへの反応(当たり判定の設定・押下・音・項目移動の前と次)。押せる UI だけが持つ
	//   UITransition  … 出し入れと合図の演出(拡大して戻る・薄くなって消える など)。全部の飾りの上から掛かる
	//   UIMaterial    … 飾り全部に掛ける効果(ディゾルブなど)。アセットを GUID で指す。無くてもよい
	//
	// テクスチャを1枚だけ持たせる作りをやめたのは、枠と文字とアイコンのように
	// 複数の絵で1つのUIを組みたいときに、UIを人数ぶん並べるしかなくなるため。
	// 飾りを配列にしておけば、位置・回転・倍率はアンカーに追従したまま重ねられる。
	//
	// ・**当たり判定は UI 1つにつき1つだけ**(矩形1枚)。
	//   飾りを何枚重ねても、判定が飾りごとに増えることはない。飾りは判定の大きさを
	//   決める材料になるだけ(アンカーの大きさが0のとき・HitFollowAnim のとき)で、
	//   そのときも全部の飾りを囲む1枚の矩形にまとめる。
	//   押せる場所を増やしたい・形を変えたいときは、判定を足すのではなく UI を増やすこと。
	//   1つの UI に判定を複数持たせると、「どこを押したか」で振る舞いを変える処理が
	//   UI の中に入り込み、取り合い(手前の1つだけが反応する)の単位も崩れるため。
	//
	// ・押せるかどうかはクラスで決まる(コンストラクタの引数)。
	//   HUD のように押されることのない UI はカーソル反応を持たず、カーソルの取り合いにも出ない
	//   (画面いっぱいの HUD が下のボタンを塞ぐことがない)。
	//
	// ・親子(BaseObject の親)の下に置いた UI は、親の行列(位置・回転・倍率・Z順)に追従する。
	//   アンカーの値は親から見たもの。画面での値は CalcWorldAnchor で引く。
	//
	// ・表示も親から伝わる。親を隠せば、下にあるものは自分の Visible に関係なく描かれず、押せなくなる。
	//   親の UI が消える演出の最中は子も一緒に描かれ、親の濃さで薄れる。
	//   UI でない親(進行役など)も、出し入れを持っていればその表示に従う。
	//
	// 飾りにするか、別の UI(子)にするかの決まりは 環境/Docs/UIGuidelines.md
	//
	// インスペクター・ギズモ(エディター用)は UIBaseInspector.cpp にある
	//======================================================================================
	class UIBase : public Engine::GameObject::BaseObject
	{
	public:

		// 継承されるうえに Cast の行き先にもなるので、型の連鎖に載せる
		ENGINE_TYPE_CHAIN_BASE(UIBase, Engine::GameObject::BaseObject);

		// 押せる UI として作る(UIButton など)
		UIBase();

		// 解放処理
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		/// <summary>
		/// 更新前処理 : カーソルの上に居ると名乗る
		/// </summary>
		/// <remarks>
		/// 重なっているUIのうち手前の1つだけが反応するように、
		/// 判定そのものはここで済ませて、勝ち負けは Update で見る。
		/// 継承先で持つ場合は、先頭で UIBase::PreUpdate を呼ぶこと
		/// </remarks>
		void PreUpdate(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : 押下の進行と、飾りのアニメーション
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		/// <summary>
		/// ゲームの HUD(IsHUD の付いた UI)を、プレイヤーの設定によらずまとめて隠す
		/// </summary>
		/// <remarks>
		/// リザルトをステージの上へ重ねたときに、止まったステージの HUD を消すためのもの。
		/// 重ねている間、下のシーンは更新されないので、上のシーンから全体の印を立てる
		/// (SequenceBgm::SetGlobalDuck と同じ形)。立てた側が必ず下ろすこと
		/// </remarks>
		static void SetHUDSuppressed(bool a_isSuppressed) { s_isHUDSuppressed = a_isSuppressed; }
		static bool IsHUDSuppressed() { return s_isHUDSuppressed; }

		// 描画処理
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//-----------------------------------------------------------------------
		// エディター用(UIBaseInspector.cpp)
		//-----------------------------------------------------------------------

		// UIでの基本的なステータスをいじる : 継承先で作るのなら、初めに呼ぶ
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

		// シーンビュー上のスクリーンハンドルで位置を編集する
		bool DrawGizmo(const Engine::GameObject::ObjectGizmoContext& a_ctx, Engine::GameObject::ObjectContext& a_context) override;

		//-----------------------------------------------------------------------
		// 親の飾りへまとめる(エディター用、UIBaseMerge.cpp)
		//
		// 別の UI にしておく理由が無い UI を、親の UI の飾りへ移す。
		// 理由の一覧は 環境/Docs/UIGuidelines.md の「別の UI にする条件」
		//-----------------------------------------------------------------------

		/// <summary>
		/// 親の飾りへまとめられるか
		/// </summary>
		/// <param name="a_outReason">まとめられないときの理由(条件のどれに当たったか)</param>
		/// <remarks>他のオブジェクトから GUID で指されていないかを、全員の保存値から探して確かめる(重いのでエディターだけで使う)</remarks>
		bool CanMergeIntoParent(Engine::GameObject::ObjectContext& a_context, std::string& a_outReason) const;

		/// <summary>
		/// 親の飾りへまとめる
		/// </summary>
		/// <remarks>
		/// 飾りは画面での見た目が変わらないよう、親から見た値に直して親の末尾へ足す。
		/// 自分の下の UI は親へ付け替え(見た目の場所は保つ)、自分は次のフレームに消える
		/// </remarks>
		/// <returns>まとめられなかったら false(何も変えない)</returns>
		bool MergeIntoParent(Engine::GameObject::ObjectContext& a_context);

		//-----------------------------------------------------------------------
		// 表示の切り替え
		//-----------------------------------------------------------------------

		/// <summary>
		/// 表示するか(自分の指定)
		/// </summary>
		/// <remarks>
		/// 切ると描画されず、押せるUI(UIButton)なら入力も受け取らなくなる。
		/// 画面の中で出したり引っ込めたりするもの(ホームとステージセレクトの
		/// 出し分けなど)は、消さずにこれで切り替える。
		///
		/// 実際に出るかは、上にある親の表示も合わせて決まる(IsVisibleInHierarchy)。
		/// 切り替わったときは出し入れの演出(UITransition の show / hide)が流れる
		/// </remarks>
		bool IsVisible() const override { return m_isVisible; }
		void SetVisible(bool a_isVisible) override;

		/// <summary>
		/// 実際に出ているか : 自分の指定と、上にある親全員の表示
		/// </summary>
		/// <remarks>
		/// 親を根まで辿り、1つでも隠れていれば出ていない。
		/// 押せるか・進行を止めるかの判断はこちらで行う(消える演出の最中は false)
		/// </remarks>
		bool IsVisibleInHierarchy(const Engine::GameObject::ObjectContext& a_context) const;

		/// <summary>
		/// 描くか : 実際に出ているものに、演出の都合を足したもの
		/// </summary>
		/// <remarks>
		/// 消える演出(hide)の最中は Visible が切れていても描き、
		/// 合図の間だけ描く UI(PulseOnly)は合図が終わったら描かない。
		/// 上の親が隠れていれば、どちらでも描かない
		/// </remarks>
		bool IsDrawnInHierarchy(const Engine::GameObject::ObjectContext& a_context) const;

		/// <summary>
		/// エディターのシーンビューで、この点を押したらこの UI を選ぶか
		/// </summary>
		/// <param name="a_uiPos">UI のピクセル座標(描画解像度, 左上原点)</param>
		/// <remarks>
		/// 描かれているものだけ。押せる UI は押したときと同じ範囲(CalcHitRect)、
		/// 押せない UI(文字・飾りだけのもの)は見えている飾りの範囲で見る。
		/// 切り抜かれて見えていないところでは選ばない
		/// </remarks>
		bool IsPickedAt(const Engine::GameObject::ObjectContext& a_context, const Math::Vector2& a_uiPos) const;

		/// <summary>
		/// HUD か : プレイヤーが設定画面で HUD を消したとき、一緒に描かれなくなる
		/// </summary>
		/// <remarks>
		/// ゲーム中の照準・ゲージなどに付ける(保存される)。HUD 用のクラスは既定で立っている。
		/// メニューやリザルトのように、HUD を消しても残すべき UI には付けないこと
		/// </remarks>
		bool IsHUD() const { return m_isHUD; }
		void SetHUD(bool a_isHUD) { m_isHUD = a_isHUD; }

		//-----------------------------------------------------------------------
		// カーソルへの反応(押せない UI では常に「何もされていない」)
		//-----------------------------------------------------------------------

		// カーソルへの反応を持つか
		bool HasInteraction() const { return m_opInteraction.has_value(); }

		// カーソルが乗っているか
		bool IsHovered() const { return m_opInteraction && m_opInteraction->isHovered; }

		// 押されている最中か(押しっぱなし)
		bool IsPressed() const { return m_opInteraction && m_opInteraction->isPressed; }

		// このフレームに押し切られたか(押して離した瞬間だけ true)
		bool IsClicked() const { return m_opInteraction && m_opInteraction->isClicked; }

		// 今の状態 : 飾りの反応(Decoration::UIReaction)へ渡すもの
		Decoration::EUIState GetUIState() const;

		// 触れるかどうか。切ると乗っても押しても反応しない(Disabled 扱い)
		bool IsInteractable() const { return m_opInteraction && m_opInteraction->isInteractable; }
		void SetInteractable(bool a_isInteractable);

		//-----------------------------------------------------------------------
		// 項目移動(キー・パッドで UI を順に選ぶ。押せない UI は持たない)
		//
		// 前・次の UI を GUID で持って保存する。実際に選択を動かすのは UINavigationGroup
		//-----------------------------------------------------------------------

		// 項目移動で選ばれているか(IsHovered も立つ)
		bool IsNavFocused() const { return m_opInteraction && m_opInteraction->isNavFocused; }

		// 前・次の UI の GUID(空なら無し。押せない UI は常に空)
		const Core::GUID& GetNavPrevGUID() const;
		const Core::GUID& GetNavNextGUID() const;

		// 前・次の UI を指し直す(押せない UI では何もしない)
		void SetNavPrevGUID(const Core::GUID& a_guid);
		void SetNavNextGUID(const Core::GUID& a_guid);

		// 前・次の UI を引く : 指していない・シーンに無い・UI でないなら nullptr
		UIBase* FindNavPrev(const Engine::GameObject::ObjectContext& a_context) const;
		UIBase* FindNavNext(const Engine::GameObject::ObjectContext& a_context) const;

		//-----------------------------------------------------------------------
		// 演出(出し入れと合図)
		//-----------------------------------------------------------------------

		// 合図の演出を頭から流す(当たった・増えた、など)
		void PlayPulse() { m_transition.PlayPulse(); }

		// 合図の演出を流している最中か
		bool IsPulsePlaying() const { return m_transition.IsPulsePlaying(); }

		const UITransition& GetTransition() const { return m_transition; }
		UITransition& RefTransition() { return m_transition; }

		//-----------------------------------------------------------------------
		// マテリアル(飾り全部に掛ける効果)
		//-----------------------------------------------------------------------

		// 指しているUIマテリアル(空なら効果なし)
		const Core::GUID& GetMaterialGUID() const { return m_materialGUID; }

		// UIマテリアルを差し替えて、読み込みを頼む(空の GUID で外す)
		void SetMaterialGUID(Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid);

		//-----------------------------------------------------------------------
		// アンカー
		//
		// アンカーの位置・回転・倍率は**親から見た値**(ローカル)。
		// 画面での置き場所は、これに親の行列を掛けたもの(CalcWorldAnchor)。
		// 親が居ない・親が置き場所を持たない(進行役など)なら、ローカルがそのまま画面での値になる
		//-----------------------------------------------------------------------
		const UIAnchor& GetAnchor() const { return m_anchor; }
		UIAnchor& RefAnchor() { return m_anchor; }

		/// <summary>
		/// 画面での置き場所 : アンカーに実行中だけのずらしと親の行列を掛けたもの
		/// </summary>
		/// <remarks>
		/// 変わるのは位置・回転・倍率・Z順(layer は親の Z順との足し算)。大きさ(pixelSize)と色は自分の値のまま
		/// (大きさへ倍率を掛けるかは使う側の決まりに従う。親の倍率も scale に入っている)。
		/// 描画・当たり判定はこれを使う。他の UI の画面での位置を見たいときもこちら
		/// </remarks>
		UIAnchor CalcWorldAnchor() const;

		// 画面でのピボットの位置(px)
		Math::Vector2 GetWorldPixelPos() const { return CalcWorldAnchor().pixelPos; }

		/// <summary>
		/// 画面でのピボットの位置(px)を入れる
		/// </summary>
		/// <remarks>親の行列を戻して、親から見た値に直してからアンカーへ入れる(並べる側が画面の座標で決めるとき用)</remarks>
		void SetWorldPixelPos(const Math::Vector2& a_screenPos);

		/// <summary>
		/// 画面での左上と大きさ(px)で置く
		/// </summary>
		/// <remarks>
		/// 当たり判定(アンカーの大きさ)も合わせる。ピボットが左上でなくても、矩形の左上が
		/// a_topLeft に来るようにずらす。見た目(飾りの大きさ)は飾り側が持つので変わらない。
		/// 並べる側(設定画面のパネル・スクロールバー)が毎フレーム入れる用
		/// </remarks>
		void SetWorldPixelRect(const Math::Vector2& a_topLeft, const Math::Vector2& a_size);

		//-----------------------------------------------------------------------
		// 別の UI を GUID で指す部品用
		//
		// 設定画面の行やスクロールバー・NumericMutate のように、シーンに置いた別の UI を
		// GUID で指して動かす部品(UIBase ではないもの)が使う
		//-----------------------------------------------------------------------

		// GUID からシーンの UI を引く(見つからなければ nullptr)。
		// エディターで選ぶコンボは Picker::DrawCombo<UIBase>(Application/Object/ObjectPicker.h)
		static UIBase* FindUI(const Engine::GameObject::ObjectContext& a_context, const Core::GUID& a_guid);

		//-----------------------------------------------------------------------
		// 行列(BaseObject) : 子の UI はこの行列に追従する
		//-----------------------------------------------------------------------

		// アンカーの位置(実行中だけのずらし込み)・回転・倍率・Z順から組み立てる
		Math::Matrix CalcLocalMatrix() const override;

		// 親が変わっても画面での置き場所が変わらないよう、アンカーの位置・回転・倍率・Z順を直す
		void RebaseLocal(const Math::Matrix& a_oldParentWorld, const Math::Matrix& a_newParentWorld) override;

		//-----------------------------------------------------------------------
		// 切り抜き(保存しない)
		//-----------------------------------------------------------------------

		/// <summary>
		/// 切り抜く矩形を設定する(px)
		/// </summary>
		/// <remarks>
		/// 矩形の外へ出たぶんは描かれず、押せもしない。
		/// スクロールする一覧(設定画面のパネル)が、範囲の外へはみ出した項目を切るために使う。
		/// 持ち主が毎フレーム入れ直す前提で、保存はしない
		/// </remarks>
		void SetClipRect(const Engine::Graphics::UIClipRect& a_clipRect) { m_opClipRect = a_clipRect; }
		void ClearClipRect() { m_opClipRect = std::nullopt; }

		/// <summary>
		/// 実行中だけのずらし(px) : 描画と当たり判定の両方に掛かる
		/// </summary>
		/// <remarks>
		/// 横から滑り込むメニュー(SideMenu)のように、置き場所は変えずに一時的に動かすときに使う。
		/// アンカーの PixelPos を書き換えると、動いている途中でシーンを保存したときにずれたまま残るため、
		/// 別に持って保存しない。
		/// ローカル行列に入るので、子の UI も一緒にずれる(親1つへ入れれば足りる)。
		/// ※ 位置を自前で差し替えて描く派生(DrawOverride::isUsePos)には掛からない
		/// </remarks>
		void SetRuntimeOffset(const Math::Vector2& a_offset) { m_runtimeOffset = a_offset; }
		const Math::Vector2& GetRuntimeOffset() const { return m_runtimeOffset; }

		//-----------------------------------------------------------------------
		// 飾り
		//-----------------------------------------------------------------------

		/// <summary>
		/// 飾りを1つ足して、その参照を返す
		/// </summary>
		/// <remarks>返る参照は次に足すまでの間だけ有効(配列が伸びると動く)</remarks>
		Decoration::Decoration& AddDecoration(Decoration::EDecorationType a_type);

		// 名前で探す : 見つからなければ nullptr
		Decoration::Decoration* FindDecoration(const std::string& a_name);

		/// <summary>
		/// 番号で探す : 見つからなければ nullptr(0 は常に nullptr)
		/// </summary>
		/// <remarks>
		/// 他から飾りを指すときはこちらを使う。名前はエディターで書き換えられるので、
		/// 名前で持っていると、見出しを直しただけで参照が切れる
		/// </remarks>
		Decoration::Decoration* FindDecorationById(uint32_t a_id);
		const Decoration::Decoration* FindDecorationById(uint32_t a_id) const;

		/// <summary>
		/// 飾りを1つ選ぶコンボ(エディター用)
		/// </summary>
		/// <param name="a_inoutId">選んでいる飾りの番号(0 で未選択)</param>
		/// <param name="a_opFilterType">この種類の飾りだけを並べる(nullopt なら全部)</param>
		/// <returns>選び直したら true</returns>
		bool DrawDecorationPicker(
			const char* a_label,
			uint32_t& a_inoutId,
			std::optional<Decoration::EDecorationType> a_opFilterType = std::nullopt) const;

		// 中身の取得 : いま出している組(下の「コントローラー用の飾り」)
		std::vector<Decoration::Decoration>& RefDecorations() { return RefShownDecorationVec(); }
		const std::vector<Decoration::Decoration>& GetDecorations() const { return GetShownDecorationVec(); }

		//-----------------------------------------------------------------------
		// コントローラー用の飾り(任意)
		//
		// 飾りの組は2つまで持てる : 既定(キーボード・マウス)と、コントローラー用。
		// コントローラー用を持つ UI は、最後に触ったデバイス(InputDeviceManager)が
		// コントローラーの間だけそちらを出す(押すボタンの絵を Enter と A で出し分ける など)。
		// 持たない UI は、どちらのデバイスでも既定の組を出す。
		//
		// 上の飾りの操作(探す・Ref/GetDecorations・DrawDecorationAt・判定の大きさ)は、
		// いま出している組に掛かる。出す組を切り替えるのは PreUpdate だけなので、
		// 1フレームの中で探した添え字が別の組を指すことはない。
		// AddDecoration は既定の組へ足す(コードが作る既定の飾り用)。
		//
		// 飾りの番号は組をまたいで共有する : コントローラー用は既定の組の写しから作るので、
		// 同じ役の飾りは同じ番号を持つ(ゲージの中身・値の文字などを番号で指したまま出し分けられる)。
		// 一度だけ書き込む値(ウェーブの見出しなど)は、ForEachDecorationSet で両方の組へ入れること
		//-----------------------------------------------------------------------

		// コントローラー用の組を持つか
		bool HasGamepadDecorations() const { return m_opGamepadDecorationVec.has_value(); }

		// いまコントローラー用の組を出しているか(持たなければ常に false)
		bool IsShowingGamepadDecorations() const { return m_isShowGamepad && m_opGamepadDecorationVec.has_value(); }

		/// <summary>
		/// 持っている飾りの組を全部回す(既定と、あればコントローラー用)
		/// </summary>
		/// <param name="a_func">std::vector&lt;Decoration::Decoration&gt;&amp; を1つ受け取る関数</param>
		template<class Func>
		void ForEachDecorationSet(Func&& a_func)
		{
			a_func(m_decorationVec);
			if (m_opGamepadDecorationVec) a_func(*m_opGamepadDecorationVec);
		}

	protected:

		/// <summary>
		/// 押せるかどうかを決めて作る
		/// </summary>
		/// <param name="a_isInteractive">
		/// false にするとカーソルへの反応を持たない(HUD など)。
		/// 持たないものはカーソルの取り合いに出ず、状態は常に Normal になる
		/// </param>
		explicit UIBase(bool a_isInteractive);

		//-----------------------------------------------------------------------
		// 継承先から使う描画
		//-----------------------------------------------------------------------

		/// <summary>
		/// 飾りを全部描く
		/// </summary>
		/// <param name="a_override">
		/// その場かぎりの差し替え。
		/// 位置を敵ごとに変える・桁ごとにUVをずらす・状態で色を掛ける、といった
		/// 保存しない上書きはここへ渡す(飾り側の値を書き換えると次のフレームまで残るため)
		/// </param>
		/// <remarks>出ていない(パネルごと隠れている場合も含む)ときは何も描かない</remarks>
		void DrawDecorations(
			Engine::GameObject::ObjectContext& a_context,
			const Decoration::DrawOverride& a_override = {});

		/// <summary>
		/// 飾りを1つだけ描く
		/// </summary>
		/// <remarks>
		/// 飾りごとに違う差し替えを掛けたいとき用(ゲージの中身だけ横に縮める等)。
		/// 自分で順に回して呼べば、配列順の重なりはそのまま保たれる
		/// </remarks>
		void DrawDecorationAt(
			Engine::GameObject::ObjectContext& a_context,
			size_t a_index,
			const Decoration::DrawOverride& a_override = {});

		// 名前から飾りの配列の添え字を引く(見つからなければ -1)
		int FindDecorationIndex(const std::string& a_name) const;

		// 番号から飾りの配列の添え字を引く(見つからなければ -1)
		int FindDecorationIndexById(uint32_t a_id) const;

		// 保存されているGUIDから、飾りのテクスチャ・フォントを引き直す
		void RequestDecorationResources(Engine::GameObject::ObjectContext& a_context);

		// 指しているプリセットが変わった飾りへ写し直す(PreUpdate と描画の頭で呼ぶ)
		void SyncDecorationPresets(Engine::GameObject::ObjectContext& a_context);

		// 飾りの一覧をインスペクターへ出す(追加・削除・並べ替え)
		void DrawDecorationListInspector(Engine::GameObject::ObjectContext& a_context);

		//-----------------------------------------------------------------------
		// 当たり判定の矩形
		//-----------------------------------------------------------------------
		struct HitRect
		{
			Math::Vector2 pixelPos = {};			// ピボットのスクリーン座標(px)
			Math::Vector2 pixelSize = {};			// 大きさ(px)
			Math::Vector2 pivot = { 0.5f, 0.5f };	// 正規化ピボット
			float rotation = 0.0f;					// 回転(度)
			Math::Vector2 padding = {};				// 足す余白(px)
		};

		/// <summary>
		/// いま効いている当たり判定の矩形を組み立てる
		/// </summary>
		/// <returns>押せない UI、または大きさを持たない(どこにも当たらない)ときは false</returns>
		/// <remarks>
		/// 判定(IsPointInsideSelf)とエディターの枠の表示が同じものを使う。
		/// 判定は UI 1つにつき必ずこの1枚だけ。配列や複数の矩形へ広げないこと
		/// (押せる場所を増やすときは UI を増やす。クラス冒頭の説明を参照)
		/// </remarks>
		bool CalcHitRect(HitRect& a_outRect) const;

		// UIのピクセル座標が自分の判定矩形の内側にあるか
		bool IsPointInsideSelf(const Math::Vector2& a_uiPos) const;

		/// <summary>
		/// 飾りが占めている範囲(アンカーからの相対, px)を求める
		/// </summary>
		/// <param name="a_outCenter">範囲の中心(アンカーからのずれ)</param>
		/// <param name="a_outSize">範囲の大きさ</param>
		/// <param name="a_isIncludeAnim">アニメーション・反応で変わった今の大きさも含めるか</param>
		/// <returns>大きさを持つ飾りが1つも無ければ false</returns>
		/// <remarks>
		/// 含める場合も素の矩形は必ず範囲へ入れる(素の矩形と今の矩形の合併を返す)。
		/// 今の矩形だけにすると、乗ると縮む反応を付けたときに
		/// 「乗る→縮んで外れる→戻って乗る」を繰り返してちらつくため
		/// </remarks>
		bool CalcDecorationBounds(
			Math::Vector2& a_outCenter,
			Math::Vector2& a_outSize,
			bool a_isIncludeAnim = false) const;

	protected:

		// 置き場所(保存される)
		UIAnchor m_anchor = {};

		// カーソルへの反応(保存される)。押せない UI は持たない
		std::optional<UIInteraction> m_opInteraction = std::nullopt;

		// 表示するか(保存される)。切ると描画も入力も止まる
		bool m_isVisible = true;

		// HUD か(保存される)。プレイヤーが HUD を消している間は描かない
		bool m_isHUD = false;

		// 飾り(保存される) : 配列の順に描くので、後ろにあるものほど手前に出る
		// キーボード・マウスのとき(コントローラー用を持たなければ常に)出す組
		std::vector<Decoration::Decoration> m_decorationVec = {};

		// コントローラー用の飾り(保存される)。持つ UI だけ、コントローラーを触っている間こちらを出す
		std::optional<std::vector<Decoration::Decoration>> m_opGamepadDecorationVec = std::nullopt;

		// 出し入れと合図の演出(保存される)。全部の飾りの上から掛かる
		UITransition m_transition = {};

		// 飾り全部に掛ける効果(保存されるのは GUID だけ)。空なら効果なし
		Core::GUID m_materialGUID = {};
		Engine::ResourceRef<Engine::Resource::UIMaterial> m_materialRef = {};

		// 読み込みで演出の値が読めたか(保存しない)。
		// 演出を持つ前のデータなら false。継承先が自分の古い値から演出へ移すかどうかの判断に使う
		bool m_isTransitionLoaded = false;

	private:

		// 旧形式(テクスチャ1枚)から飾りへ移し替える(読み込みの最後から)
		void MigrateLegacyTexture(bool a_hasDecorationArray);

		/// <summary>
		/// 番号を持たない・重なっている飾りへ番号を振る
		/// </summary>
		/// <remarks>
		/// 番号を持つ前に保存された飾りは 0 で読まれるので、読み込みの後に通す。
		/// 先に持っている番号は変えない(他から指されている番号が動かないように)
		/// </remarks>
		void AssignDecorationIds();

		// 1つの組の中で、番号を持たない・重なっている飾りへ番号を振る
		void AssignDecorationIdsIn(std::vector<Decoration::Decoration>& a_decorationVec);

		// まだ使われていない番号(両方の組を通して)
		uint32_t MakeNewDecorationId() const;

		// 飾りを1つ、指した組へ足す(番号・名前を付けて)
		Decoration::Decoration& AddDecorationTo(std::vector<Decoration::Decoration>& a_decorationVec, Decoration::EDecorationType a_type);

		// 飾りの配列1つぶんの保存(要素ごとに区切る)。配列が無ければ(古いデータ) false
		static bool ArchiveDecorationVec(Engine::Persistence::Archive& a_ar, const std::string& a_name, std::vector<Decoration::Decoration>& a_decorationVec);

		//-----------------------------------------------------------------------
		// コントローラー用の飾りの出し分け
		//-----------------------------------------------------------------------

		// いま出している組
		std::vector<Decoration::Decoration>& RefShownDecorationVec();
		const std::vector<Decoration::Decoration>& GetShownDecorationVec() const;

		// 最後に触ったデバイスから出す組を決める(PreUpdate から。エディターの確かめ用の指定があればそちら)
		void UpdateShownDecorationSet(const Engine::GameObject::ObjectContext& a_context);

		// コントローラー用の組を既定の組の写しから作る / 外す(エディター用)
		void CreateGamepadDecorations();
		void RemoveGamepadDecorations();

		// インスペクターで編集している組
		std::vector<Decoration::Decoration>& RefEditDecorationVec();

		// インスペクター : どちらの組を編集するか・どちらを出して確かめるか
		void DrawDecorationSetInspector(Engine::GameObject::ObjectContext& a_context);

		// 飾りへ渡す親の設定 : アンカーの設定に、実行中だけの切り抜きを足したもの
		Decoration::ParentOption MakeParentOption() const;

		// 飾りへ渡す親の位置・回転など : アンカーの値に、実行中だけのずらしを足したもの
		Decoration::ParentTransform MakeParentTransform() const;

		// HUD として隠れているか : HUD で、プレイヤーが HUD を消しているとき true
		bool IsHiddenAsHUD(const Engine::GameObject::ObjectContext& a_context) const;

		// ギズモ : 当たり判定の範囲を枠で囲って出す(押せる UI だけ)
		void DrawHitRectGizmo(const Engine::GameObject::ObjectGizmoContext& a_ctx, float a_screenW, float a_screenH) const;

		// ギズモ : 開いている飾りの位置(OffsetPos)を動かすハンドル
		void DrawDecorationGizmo(const Engine::GameObject::ObjectGizmoContext& a_ctx, float a_screenW, float a_screenH);

		// インスペクター : カーソルへの反応(判定の範囲の表示を含む)
		void DrawInteractionInspector(Engine::GameObject::ObjectContext& a_context);

		// インスペクター : 項目移動の前・次(指した相手の向かい側もつなぐ)
		void DrawNavigationInspector(Engine::GameObject::ObjectContext& a_context);

		// インスペクター : 出し入れと合図の演出(確認用に流すボタンを含む)
		void DrawTransitionInspector();

		// 自分の指定だけで描くか(消える演出の最中は描く・合図だけの UI は合図の間だけ)。親は見ない
		bool IsSelfDrawn() const;

		// 上にある親が全員出ているか(自分の Visible は見ない)
		bool IsParentsVisible() const;

		// 上にある親が全員描かれているか(親の UI は IsSelfDrawn と HUD の隠しで見る)
		bool IsParentsDrawn(const Engine::GameObject::ObjectContext& a_context) const;

		// 上にある親の UI の演出の濃さを掛け合わせたもの
		float CalcParentsTransitionAlpha() const;

		// 描く前の差し替えに、演出の値を掛ける
		Decoration::DrawOverride ApplyTransition(const Decoration::DrawOverride& a_override) const;

		// 保存されている GUID からUIマテリアルを引き直す(実体の到着は待たない)
		void RequestMaterial(Engine::GameObject::ObjectContext& a_context);

		// 読み終わっているUIマテリアル(まだ・未指定なら nullptr)
		const Engine::Resource::UIMaterial* FindReadyMaterial(Engine::Resource::ResourceManager* a_pResourceManager) const;

		// インスペクター : UIマテリアル
		void DrawMaterialInspector(Engine::GameObject::ObjectContext& a_context);

	private:

		//-----------------------------------------------------------------------
		// 旧形式(テクスチャ1枚)からの引き継ぎ用
		//
		// 既存のシーンは UIBase が直接テクスチャを持っていた頃の値で保存されている。
		// 並びを変えずにここへ読み込んでおき、飾りの配列を持たないシーンだけ
		// 画像の飾り1つへ移し替える
		//-----------------------------------------------------------------------
		Core::GUID m_legacyTexGUID = {};
		Math::Vector2 m_legacyUvOffset = {};

		// エディターで開いている飾りの番号(-1 で未選択)。保存しない
		int m_editDecorationIndex = -1;

		// HUD をまとめて隠しているか(SetHUDSuppressed)。保存しない
		inline static bool s_isHUDSuppressed = false;

		// コントローラー用の組を出しているか(最後に触ったデバイスから PreUpdate で決める)。保存しない
		bool m_isShowGamepad = false;

		//-----------------------------------------------------------------------
		// エディターでの出し分けの確かめ(保存しない)
		//-----------------------------------------------------------------------
		enum class EDecorationPreview : uint32_t
		{
			Auto,			// 最後に触ったデバイスに従う(実行時と同じ)
			KeyboardMouse,	// 既定の組を出す
			Gamepad,		// コントローラー用の組を出す
		};
		EDecorationPreview m_decorationPreview = EDecorationPreview::Auto;

		// インスペクターでコントローラー用の組を編集しているか
		bool m_isEditGamepadSet = false;

		// 切り抜き(px)。持ち主(スクロールする一覧など)が毎フレーム入れる。保存しない
		std::optional<Engine::Graphics::UIClipRect> m_opClipRect = std::nullopt;

		// 実行中だけのずらし(px)。持ち主(滑り込むメニューなど)が入れる。保存しない
		Math::Vector2 m_runtimeOffset = {};
	};
}
