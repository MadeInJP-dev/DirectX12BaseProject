#pragma once

#include "../UIBase.h"

namespace App::Object
{
	/// <summary>
	/// 画面中央の内側レティクル(オートエイム用)。
	///
	/// CombatReticleHUD が「戦闘中の外枠」なのに対して、こちらは
	/// 「この円の内側に入った敵だけがロック対象になる」判定そのものを表す UI。
	///
	/// 判定の円(中心と半径)はプレイヤーの LockOnTargetComponent が持つ唯一の正解で、
	/// この UI はそれを PlayerHUDResource 経由で受け取り、絵を合わせて出すだけ。
	/// (以前は UI が円をプレイヤーへ書き込んでいたため、置き忘れると判定が変わっていた)
	///
	/// 絵の合わせ方 : アンカーの上で「判定の円に当たる半径」(CalcArtRadius)を決めておき、
	/// それが判定の半径と同じ大きさになるよう倍率を掛けて描く。
	/// 判定の円が届いていないとき(プレイ中でない・プレイヤーが居ない)は置いたとおりに出す。
	/// </summary>
	class AimReticleHUD : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない
		AimReticleHUD() : UIBase(false) { m_isHUD = true; }	// ゲーム中の HUD : プレイヤーが HUD を消すと一緒に消える

		// 初期化処理 : レティクルテクスチャの読み込み
		void PostDeserialize(Engine::GameObject::ObjectContext& a_context) override;
		void Awake(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : 判定の円を受け取る
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 描画処理 : 判定の円に合わせて描く
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "AimReticleHUD"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

	private:

		// アンカーの上で、判定の円に当たる半径(px)を求める
		float CalcArtRadius() const;

	private:

		// 判定の円に当たる半径をアンカーの大きさ(PixelSize)から作るか。
		// true  : PixelSize の半分 × radiusScale
		// false : lockRadius をそのまま使う(絵に余白がある時などに手で詰める)
		bool  m_isUseTextureSize = true;

		float m_radiusScale = 1.0f;		// 表示サイズから作るときの倍率
		float m_lockRadius  = 80.0f;	// 手で指定するときの半径(px)

		// ---- ランタイム ----
		// このフレームの判定の円(PlayerHUDResource から貰う)
		Math::Vector2 m_reticleCenter = {};
		float m_reticleRadius = 0.0f;
		bool  m_hasReticle = false;
	};
}
