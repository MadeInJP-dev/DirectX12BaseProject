#pragma once

#include "Application/Object/UI/Decoration/DecorationReaction.h"

#include "Application/Game/Core/InputSettings.h"

namespace Engine::GameObject { struct ObjectContext; }

namespace App::Object
{
	//======================================================================================
	// UIのカーソルへの反応 : 当たり判定の設定・押下の進行・音
	//
	// UIBase から切り出したもの。押せる UI(UIButton・カーソルを遮る UIElement など)だけが持ち、
	// HUD のように押されることのない UI は持たない(UIBase のコンストラクタで決まる)。
	//
	// 押されて何をするかは持たない。背景でもパネルでも「乗ったら枠を出す」「押したら縮む」が
	// 付けられるように、判定と状態だけをここで作る。見た目の変化は飾り側(Decoration の Reaction)。
	//
	// 重なったときは **手前の1つだけ** が反応する(layer が大きいほど手前。
	// 同じ値なら後に描かれるほう)。決めるのは UICursorResource で、
	//   PreUpdate … ClaimCursor で名乗る
	//   Update    … Advance で取れたかを見て、押下を進める
	// の2段に分けてある。下になったUIは矩形の中にカーソルが居ても乗っていない扱いになるので、
	// 上のパネルごしに裏のボタンが押せることはない。
	// カーソルを通したい(奥のUIを押させたい)ものは isInteractable を切ること。
	//
	// 当たり判定の矩形そのものは UIBase が組み立てる(飾りの範囲を使うことがあるため)。
	// **判定は UI 1つにつき1枚だけ**で、ここが持つ状態(乗っている・押している)もその1枚に対するもの。
	// 押せる場所を増やしたいときは、判定を足すのではなく UI を増やすこと
	//
	// キー・パッドでの項目移動(UINavigationGroup)のために、前・次の UI を GUID で持つ。
	// 項目移動で選ばれている間は「乗っている」と同じ扱いになり、決定(Decide)で押される。
	// 項目移動の間はカーソルが誰にも乗らない(UINavigationResource が止める)
	//======================================================================================
	struct UIInteraction
	{
		//-----------------------------------------------------------------------
		// 設定(保存される)
		//-----------------------------------------------------------------------
		// 押下に使う入力アクション。どのキーで押されるかは持たない
		// (割り当ては InputActionManager がユーザーデータから作る)。
		// 保存はアクション名なので、並べ替えても指す先は変わらない
		Game::EGameAction clickAction = Game::EGameAction::Select;

		// 当たり判定の余白(px)。見た目より広く/狭く取りたいとき用
		Math::Vector2 hitPadding = { 0.0f, 0.0f };

		// 判定を飾りの今の大きさへ追従させるか
		// 立てている間はアンカーの pixelSize より飾りの範囲が優先される
		bool isHitFollowAnim = false;

		// 触れるかどうか。切ると Disabled 扱いになる
		bool isInteractable = true;

		// 乗った瞬間 / 押した瞬間に鳴らす音
		Core::GUID hoverSoundGUID = {};
		Core::GUID pressSoundGUID = {};
		float soundVolume = 1.0f;

		// 同じ音を鳴らし直す最短間隔(秒)。0 で間引かない
		float soundMinInterval = 0.08f;

		// 項目移動での前・次の UI。キー・パッドで選ぶときに、ここをたどって移る。
		// 空なら無し(前が空のときは、グループが最初からたどって「次が自分」のものを前とみなす)。
		// 実際に選択を動かすのは UINavigationGroup
		Core::GUID navPrevGUID = {};
		Core::GUID navNextGUID = {};

		//-----------------------------------------------------------------------
		// 状態(保存しない)
		//-----------------------------------------------------------------------
		bool isHovered = false;		// カーソルが乗っている(かつ自分が手前)か、項目移動で選ばれている
		bool isPressed = false;		// 押されている最中
		bool isClicked = false;		// このフレームに押し切られた

		// 項目移動で選ばれている(グループが選んでいて、キー・パッドで操作している間)
		bool isNavFocused = false;

		// 選ばれている間に決定を押し始めたか。
		// 押している間に選択が移ったら、離しても押し切りにしない
		bool isNavPressStarted = false;

		// 矩形の中にカーソルが居るか。ClaimCursor で見た結果。
		// これが true でも、手前に別のUIが重なっていれば isHovered は false になる
		bool isCursorInside = false;

		// 矩形の内側で押し始めたか。
		// これを見ておかないと、外で押してUIの上で離しただけで反応してしまう
		bool isPressStartedInside = false;

		// 借りている音のインスタンス。初めて鳴らすときに取り、ReleaseSounds で返す
		Engine::Handle<Engine::Resource::SoundInstance> hoverSoundHandle = {};
		Engine::Handle<Engine::Resource::SoundInstance> pressSoundHandle = {};

		// 次に鳴らせるまでの残り時間(秒)
		float hoverSoundCoolTime = 0.0f;
		float pressSoundCoolTime = 0.0f;

		//=======================================================================

		// 今の状態 : 飾りの反応(Decoration::UIReaction)へ渡すもの
		Decoration::EUIState GetState() const;

		/// <summary>カーソルを受け取れる状態か</summary>
		/// <param name="a_isVisible">持ち主が出ているか(パネルの表示も含めたもの)</param>
		/// <remarks>
		/// 出ていて・触れて・プレイモード中(エディターで文字を打っていない)のときだけ true。
		/// 名乗り(ClaimCursor)と進行(Advance)で同じ条件を見ること。
		/// ここがずれると、反応しないUIが名乗って下のUIを塞ぐ
		/// </remarks>
		bool IsReceivable(const Engine::GameObject::ObjectContext& a_context, bool a_isVisible) const;

		/// <summary>
		/// カーソルの上に居ると名乗る(持ち主の PreUpdate から)
		/// </summary>
		/// <param name="a_pOwner">持ち主。アドレスの比較にしか使わない</param>
		/// <param name="a_layer">重なり順(大きいほど手前)</param>
		/// <param name="a_isReceivable">IsReceivable の結果</param>
		/// <param name="a_isInside">持ち主の判定矩形の内側にカーソルが居るか</param>
		void ClaimCursor(
			Engine::GameObject::ObjectContext& a_context,
			const void* a_pOwner,
			float a_layer,
			bool a_isReceivable,
			bool a_isInside);

		/// <summary>
		/// 取り合いの結果を見て、乗った・押したを進める(持ち主の Update から)
		/// </summary>
		/// <remarks>
		/// カーソルでの押下(clickAction)と、項目移動で選ばれているときの決定(Decide)を両方見る
		/// </remarks>
		void Advance(Engine::GameObject::ObjectContext& a_context, const void* a_pOwner, bool a_isReceivable);

		// このフレームのカーソルを自分が取ったか(項目移動の間は誰も取れない)
		bool IsCursorOwner(const Engine::GameObject::ObjectContext& a_context, const void* a_pOwner) const;

		// このフレームに項目移動で選ばれているか
		bool IsNavFocusedOwner(const Engine::GameObject::ObjectContext& a_context, const void* a_pOwner) const;

		// 借りている音のインスタンスを返す
		void ReleaseSounds(Engine::GameObject::ObjectContext& a_context);

		/// <summary>
		/// 設定の保存(HitFollowAnim を除く)
		/// </summary>
		/// <remarks>
		/// 並びは UIBase が抱えていた頃のまま。HitFollowAnim は後から足したもので、
		/// 飾りの配列より後ろに置かれているため UIBase::Archive が書く
		/// </remarks>
		void ArchiveSettings(Engine::Persistence::Archive& a_ar);

		// インスペクター : 判定の設定(エディター用)
		void DrawSettingsInspector();

		// インスペクター : 音と今の状態(エディター用)
		void DrawSoundInspector(Engine::GameObject::ObjectContext& a_context);

		/// <summary>
		/// カーソル位置をUIのピクセル座標(描画解像度基準)で取得する
		/// </summary>
		/// <returns>取得できたら true(最小化中などは false)</returns>
		static bool CalcCursorUIPos(Engine::GameObject::ObjectContext& a_context, Math::Vector2& a_outPos);

	private:

		/// <summary>
		/// 音を鳴らす
		/// </summary>
		/// <remarks>
		/// インスタンスは初めて鳴らすときに借りる。
		/// UIは画面ぶん並ぶので、鳴らさないものにまで先に確保させると席が尽きる。
		///
		/// 間引きの残り時間が残っているうちは鳴らさない。
		/// インスタンスは1つで Play は頭出しの鳴らし直しになるため、
		/// 判定の縁でカーソルが揺れると毎フレーム鳴り直してしまう
		/// (残響が重なって、だんだん大きくなったように聞こえる)
		/// </remarks>
		void PlaySound(
			Engine::GameObject::ObjectContext& a_context,
			const Core::GUID& a_guid,
			Engine::Handle<Engine::Resource::SoundInstance>& a_inoutHandle,
			float& a_inoutCoolTime);
	};
}
