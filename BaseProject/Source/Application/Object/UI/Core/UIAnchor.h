#pragma once

#include "Application/Object/UI/Decoration/DecorationDraw.h"

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

		// 湾曲(飾りも含めてこのUI全体を「1つの円」に巻き付ける)
		//
		// pixelSize の幅がちょうど開き角ぶんの弧になるよう巻き付ける。
		// 弧の長さは元の幅のままなので、曲げるほど見た目の横幅(弦)は狭くなり、文字も伸びない。
		// 縦の中心線(ピボットの高さ)が円周に乗り、それより下は内側・上は外側になる。
		// 枠と中身のように幅が違う飾りでも同じ円に乗る(ゲージの残量が枠から外れない)。
		// pixelSize が0だと曲がらない。
		// 曲げるUIは横に分割した板ポリで描かれる(UIData::IsCurved を見て切り替わる)
		Math::Vector2 curveCenter = {};			// 弧の頂点。pixelSize を -1..1 とした座標(x=横位置 / y=上下のずらし)
		float curveRadius = 1.0f;				// 巻き付ける角度の倍率(1で開き角そのまま。0も1として扱う)
		float curveAngle = 0.0f;				// 開き角(ラジアン)。pixelSize の幅がこの角度の弧になる。0で曲げない。正で山なり、負で谷

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

		// 度で回す : IsPointInside と同じ向き(時計回り)
		static Math::Vector2 RotateDeg(const Math::Vector2& a_value, float a_degree);
	};
}
