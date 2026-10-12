// UI1枚ぶんの描画データ。
//
// 回転・アスペクト補正・ピボットはCPU側がピクセル空間で解決済みで、
// シェーダーは pos + axisX*q.x + axisY*q.y の線形変換をするだけでよい。
//
// ※ CPU 側 Engine::Graphics::UIData と並びを合わせること。
//    float2 が16バイト境界をまたぐ位置に来ると次の境界へ押し出されるので、
//    各行がちょうど float4 に収まる順序を崩さないこと
//    (row0: pos+axisX / row1: axisY+uvOffset / row2: color / row3: layer+texIndex+uvScale
//     / row4: 湾曲 / row5: clipRect / row6: postEffectFlags)
#ifndef ROOTPARAM_UI_DATA_HLSLI
#define ROOTPARAM_UI_DATA_HLSLI

// UIに掛けるポストプロセスの印(UIData::postEffectFlags のビット)
// ※ CPU 側 Engine::Graphics::UI_POST_EFFECT_* と値を合わせること
#define UI_POST_EFFECT_SCAN_LINE (1u << 0)	// ScanLinePass を掛ける

struct UIData
{
	float2 pos;			// クアッド中心(NDC)
	float2 axisX;		// クアッドx方向の基底(NDC)

	float2 axisY;		// クアッドy方向の基底(NDC)
	float2 uvOffset;

	float4 color;

	float layer;		// 重なり順。CPU側の並べ替え用で、VSでは読まない
	uint texIndex;		// SRVインデックス
	float2 uvScale;		// uv * uvScale + uvOffset。既定は(1,1)

	// 湾曲
	//
	// 「弧の中心から横へ dx 離れた点を、下へ k*dx^2 ずらす」だけの形にしてある。
	// 開き角・半径・弧の中心といった作り手が触る値はCPU側(Decoration::Resolve)で
	// この4つへ畳んである。
	//
	// こうしているのは、1つのUIが枠・中身・文字と複数のクアッドに分かれるため。
	// クアッドごとに自分の幅で曲げると、幅の違う中身と枠が別々の弧に乗ってしまう。
	// ずれをUIの共通ローカル(px)で測れば、どのクアッドも同じ1本の弧に乗る
	float curveK;				// 反りの強さ(1/px)。0で曲げない
	float curveOffsetX;			// 弧の中心からこのクアッドの中心までの横ずれ(px)
	float curveHalfWidth;		// このクアッドの半幅(px)
	float curveInvHalfHeight;	// このクアッドの半分の高さの逆数(1/px)

	// 切り抜き(NDC)。(minX, minY, maxX, maxY) の外のピクセルは PS で捨てる。
	// 切らないものには十分に広い矩形が入っている
	float4 clipRect;

	// 掛けるポストプロセスの印(UI_POST_EFFECT_*)。UIPS が効果ごとのマスクへ書き分ける
	uint postEffectFlags;
	float pad0;
	float pad1;
	float pad2;
};

#endif
