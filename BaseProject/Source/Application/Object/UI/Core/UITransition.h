#pragma once

#include "Application/Object/UI/Decoration/DecorationAnimation.h"	// イージング

namespace Engine::Persistence { class Archive; }

namespace App::Object
{
	//======================================================================================
	// 演出1回ぶん : 始まりの値から終わりの値へ duration 秒で動かす
	//
	// 値は UI 全体(全部の飾り)へ掛ける。既定のままなら何も変えない。
	//   alpha    … 色の a へ掛ける倍率(1 で変化なし)
	//   scale    … 大きさへ掛ける倍率(飾りの位置のずれにも掛かるので、並びごと伸び縮みする)
	//   dissolve … ディゾルブの進み具合(0 で変化なし・1 で全部消える)。
	//              UI にディゾルブの UIマテリアルを付けたときだけ効く
	//
	// ※ 動かす値を足すときは、UITransitionValue・Lerp・保存(末尾)の3か所へ
	//======================================================================================
	struct UITransitionClip
	{
		float duration = 0.2f;										// かかる時間(秒)。0 以下なら流さない
		Decoration::EEase ease = Decoration::EEase::OutQuad;		// 進み具合の曲線

		float startAlpha = 1.0f;
		float endAlpha = 1.0f;

		float startScale = 1.0f;
		float endScale = 1.0f;

		float startDissolve = 0.0f;
		float endDissolve = 0.0f;
	};

	//--------------------------------------------------------------------------------------
	// いま掛かっている値
	//--------------------------------------------------------------------------------------
	struct UITransitionValue
	{
		float alpha = 1.0f;
		float scale = 1.0f;
		float dissolve = 0.0f;
	};

	//======================================================================================
	// UIの出し入れと合図の演出
	//
	// 拡大して戻る・薄くなって消える、といった「UI 全体の動き」を HUD ごとに書かずに済むよう、
	// UIBase がこれを1つ持ち、描くときに全部の飾りへ掛ける。飾り1つずつの動き
	// (Decoration の Tween / Oscillation / Reaction)とは別で、その上から掛かる。
	//
	//   show  … SetVisible(true) で流す(出てくるとき)
	//   hide  … SetVisible(false) で流す(消えるとき)。流れ終わるまでは描き続ける
	//   pulse … PlayPulse で流す(当たった・増えた、などの合図)。持ち主のコードが呼ぶ
	//
	// show / hide は使うと決めたときだけ流す(既定は使わない = すぐ出る・すぐ消える)。
	// pulse は呼ばれたときだけ流れるので、値が 1 のままなら何も変わらない。
	// 出し入れと合図は別々に進み、掛かる値は2つの積になる。
	//
	// ・hide を流している間は Visible が切れているので、押せない(描かれるだけ)
	// ・流れ終わったら値は 1 へ戻る。show の終わりと hide の始まりは 1 にしておくと継ぎ目が出ない
	// ・UIPanel を隠したときは、下の UI の hide は流れない(パネルの表示は下へ伝わるだけで、
	//   下の UI の SetVisible は呼ばれないため)
	//======================================================================================
	struct UITransition
	{
		//-----------------------------------------------------------------------
		// 設定(保存される)
		//-----------------------------------------------------------------------
		bool isUseShow = false;			// 出てくるときに show を流すか
		bool isUseHide = false;			// 消えるときに hide を流すか
		UITransitionClip show = { 0.2f, Decoration::EEase::OutQuad, 0.0f, 1.0f, 0.9f, 1.0f, 0.0f, 0.0f };
		UITransitionClip hide = { 0.15f, Decoration::EEase::InQuad, 1.0f, 0.0f, 1.0f, 0.9f, 0.0f, 0.0f };
		UITransitionClip pulse = {};

		// 合図を流している間だけ描く(ヒットマーカー・ウェーブの告知のような、一瞬出して消えるもの)
		bool isPulseOnly = false;

		//-----------------------------------------------------------------------
		// 状態(保存しない)
		//-----------------------------------------------------------------------
		enum class EVisiblePhase : uint32_t
		{
			Idle,		// 何も流していない
			Showing,	// show を流している
			Hiding,		// hide を流している
		};
		EVisiblePhase visiblePhase = EVisiblePhase::Idle;
		float visibleTime = 0.0f;		// 出し入れの経過時間(秒)

		bool isPulsePlaying = false;
		float pulseTime = 0.0f;			// 合図の経過時間(秒)

		//-----------------------------------------------------------------------
		// 操作
		//-----------------------------------------------------------------------

		// 表示が切り替わった : 使うと決めていれば show / hide を頭から流す
		void OnVisibleChanged(bool a_isVisible);

		// show / hide を頭から流す(使うかどうかに関わらず。エディターの確認用)
		void PlayShow();
		void PlayHide();

		// 合図を頭から流す(流している最中なら出し直す)
		void PlayPulse();

		// 時間を進める(UIBase::Update から)
		void Advance(float a_deltaTime);

		// hide を流している最中か : Visible が切れていても描き続ける
		bool IsHiding() const { return visiblePhase == EVisiblePhase::Hiding; }

		// 合図を流している最中か
		bool IsPulsePlaying() const { return isPulsePlaying; }

		// いま掛かっている値(出し入れ × 合図)
		UITransitionValue CalcValue() const;

		//-----------------------------------------------------------------------
		// 保存・エディター
		//-----------------------------------------------------------------------

		/// <summary>
		/// アーカイブ
		/// </summary>
		/// <returns>
		/// 読み込みで、保存されていた値を読めたら true。
		/// 演出を持つ前のデータ(読めなかった)なら false で、値はクラスの既定のまま残る。
		/// 持ち主が古い形式の値から移すかどうかの判断に使う
		/// </returns>
		/// <remarks>フィールドを足すときは必ず末尾へ(区切りの中は並び順で読むため)</remarks>
		bool Archive(Engine::Persistence::Archive& a_ar);

		// 設定の編集欄(確認用のボタンは持ち主が出す)
		void DrawSettingsInspector();
	};
}
