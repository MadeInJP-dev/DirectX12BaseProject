#pragma once

#include "../UIBase.h"

namespace App::Object
{
	/// <summary>
	/// 戦闘時に画面中央へ表示する照準(レティクル)HUD。外枠のほう。
	///
	/// ミサイルのターゲット収集範囲を表す。収集の円はプレイヤーの MissileLockComponent が持つ
	/// 唯一の正解(見た目の半径 reticleRadius × 判定の倍率 reticleScale)で、
	/// この UI は PlayerHUDResource 経由で見た目の円を受け取り、絵を合わせて出すだけ。
	/// (以前は UI が円をプレイヤーへ書き込んでいたため、置き忘れると判定が変わっていた)
	///
	/// 絵の合わせ方 : アンカーの PixelSize に内接する円を「見た目の円」とみなし、
	/// それが reticleRadius と同じ大きさになるよう倍率を掛けて描く。
	/// 円が届いていないとき(プレイ中でない・プレイヤーが居ない)は置いたとおりに出す。
	///
	/// 内側の AimReticleHUD は銃のロックオン(LockOnTargetComponent)用で別枠。
	/// </summary>
	class CombatReticleHUD : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない
		CombatReticleHUD() : UIBase(false) { m_isHUD = true; }	// ゲーム中の HUD : プレイヤーが HUD を消すと一緒に消える

		// 初期化処理 : レティクルテクスチャの読み込み
		void PostDeserialize(Engine::GameObject::ObjectContext& a_context) override;
		void Awake(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : 見た目の円を受け取る
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 描画処理 : 見た目の円に合わせて描く
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "CombatReticleHUD"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// アンカーの上で、見た目の円に当たる半径(px)を求める。
		// アンカーの PixelSize に内接する円
		float CalcArtRadius() const;

	private:

		// ---- ランタイム ----
		// このフレームの見た目の円(PlayerHUDResource から貰う)
		Math::Vector2 m_reticleCenter = {};
		float m_reticleRadius = 0.0f;
		bool  m_hasReticle = false;
	};
}
