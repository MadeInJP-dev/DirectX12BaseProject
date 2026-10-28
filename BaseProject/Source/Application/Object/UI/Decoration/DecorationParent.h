#pragma once

#include "Application/AppCommon.h"
#include "Engine/Graphics/Frame/DrawList/UIData.h"	// 切り抜き矩形(UIClipRect)

//==========================================================================================
// 飾りが親(UI)から受け取るもの
//
// 親のトランスフォーム・親だけが持つ設定・その場かぎりの差し替え。
// 旧 Decoration(DecorationDraw.h)と新しいエレメント(UIDecorationDrawContext.h)の両方が使う
//==========================================================================================
namespace Engine::Resource { struct UIMaterial; }

namespace App::Object::Decoration
{
	//======================================================================================
	// 親(UI)のトランスフォーム : デコレーションを合成する土台
	//======================================================================================
	struct ParentTransform
	{
		Math::Vector2 pixelPos = {};				// ピボットのスクリーン座標(px)
		float rotation = 0.0f;						// 回転(度)
		float scale = 1.0f;							// 倍率
		float layer = 0.0f;							// Z順
		Math::Color color = Engine::Color::WHITE;	// 全デコレーションへ掛かる色
	};

	// 親しか持たない固有の設定、子供全体にかかる
	//
	// 湾曲は「UI1つにつき弧が1本」。枠も中身も文字も同じ弧に乗せたいので、
	// 弧そのものの形は飾りではなく親が持つ
	struct ParentOption
	{
		// 弧が張る幅の基準になる、親自身の矩形(px)。
		// これが弧の -1..1 になる。UIAnchor::pixelSize がそのまま入る
		Math::Vector2 parentSize = {};

		// 親自身の矩形のピボット(正規化[0,1])。UIAnchor::pivot がそのまま入る。
		// 親の矩形に当てるボディ(UIHitBody::isUseParentRect)が使う
		Math::Vector2 parentPivot = { 0.5f, 0.5f };

		// 弧の頂点(反りの中心)。親の矩形を -1..1 とした座標。
		//   x … 頂点を横のどこに置くか(0で中央)
		//   y … 曲げたときに全体を上下へずらす量
		Math::Vector2 curveCenter = {};

		float curveRadius = 1.0f;			// 反りの深さの倍率(1で素直な円弧。0も1として扱う)
		float curveAngle = 0.0f;			// 開き角(ラジアン)。0で曲げない。正で山なり、負で谷

		// 切り抜き(px)。持っていれば、この矩形の外へ出たぶんは描かない。
		// アンカーの設定ではなく、持ち主の UI が実行中だけ持つもの
		// (スクロールする一覧の項目など。UIBase::SetClipRect)
		std::optional<Engine::Graphics::UIClipRect> opClipRect = std::nullopt;

		// スキャンライン(ScanLinePass)を掛けるか。UIAnchor::isScanLine がそのまま入る
		bool isScanLine = false;

		// 掛ける効果(UIマテリアル)。UI 単位で1つ持ち、飾りは全部これに従う。
		// 読み込み中・未指定なら nullptr(何も掛けない)。指すだけで持ち主は UI
		const Engine::Resource::UIMaterial* pMaterial = nullptr;
	};

	//======================================================================================
	// 描くときの一時的な上書き
	//
	// 「同じ見た目を敵の数だけ別の場所へ出す」「桁ごとにUVをずらす」といった、
	// 保存しないその場かぎりの差し替えをここへまとめる。
	// デコレーション側の値を書き換えて描くと、次のフレームまで汚れが残ってしまうため
	//======================================================================================
	struct DrawOverride
	{
		// 位置の差し替え : 親のピクセル座標を無視してここへ出す
		bool isUsePos = false;
		Math::Vector2 pixelPos = {};

		float scale = 1.0f;							// さらに掛ける倍率(位置のずれにも掛かる)

		/// <summary>大きさだけへ掛ける倍率(軸ごと)</summary>
		/// <remarks>
		/// ゲージのように「横だけ縮める」ために使う。
		/// scale と違って位置のずれや枠の太さには掛からないので、
		/// 縮めても枠の太さや文字の大きさは変わらない
		/// </remarks>
		Math::Vector2 sizeScale = { 1.0f, 1.0f };

		Math::Color tint = Engine::Color::WHITE;	// さらに掛ける色
		Math::Vector2 uvOffsetAdd = {};				// UVオフセットへ足す

		// UV倍率の差し替え : 立てると飾り側の UVScale を無視してこちらを使う
		//
		// 「1枚に並べた絵から1コマ切り出す」ような、コマの幅を出す側が知っている場合に使う
		// (ScoreHUD が桁数から 1/AtlasCount を出すなど)
		bool isUseUvScale = false;
		Math::Vector2 uvScale = { 1.0f, 1.0f };

		// 群の絞り込み : 立てるとこの群のデコレーションだけ描く
		bool isUseGroup = false;
		uint32_t group = 0;

		/// <summary>ディゾルブの進み具合(0 で何もしない・1 で全部消える)</summary>
		/// <remarks>
		/// UIマテリアルの効果がディゾルブのときだけ効く(マテリアルが無ければ何も起きない)。
		/// 出し入れの演出(UITransition)はここへ入れる
		/// </remarks>
		float dissolve = 0.0f;
	};

}
