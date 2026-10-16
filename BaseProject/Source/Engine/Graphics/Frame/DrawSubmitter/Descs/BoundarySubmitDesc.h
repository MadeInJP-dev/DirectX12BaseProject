#pragma once

namespace Engine::Resource
{
	class Texture;
}

namespace Engine::Graphics
{
	//==========================================================================================
	// 境界面描画用命令構造体 : 面1枚ぶん
	//
	// モデルではなく板ポリ1枚を描く。板は原点を中心にした 1x1 の正方形(XY 平面・表は +Z)で、
	// worldMat で面の大きさ・向き・位置へ移す。立方体なら面の数だけ積むこと。
	//
	// 普段は見えず、基準点(プレイヤー)が近づいたところだけ浮かび上がる。
	// 濃さは「面の各点と基準点の距離」で決まるので、近づいた辺りだけが丸く見えてくる
	//
	// 以下の項目は必須なため入力時に入れること
	// .texHandle
	// .worldMat
	// .revealPos
	//==========================================================================================
	struct BoundarySubmitDesc
	{
		// 必須項目
		Handle<Resource::Texture> texHandle;	// 面に貼る画像
		Math::Matrix worldMat;					// 板(1x1)を面へ移す行列。拡縮に面の大きさを入れる
		Math::Vector3 revealPos = {};			// 近づき具合を測る基準点(プレイヤーの位置)

		// 見え方 : 基準点との距離が revealStartDistance より近いと見え始め、
		// revealEndDistance で一番濃くなる。revealStartDistance が revealEndDistance 以下なら常に見える
		float revealStartDistance = 30.0f;
		float revealEndDistance = 5.0f;

		// スタイル設定 : 必須ではないため初期値を入れる
		Math::Color color = Color::WHITE;		// 画像に掛ける色(rgb は 1 を超えてよい・a は一番濃いときの濃さ)
		Math::Vector2 uvTiling = { 1.0f, 1.0f };	// 面に画像を何枚並べるか。画像をそのまま引き伸ばさず、程よい大きさで繰り返す用
		Math::Vector2 uvScroll = {};			// UVのずらし(呼ぶ側が時間で進める)

		// 走査線・ノイズ・明滅(ScanLinePass と同じ考え方を面の上で掛ける)
		float time = 0.0f;						// ノイズと明滅を動かす経過時間(秒)
		float scanLineDensity = 8.0f;			// 画像1枚あたりの走査線の本数
		float opacityScanLine = 0.0f;			// 走査線の濃さ(0 で掛けない)
		float opacityNoise = 0.0f;				// ノイズの濃さ(0 で掛けない)
		float flickeringSpeed = 0.0f;			// 明滅の速さ
		float flickeringStrength = 0.0f;		// 明滅の強さ(0 で掛けない)
	};
}
