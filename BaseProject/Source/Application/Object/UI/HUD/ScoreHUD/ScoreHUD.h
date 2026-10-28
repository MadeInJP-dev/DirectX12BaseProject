#pragma once

#include "Application/Object/UI/UIBase.h"

namespace App::Game
{
	struct GlobalGameContext;
}

namespace App::Object
{
	/// <summary>
	/// GlobalGameContext の数字(スコア・タイム・撃破数)を出す HUD。
	/// </summary>
	enum class EScoreValueKind : uint32_t
	{
		Score,		// スコア
		Time,		// タイム(秒。小数は切り捨て)
		KillCount,	// 倒した数
	};

	/// <summary>
	/// 数字の出し方
	/// </summary>
	enum class EScoreDrawMode : uint32_t
	{
		Atlas,		// 0〜9 を並べた1枚の絵から1コマずつ切り出して並べる(飾りは全部が1桁ぶん)
		Text,		// 選んだ Text の飾りへ文字列として流し込む(ほかの飾りはそのまま描く)
	};

	/// <summary>
	/// タイムの書式(Text のときだけ効く。Atlas は秒だけ)
	/// </summary>
	enum class EScoreTimeFormat : uint32_t
	{
		Seconds,	// 秒だけ(小数は切り捨て)。例 : 0083
		Clock,		// 分:秒.1/100秒。例 : 01:23.45
	};

	/// <summary>
	/// スコアを数字で出す HUD。
	///
	/// ・数える側と出す側を分けてある。
	///   加算するのは ScoreSystem(倒した相手の ScoreTargetComponent を見る)で、
	///   合計は GlobalGameContext(GameManager が持つ)。ここは読んで並べるだけ。
	///   ワールドのリソースではなくグローバル側を見るので、
	///   ゲームシーンでもリザルトでも同じものがそのまま出せる。
	///
	/// ・出し方は2つ(DrawMode)。
	///   Atlas : 「0〜9 を横一列に並べた1枚のテクスチャ」から1コマずつ切り出して出す。
	///           切り出しは SubmitUI の uvScale / uvOffset で行う
	///           (uvScale = 1 / コマ数ぶんの幅、uvOffset = 何コマ目か)。
	///           飾りは全部が1桁ぶんとして桁の数だけ繰り返し描かれる。
	///   Text  : 選んだ Text の飾り1つへ文字列を流し込む(UIGauge の数値と同じ作り)。
	///           見出し("TIME" など)や下敷きは別の飾りとして並べれば一緒に出る。
	///           タイムは「分:秒.1/100秒」でも出せる。
	///           数字は1文字ずつ同じ幅のマスへ置く。フォントの数字は字ごとに幅が違うので、
	///           文字列のまま寄せると値が変わるたびに全体が左右へ揺れるため。
	///           記号(: や .)は自分の幅だけ取る。
	///
	/// ・表示桁数は固定で、足りないぶんは 0 で埋める
	///   (桁数で幅が変わると、加算のたびに数字が横へずれて読みにくいため)。
	///
	/// ・増えた瞬間の弾み方は UIBase の演出(Transition の Pulse)で決める。ここは合図を出すだけ。
	///   タイムは毎秒増えるので弾ませない。
	/// </summary>
	class ScoreHUD : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない
		ScoreHUD();

		// 更新処理 : 合計を読み、増えたフレームは合図を流す
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 描画処理 : 桁ぶんの数字を並べる
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "ScoreHUD"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 出す数を GlobalGameContext から取る(無ければ 0)
		int PickValue(const Game::GlobalGameContext* a_pGameData) const;

		// 出す文字列を組む(Text のとき)
		std::string MakeValueText(const Game::GlobalGameContext* a_pGameData) const;

		// 選んだ Text の飾りへ文字列を入れる(変わったときだけ)
		void ApplyValueText(const std::string& a_text);

		// 1文字ずつのマスの幅(px)を出す。数字は一番広い数字の幅にそろえる。
		// フォントがまだ読めていなければ false
		bool CalcCharWidths(
			Engine::GameObject::ObjectContext& a_context,
			const Decoration::UITextElement& a_text,
			std::vector<float>& a_outWidthVec) const;

		// 数字の飾りを1文字ずつマスへ並べて描く(a_index は飾りの並びの何番目か)
		void DrawValueByChar(Engine::GameObject::ObjectContext& a_context, size_t a_index);

		// 1桁ぶんを描く。a_index は左から何桁目か
		void DrawDigit(
			Engine::GameObject::ObjectContext& a_context,
			int a_digit,
			int a_index);

		// 新しく置いたときの弾み方を演出へ入れる
		void SetDefaultTransition();

	private:

		// ---- 表示(保存される) ----
		// 何を出すか。スコア・タイム・撃破数のどれでも同じ並べ方で出せる
		EScoreValueKind m_valueKind = EScoreValueKind::Score;

		int   m_digitCount   = 6;		// 表示する桁数(足りないぶんは 0 埋め)
		int   m_atlasCount   = 10;		// テクスチャに並んでいるコマ数(0〜9 なら 10)
		float m_digitSpacing = 0.0f;	// 桁と桁の間隔(px)。0 で隙間なく並ぶ

		// ---- 文字で出すとき(保存される。区切りを入れた後に足したもの) ----
		EScoreDrawMode   m_drawMode   = EScoreDrawMode::Atlas;		// 出し方
		uint32_t         m_textDecorationId = 0;					// 数字を流し込む Text の飾り(0 で未選択)
		EScoreTimeFormat m_timeFormat = EScoreTimeFormat::Clock;	// タイムの書式
		float            m_charPitch  = 0.0f;	// 数字1文字ぶんのマスの幅(px)。0 でフォントの一番広い数字に合わせる

		// ---- ランタイム ----
		std::string m_appliedText = {};	// 最後に流し込んだ文字列(変わったときだけ書き換えるため)

		int   m_value      = 0;			// 表示している数(GlobalGameContext から毎フレーム貰う)
		int   m_prevValue  = 0;			// 前フレームの数。増えた瞬間を見るために覚えておく
		bool  m_isFirst    = true;		// 初回か(出た瞬間に弾ませないための印)
	};
}
