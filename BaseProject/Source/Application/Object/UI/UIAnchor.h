#pragma once

#include "Decoration.h"

namespace App::Object
{
	//======================================================================================
	// UIのアンカー : 画面のどこに、どの向き・どの大きさで置くか
	//
	// UIBase から切り出したもの。見えるもの(飾り)はここから相対で出る。
	//
	// pixelSize はアンカー自身の矩形。当たり判定(UIInteraction)や
	// 判定円の大きさ(AimReticleHUD / CombatReticleHUD)がこれを見る。
	// 見た目そのものは飾り側の pixelSize が決めるので、両者は別物であることに注意。
	//
	// 保存は UIBase::Archive が行う(旧形式の名残と並びが混ざっているため、ここでは持たない)
	//======================================================================================
	struct UIAnchor
	{
		// 色 : 全ての飾りへ乗算で掛かる
		Math::Color color = Engine::Color::WHITE;

		// 座標系
		Math::Vector2 pixelPos = {};			// ピボットのスクリーン座標(px)
		Math::Vector2 pixelSize = {};			// アンカー自身の大きさ(px) : 当たり判定・判定円の基準
		float rotation = 0.0f;					// 回転(度)

		// オプション
		Math::Vector2 pivot = { 0.5f, 0.5f };	// 回転軸/基準点(正規化[0,1], 0.5=中心)
		float layer = 0.0f;						// Z位置。大きいほど手前
		Math::Vector2 editSize = {};			// エディターでいじる際のピクセルサイズ(倍率を掛ける前)
		float scale = 1.0f;						// 等倍スケール用

		// 湾曲(飾りも含めてこのUI全体に「1本の弧」として掛かる)
		//
		// 曲げても幅は変わらない。角度を入れた分だけ反るだけ。
		// 弧は pixelSize を -1..1 として張るので、枠と中身のように幅が違う飾りでも
		// 同じ弧に乗る(ゲージの残量が枠から外れない)。pixelSize が0だと曲がらない。
		// 曲げるUIは横に分割した板ポリで描かれる(UIData::IsCurved を見て切り替わる)
		Math::Vector2 curveCenter = {};			// 弧の頂点。pixelSize を -1..1 とした座標(x=横位置 / y=上下のずらし)
		float curveRadius = 1.0f;				// 反りの深さの倍率(1で素直な円弧。0も1として扱う)
		float curveAngle = 0.0f;				// 開き角(ラジアン)。0で曲げない。正で山なり、負で谷

		// スキャンライン(ScanLinePass)を掛けるか。飾りも含めてこのUI全体に効く。
		// パイプラインで UIPass の ScanLineMask を ScanLinePass の Mask へ繋いでおく必要がある
		bool isScanLine = false;

		//==================================================================================

		// 大きさを持っているか(幅0の矩形はどこにも当たらない)
		bool HasSize() const { return pixelSize.x > 0.0f && pixelSize.y > 0.0f; }

		// 飾りへ渡す形で取り出す
		Decoration::ParentTransform MakeParentTransform() const;
		Decoration::ParentOption MakeParentOption() const;

		/// <summary>
		/// アンカー上の点(アンカーからのずれ, px)を画面の座標へ直す
		/// </summary>
		/// <remarks>倍率と回転はアンカーのものが乗る</remarks>
		Math::Vector2 ToScreenPos(const Math::Vector2& a_localOffset) const;

		/// <summary>
		/// インスペクター(エディター用)
		/// </summary>
		/// <param name="a_screenW">描画解像度の幅(px)。初期化ボタンで画面中央へ戻すのに使う</param>
		/// <param name="a_screenH">描画解像度の高さ(px)</param>
		void DrawInspector(float a_screenW, float a_screenH);

		//==================================================================================
		// 当たり判定(アンカーを持たない側からも使えるように静的にしてある)
		//==================================================================================

		/// <summary>
		/// UIのピクセル座標が矩形の内側にあるか
		/// </summary>
		/// <param name="a_uiPos">判定する点(UIのピクセル座標)</param>
		/// <param name="a_pixelPos">ピボットのスクリーン座標(px)</param>
		/// <param name="a_pixelSize">矩形の大きさ(px)</param>
		/// <param name="a_pivot">正規化ピボット[0,1]</param>
		/// <param name="a_rotationDeg">回転(度)</param>
		/// <param name="a_hitPadding">矩形へ足す余白(px)</param>
		static bool IsPointInside(
			const Math::Vector2& a_uiPos,
			const Math::Vector2& a_pixelPos,
			const Math::Vector2& a_pixelSize,
			const Math::Vector2& a_pivot,
			float a_rotationDeg,
			const Math::Vector2& a_hitPadding = {});

		/// <summary>
		/// 矩形の4隅を求める(IsPointInside と同じ矩形)
		/// </summary>
		/// <param name="a_outCorners">左上・右上・右下・左下の順(回転前の見た目で)。そのまま線で結べる</param>
		static void CalcRectCorners(
			const Math::Vector2& a_pixelPos,
			const Math::Vector2& a_pixelSize,
			const Math::Vector2& a_pivot,
			float a_rotationDeg,
			const Math::Vector2& a_hitPadding,
			Math::Vector2 (&a_outCorners)[4]);

		// 度で回す : IsPointInside と同じ向き(時計回り)
		static Math::Vector2 RotateDeg(const Math::Vector2& a_value, float a_degree);
	};
}
