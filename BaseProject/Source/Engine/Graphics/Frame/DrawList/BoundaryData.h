#pragma once
namespace Engine::Graphics
{
	// 境界面1枚ぶんの描画データ(定数バッファ b1)
	//
	// BoundaryPass が面ごとに定数バッファへ詰めて、板ポリ1枚(6頂点)を描く。
	// 面の数は多くても立方体の6枚ほどなので、インスタンシングせず1枚ずつ描いている。
	//
	// float4 の行に収まるように並べる。
	// (row0〜3: worldMat / row4: color / row5: 基準点+見え始め / row6: 並べる数+ずらし
	//  / row7: 一番濃い距離+テクスチャ+時間+走査線の本数 / row8: 走査線・ノイズ・明滅の濃さ)
	// ※ HLSL 側(Common/RootParameters/BoundaryData.hlsli)と並びを合わせること
	struct BoundaryDrawData
	{
		// 板(1x1, XY 平面)をワールドへ移す行列。
		// HLSL 側は row_major で受けるので転置しないで入れる
		Math::Matrix worldMat;

		Math::Vector4 color;				// 画像に掛ける色(a は一番濃いときの濃さ)

		Math::Vector3 revealPos;			// 近づき具合を測る基準点
		float revealStartDistance = 0.0f;	// この距離より近いと見え始める

		Math::Vector2 uvTiling;				// 面に画像を何枚並べるか
		Math::Vector2 uvScroll;				// UVのずらし

		float revealEndDistance = 0.0f;		// この距離で一番濃くなる
		UINT texIndex = 0;					// 画像のSRVインデックス
		float time = 0.0f;					// ノイズと明滅を動かす経過時間(秒)
		float scanLineDensity = 0.0f;		// 画像1枚あたりの走査線の本数

		float opacityScanLine = 0.0f;		// 走査線の濃さ(0 で掛けない)
		float opacityNoise = 0.0f;			// ノイズの濃さ(0 で掛けない)
		float flickeringSpeed = 0.0f;		// 明滅の速さ
		float flickeringStrength = 0.0f;	// 明滅の強さ(0 で掛けない)
	};
	static_assert(sizeof(BoundaryDrawData) == 16 * 9, "BoundaryDrawData は float4 の9行ぶん。HLSL 側と並びを合わせること");
}
