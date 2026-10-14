// UI1枚ぶんの描画データ。
//
// 回転・アスペクト補正・ピボットはCPU側がピクセル空間で解決済みで、
// シェーダーは pos + axisX*q.x + axisY*q.y の線形変換をするだけでよい。
//
// ※ CPU 側 Engine::Graphics::UIData と並びを合わせること。
//    float2 が16バイト境界をまたぐ位置に来ると次の境界へ押し出されるので、
//    各行がちょうど float4 に収まる順序を崩さないこと
//    (row0: pos+axisX / row1: axisY+uvOffset / row2: color / row3: layer+texIndex+uvScale
//     / row4: 湾曲 / row5: clipRect / row6: postEffectFlags+効果 / row7: ノイズの座標
//     / row8: 縁の色 / row9: 縁の幅+湾曲の半分の高さ)
#ifndef ROOTPARAM_UI_DATA_HLSLI
#define ROOTPARAM_UI_DATA_HLSLI

// UIに掛けるポストプロセスの印(UIData::postEffectFlags のビット)
// ※ CPU 側 Engine::Graphics::UI_POST_EFFECT_* と値を合わせること
#define UI_POST_EFFECT_SCAN_LINE (1u << 0)	// ScanLinePass を掛ける

// UIに掛ける効果(UIData::effectType)
// ※ CPU 側 Engine::Graphics::EUIEffect と値を合わせること
#define UI_EFFECT_NONE		0u	// 何もしない
#define UI_EFFECT_DISSOLVE	1u	// ノイズで虫食いに消す

// ノイズのテクスチャを持たないときの印(シェーダー内のノイズを使う)
// ※ CPU 側 Engine::Graphics::UI_NOISE_TEX_NONE と値を合わせること
#define UI_NOISE_TEX_NONE	0xFFFFFFFFu

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

	// 湾曲 : UIを1つの円に巻き付ける
	//
	// 弧の頂点(UIの基準線の上の1点)から横へ dx・下へ dy 離れた点を、
	//   角度 θ = dx / R(弧の長さが dx になる)
	//   半径 r = R - dy(基準線より下は内側、上は外側)
	// の位置へ移す。R は円の半径で、ここでは曲率 curvature = 1/R(符号付き)で持つ。
	// 正で山なり(円の中心が下)、負で谷(円の中心が上)。
	// 開き角・深さ・弧の頂点といった作り手が触る値はCPU側(Decoration::Resolve)で畳んである。
	//
	// ずれ(dx, dy)は UI の共通ローカル(px)で測る。1つのUIが枠・中身・文字と
	// 複数のクアッドに分かれても、どのクアッドも同じ1つの円に乗る
	// (クアッドごとに自分の幅や UV で曲げると、幅の違う中身と枠が別々の弧になってしまう)
	float curvature;			// 曲率(1/px, 符号付き)。0で曲げない
	float curveOffsetX;			// 弧の頂点からこのクアッドの中心までの横ずれ(px, 右が正)
	float curveOffsetY;			// 弧の頂点からこのクアッドの中心までの縦ずれ(px, 下が正)
	float curveHalfWidth;		// このクアッドの半幅(px)

	// 切り抜き(NDC)。(minX, minY, maxX, maxY) の外のピクセルは PS で捨てる。
	// 切らないものには十分に広い矩形が入っている
	float4 clipRect;

	// 掛けるポストプロセスの印(UI_POST_EFFECT_*)。UIPS が効果ごとのマスクへ書き分ける
	uint postEffectFlags;

	// 効果(UIマテリアル)。ノイズはUIの原点からのずれで引く(枠も文字も1枚の絵として崩れるように)
	uint effectType;		// UI_EFFECT_*
	uint noiseTexIndex;		// ノイズのSRVインデックス。UI_NOISE_TEX_NONE ならシェーダー内のノイズ
	float effectProgress;	// 進み具合(0 で何もしない・1 で全部消える)

	float2 effectOriginNdc;	// UIの原点(NDC)
	float2 noiseUVPerNdc;	// noiseUV = (ndc - effectOriginNdc) * noiseUVPerNdc

	float4 edgeColor;		// 縁の色(a は乗せる強さ)

	float edgeWidth;		// 縁の幅(ノイズの値の幅)
	float curveHalfHeight;	// 湾曲 : このクアッドの半分の高さ(px)
	float pad0;
	float pad1;
};

#endif
