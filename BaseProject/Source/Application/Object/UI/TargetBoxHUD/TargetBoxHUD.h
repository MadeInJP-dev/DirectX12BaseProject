#pragma once

#include "../UIBase.h"

namespace App::Object
{
	/// <summary>
	/// 敵へ重ねて表示するターゲットボックスHUD。
	///
	/// 出す相手は LockOnTargetSystem がプレイヤーの LockOnTargetComponent へ書いた結果。
	/// (HUDGatherSystem が PlayerHUDResource へまとめたものを読む。ワールドは直接見ない)
	///   レティクル内の敵           … 群 0 の飾り(黄色の枠)
	///   そのうち画面中央に最も近い … 群 1 の飾り(赤い枠)
	///
	/// スクリーン座標はロック判定側が計算済みのものをそのまま使う。ここで射影をやり直すと、
	/// 条件のわずかな差で「枠は出ているのにロックされない」といったズレが起きるため。
	///
	/// 位置は毎フレーム作り直すので、UIBase の PixelPos は使わない。
	/// 見た目は飾りをそのまま使い、位置だけ敵ごとに差し替えて出す。
	///
	/// 群 1 の飾りを1つも持たない場合は、群 0 の飾りを LockColor で染めて代用する
	/// (枠が消えるより分かりやすいため)。
	/// </summary>
	class TargetBoxHUD : public UIBase
	{
	public:

		// 押されることのない HUD なので、カーソルへの反応を持たない
		TargetBoxHUD() : UIBase(false) { m_isHUD = true; }	// ゲーム中の HUD : プレイヤーが HUD を消すと一緒に消える

		// 飾りの群 : 0 = 画面内の敵に出す枠 / 1 = ロック中の相手に出す枠
		static constexpr uint32_t GROUP_NORMAL = 0;
		static constexpr uint32_t GROUP_LOCK = 1;

		// 初期化処理 : 既定の枠を用意する
		void PostDeserialize(Engine::GameObject::ObjectContext& a_context) override;
		void Awake(Engine::GameObject::ObjectContext& a_context) override;

		// 更新処理 : プレイヤーのロック結果からスクリーン座標を集める
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 描画処理 : 集めた座標ぶんだけUIを積む
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "TargetBoxHUD"; }

		// インスペクター
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

		// 位置は敵から毎フレーム作るので、座標編集用のギズモは出さない
		bool DrawGizmo(
			const Engine::GameObject::ObjectGizmoContext& /*a_ctx*/,
			Engine::GameObject::ObjectContext& /*a_context*/) override {
			return false;
		}

	private:

		// その群の飾りを持っているか
		bool HasDecorationGroup(uint32_t a_group) const;

	private:

		// ロック枠の拡大率(通常枠のピクセルサイズに掛ける)
		float m_lockSizeScale = 1.0f;

		// ロック枠へ掛ける色。群 1 を持たないときは通常枠をこの色で染める
		Math::Color m_lockColor = Engine::Color::RED;

		// このフレームに描くボックスのスクリーン座標(px, 左上原点)。
		// Update で作って Draw で消費する。
		std::vector<Math::Vector2> m_targetScreenPosVec = {};

		// ロック中の相手のスクリーン座標(px)。isLocked が false のフレームは描かない
		Math::Vector2 m_lockedScreenPos = {};
		bool m_isLocked = false;

		// 旧形式(ロック枠テクスチャ1枚)からの引き継ぎ用
		Core::GUID m_legacyLockTexGUID = {};
	};
}
