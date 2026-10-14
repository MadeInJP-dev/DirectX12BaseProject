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
	/// スコアを数字で出す HUD。
	///
	/// ・数える側と出す側を分けてある。
	///   加算するのは ScoreSystem(倒した相手の ScoreTargetComponent を見る)で、
	///   合計は GlobalGameContext(GameManager が持つ)。ここは読んで並べるだけ。
	///   ワールドのリソースではなくグローバル側を見るので、
	///   ゲームシーンでもリザルトでも同じものがそのまま出せる。
	///
	/// ・数字は「0〜9 を横一列に並べた1枚のテクスチャ」から1コマずつ切り出して出す。
	///   このエンジンには文字を出す仕組みが無いので、絵として並べている。
	///   切り出しは SubmitUI の uvScale / uvOffset で行う
	///   (uvScale = 1 / 桁数ぶんの幅、uvOffset = 何コマ目か)。
	///
	/// ・桁は左から順に置く。表示桁数は固定で、足りないぶんは 0 で埋める
	///   (桁数で幅が変わると、加算のたびに数字が横へずれて読みにくいため)。
	/// </summary>
	class ScoreHUD : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない
		ScoreHUD() : UIBase(false) {}

		// 更新処理 : 合計を読み、増えたフレームは弾ませる
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

		// 1桁ぶんを描く。a_index は左から何桁目か
		void DrawDigit(
			Engine::GameObject::ObjectContext& a_context,
			int a_digit,
			int a_index,
			float a_scale);

	private:

		// ---- 表示(保存される) ----
		// 何を出すか。スコア・タイム・撃破数のどれでも同じ並べ方で出せる
		EScoreValueKind m_valueKind = EScoreValueKind::Score;

		int   m_digitCount   = 6;		// 表示する桁数(足りないぶんは 0 埋め)
		int   m_atlasCount   = 10;		// テクスチャに並んでいるコマ数(0〜9 なら 10)
		float m_digitSpacing = 0.0f;	// 桁と桁の間隔(px)。0 で隙間なく並ぶ

		// 増えた瞬間の演出。0 にすると弾まない
		float m_punchScale = 1.3f;		// 増えたフレームの拡大率
		float m_punchTime  = 0.2f;		// 元の大きさへ戻るまでの秒数

		// ---- ランタイム ----
		int   m_value      = 0;			// 表示している数(GlobalGameContext から毎フレーム貰う)
		int   m_prevValue  = 0;			// 前フレームの数。増えた瞬間を見るために覚えておく
		bool  m_isFirst    = true;		// 初回か(出た瞬間に弾ませないための印)
		float m_punchTimer = 0.0f;		// 戻るまでの残り時間(秒)
	};
}
