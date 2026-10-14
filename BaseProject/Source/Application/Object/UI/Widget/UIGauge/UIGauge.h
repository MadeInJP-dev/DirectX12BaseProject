#pragma once

#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	/// <summary>
	/// ゲージの伸び縮みの向き
	/// </summary>
	/// <remarks>
	/// 減ったぶんをどちら側から削るか。
	/// 動かさない場所をピボットにして、そこを固定したまま横幅を縮める。
	///
	/// ※ 値は保存されるので、増やすときは末尾へ足すこと
	/// </remarks>
	enum class EGaugeAnchor : uint32_t
	{
		Left,	// 左端を固定して右から減る
		Right,	// 右端を固定して左から減る
		Center,	// 中央を固定して両側から減る(中央から外へ伸びる)
	};

	/// <summary>
	/// 数値の出し方
	/// </summary>
	enum class EGaugeTextFormat : uint32_t
	{
		None,			// 出さない
		Value,			// 現在値だけ        (例 : 72)
		ValueAndMax,	// 現在値と最大値    (例 : 72/100)
		Percent,		// 割合              (例 : 72%)
	};

	/// <summary>
	/// 誰の値を出すか
	/// </summary>
	/// <remarks>
	/// 見るエンティティの決め方。実際に探すのは HUDGatherSystem で、
	/// 毎フレーム引き直すので、ロックが外れた・敵が消えた といった入れ替わりにそのまま追従する
	///
	/// ※ 値は保存されるので、増やすときは末尾へ足すこと
	/// </remarks>
	enum class EGaugeTarget : uint32_t
	{
		Manual,				// 相手を持たない(値は SetValue で入れる。Source も Manual にすること)
		Player,				// 操作しているプレイヤー
		LockedEnemy,		// プレイヤーがロックしている敵
		PlayerRightWeapon,	// プレイヤーの右手武器(オーバーヒート用)
		PlayerLeftWeapon,	// プレイヤーの左手武器(オーバーヒート用)
	};

	/// <summary>
	/// 何の値を出すか
	/// </summary>
	/// <remarks>
	/// 対応するコンポーネントを持っていなければ、そのフレームは何も出さない。
	/// 「HPを出すゲージにロック中の敵を入れたが、その敵はHPを持っていない」
	/// といった組み合わせでも落ちないようにするため
	/// </remarks>
	enum class EGaugeSource : uint32_t
	{
		Manual,			// 外から SetValue で入れる(コンポーネントを見ない)
		Health,			// HealthComponent      : currentHealth / maxHealth
		BoostFuel,		// BoostStateComponent / BoostParamsComponent : currentFuel / maxFuel
		Overheat,		// WeaponOverHeatComponent : heat / limit
		ChargeDash,		// ChargeDashComponent  : chargeTimer / chargeTime
		Magazine,		// WeaponReloadComponent : 残弾 / 容量(リロード中は進み具合)。※ 保存値なので末尾に足すこと
	};

	/// <summary>
	/// 残量ごとの色
	/// </summary>
	struct GaugeColorStop
	{
		float ratio = 0.0f;							// この残量(0〜1)のときの色
		Math::Color color = Engine::Color::WHITE;
	};

	//======================================================================================
	// ゲージUI : HP / オーバーヒート / ブーストのエナジーなど
	//
	// 受け取るのは「現在値」と「最大値」の2つだけ。何の値かは知らない。
	// 入れるのは持っている側(SetValue)で、UIButton が SetOnClick で外から
	// ふるまいを差し込むのと同じ作り。
	//
	// ・Target / Source を選ぶと PlayerHUDResource から毎フレーム取る
	//     ワールドを直接は見ない。集めるのは HUDGatherSystem で、
	//     見る値を増やすときは EHUDGaugeKind と HUDGatherSystem へ足してから、ここの対応を足す。
	//
	// ・伸び縮みは飾り1つの横幅で表す
	//     選んだ飾り(番号で指す)の横幅へ残量を掛けて描く。
	//     残す側の端をピボットにするので、そこを固定したまま反対側から削れていく。
	//     縮めるのは大きさだけで、枠の太さや位置のずれには掛からない。
	//
	// ・枠・背景・下地は同じUIへ飾りとして並べる
	//     配列順がそのまま重なり順なので、下地 → 中身 → 枠 の順に置けばよい。
	//
	// ・数値は Text の飾りへ流し込む
	//     置き場所は飾りの OffsetPos なので、ゲージの中でも脇でも好きな位置に置ける。
	//     文字を出すにはフォントの割り当てが要る。
	//
	// ・残量で色が変わる
	//     「この残量でこの色」を並べておくと、そのときの残量に応じた色が
	//     中身の飾りへ掛かる。なめらかに混ぜるか、しきい値でパッと切り替えるかを選べる。
	//======================================================================================
	class UIGauge final : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない
		UIGauge() : UIBase(false) {}

		//=======================================================================
		// 値
		//=======================================================================

		/// <summary>
		/// 表示する値を入れる
		/// </summary>
		/// <param name="a_current">現在値</param>
		/// <param name="a_max">最大値。0以下だと空として扱う</param>
		/// <remarks>Source が Manual のときだけ効く。それ以外は毎フレーム上書きされる</remarks>
		void SetValue(float a_current, float a_max);

		// 現在値だけ差し替える(最大値は据え置き)
		void SetCurrent(float a_current);

		float GetCurrent() const { return m_current; }
		float GetMax() const { return m_max; }

		// 残量(0〜1)
		float GetRatio() const;

		//=======================================================================
		// オブジェクト
		//=======================================================================

		// 更新処理 : 数値の流し込みと、ピボットの合わせ込み
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 描画処理 : 中身だけ横に縮めて描く
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "UIGauge"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		//-------------------------------------------------------------------
		// 値の取り込み
		//-------------------------------------------------------------------

		// 設定に沿って PlayerHUDResource から値を取る。取れたら true
		bool PickValue(Engine::GameObject::ObjectContext& a_context);

		// 残量に対応する色を出す
		Math::Color CalcGaugeColor(float a_ratio) const;

		// 数値を Text の飾りへ流し込む(変わったときだけ)
		void ApplyValueText();

		// 中身の飾りのピボットを、伸び縮みの向きへ合わせる
		void ApplyFillPivot();

		// 表示する文字列を作る
		std::string MakeValueText() const;

	private:

		//-------------------------------------------------------------------
		// 設定(保存される) : ゲージ
		//-------------------------------------------------------------------
		// 横幅を縮める飾りの番号(Decoration::id)。この飾りが「中身」になる。0 で無し
		uint32_t m_fillDecorationId = 0;

		// どちら側を固定して縮めるか
		// (UIBase のアンカー(置き場所)とは別物。名前を分けてあるのはそのため)
		EGaugeAnchor m_fillAnchor = EGaugeAnchor::Left;

		//-------------------------------------------------------------------
		// 設定(保存される) : 色
		//-------------------------------------------------------------------
		// 残量ごとの色。残量の小さい順に並べて持つ
		std::vector<GaugeColorStop> m_colorStopVec = {
			{ 0.0f, { 1.0f, 0.25f, 0.20f, 1.0f } },	// 空に近い : 赤
			{ 0.35f,{ 1.0f, 0.85f, 0.25f, 1.0f } },	// 半分以下 : 黄
			{ 0.70f,{ 0.35f,1.0f,  0.45f, 1.0f } },	// 十分     : 緑
		};

		// なめらかに混ぜるか。切ると、しきい値でパッと切り替わる
		bool m_isBlendColor = true;

		//-------------------------------------------------------------------
		// 設定(保存される) : 数値
		//-------------------------------------------------------------------
		// 数値を流し込む Text 飾りの番号(Decoration::id)。0 で無し
		uint32_t m_textDecorationId = 0;

		EGaugeTextFormat m_textFormat = EGaugeTextFormat::Value;

		// 小数点以下の桁数
		int m_decimals = 0;

		//-------------------------------------------------------------------
		// 設定(保存される) : どこから値を取るか
		//-------------------------------------------------------------------
		EGaugeTarget m_target = EGaugeTarget::Manual;
		EGaugeSource m_source = EGaugeSource::Manual;

		/// <summary>
		/// 値が取れないフレームは何も描かないか
		/// </summary>
		/// <remarks>
		/// ロックしていない・相手がそのコンポーネントを持っていない、といったとき用。
		/// 切ると最後に取れた値を出したままにする。
		///
		/// Visible ではなく描画だけを止めるのは、
		/// 進行役(HomeSequence など)が握っている出し入れと取り合いにならないようにするため
		/// </remarks>
		bool m_isHideWhenNoValue = true;

		//-------------------------------------------------------------------
		// 値(保存しない)
		//-------------------------------------------------------------------
		float m_current = 100.0f;
		float m_max = 100.0f;

		// このフレームに値を取れたか
		bool m_hasValue = true;

		// 最後に流し込んだ文字列。変わったときだけ書き換える
		std::string m_appliedText = {};

		//-------------------------------------------------------------------
		// 名前で飾りを指していた頃の名残(保存の並びを保つために読み書きだけ続ける)
		//
		// 番号を持たない古いデータは、読み込みの後にこの名前から番号を引き直す
		//-------------------------------------------------------------------
		std::string m_legacyFillDecorationName = "Fill";
		std::string m_legacyTextDecorationName = "Value";
	};
}
