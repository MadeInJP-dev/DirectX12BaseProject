#pragma once

#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	/// <summary>
	/// ミサイルの溜め中に、ロックした敵を囲む枠(黄色)。
	///
	/// 出す相手は MissileSalvoSystem がプレイヤーの MissileLockComponent へ書いた結果。
	/// (HUDGatherSystem が PlayerHUDResource へまとめたものを読む。ワールドは直接見ない)
	/// ミサイルキーを押している間(isCharging)だけ、溜まっているぶんの枠を出す。
	/// 離して撃った瞬間に溜めは捨てられるので、枠もそのフレームで消える。
	///
	/// スクリーン座標は収集側が計算済みのものをそのまま使う。ここで射影をやり直すと、
	/// 条件のわずかな差で「枠は出ているのにロックされない」といったズレが起きるため。
	/// (TargetBoxHUD と同じ考え方)
	///
	/// 位置は毎フレーム作り直すので、UIBase の PixelPos は使わない。
	/// サイズ・色・回転・ピボットは UIBase 側の値を全ボックス共通で使う。
	/// </summary>
	class MissileLockBoxHUD : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない
		MissileLockBoxHUD() : UIBase(false) { m_isHUD = true; }	// ゲーム中の HUD : プレイヤーが HUD を消すと一緒に消える

		// 初期化処理 : 枠テクスチャの読み込み
		void PostDeserialize(Engine::GameObject::ObjectContext& a_context) override;
		void Awake(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : プレイヤーの溜め結果からスクリーン座標を集める
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 描画処理 : 集めた座標ぶんだけUIを積む
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "MissileLockBoxHUD"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

		// 位置は敵から毎フレーム作るので、座標編集用のギズモは出さない
		bool DrawGizmo(
			const Engine::GameObject::ObjectGizmoContext& /*a_ctx*/,
			Engine::GameObject::ObjectContext& /*a_context*/) override {
			return false;
		}

	private:

		// このフレームに描く枠のスクリーン座標(px, 左上原点)。
		// Update で作って Draw で消費する
		std::vector<Math::Vector2> m_lockScreenPosVec = {};
	};
}
