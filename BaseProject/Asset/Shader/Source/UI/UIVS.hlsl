#include "UI.hlsli"

[RootSignature(UI_RS)]
VSOutput VSMain(VSInput a_input)
{
	// UIデータを取得
	UIData _uiData = g_uiDataBuffer[a_input.instID];

	// 回転・アスペクト補正・ピボットはCPU(SubmitUI)側でピクセル空間で計算し、
	// クアッド頂点(-1..1)を線形変換する基底(axisX/axisY)とNDC中心(pos)として渡している。
	// ここでは基底に沿ってクアッド頂点を配置するだけ。
	float2 _q = a_input.pos.xy;	// -1..1

	//------------------------------------------------------------------
	// 湾曲 : UIを1つの円に巻き付ける
	//
	// 板ポリは横に分割してある(GraphicsEngine::CURVE_DIVISION)ので、
	// 頂点を円の上へ移すと本当に曲がる。縦の辺は円の中心へ向かう線になるだけで
	// まっすぐなままなので、縦は分割しなくてよい。
	//
	// 弧の頂点から横へ dx・下へ dy 離れた点を、角度 θ = dx * c・半径 r = 1/c - dy の位置へ移す
	// (c は曲率 = 1/半径。弧の長さが dx のままなので、曲げても文字や枠は伸び縮みしない)。
	//   x' = r * sin θ
	//   y' = 1/c - r * cos θ        (円の中心は頂点から 1/c 下。c が負なら上)
	// 1 - cos θ は 2 sin²(θ/2) で計算する(曲げが小さいときに桁落ちしないように)。
	//
	// dx, dy はUIの共通ローカル(px)で測る。枠・中身・文字と複数のクアッドに分かれても、
	// 全部が同じ1つの円に乗る(クアッドごとに自分の幅や UV で曲げると、別々の弧になってしまう)。
	// 開き角・深さ・弧の頂点はCPU側(Decoration::Resolve)で曲率とずれへ畳んである
	//------------------------------------------------------------------
	if (_uiData.curvature != 0.0f && _uiData.curveHalfWidth > 0.0f && _uiData.curveHalfHeight > 0.0f)
	{
		const float _c = _uiData.curvature;

		// 弧の頂点から見たこの頂点の位置(px, 右・下が正)。
		// クアッド座標は上が +1 なので、縦は向きを反転する
		const float _dx = _uiData.curveOffsetX + _q.x * _uiData.curveHalfWidth;
		const float _dy = _uiData.curveOffsetY - _q.y * _uiData.curveHalfHeight;

		float _sin = 0.0f;
		float _cos = 0.0f;
		sincos(_dx * _c, _sin, _cos);
		const float _halfSin = sin(_dx * _c * 0.5f);

		// 円の上へ移した位置(px, 弧の頂点から)
		const float _bentX = _sin / _c - _dy * _sin;
		const float _bentY = 2.0f * _halfSin * _halfSin / _c + _dy * _cos;

		// クアッド座標(半分の大きさが 1, 上が +1)へ戻す
		_q.x = (_bentX - _uiData.curveOffsetX) / _uiData.curveHalfWidth;
		_q.y = -(_bentY - _uiData.curveOffsetY) / _uiData.curveHalfHeight;
	}

	// ndc空間座標に変換
	float2 _ndc = _uiData.pos + _uiData.axisX * _q.x + _uiData.axisY * _q.y;
	

	// 構造体にしてPSへ
	//
	// Zは常に0。layer は「どの順で描くか」を決めるためのものでCPU側が並べ替えに使う。
	// ここへ入れてしまうと、深度を使っていないので重なりには効かないのに、
	// 0〜1 の外を指定したときだけクリップされて絵が消える
	VSOutput _out;
	_out.pos = float4(_ndc, 0.0f, 1);
	// 倍率 → オフセットの順。1枚に並べた絵から1コマだけ切り出すときは
	// 倍率でコマの大きさ、オフセットで何コマ目かを指定する
	_out.uv = a_input.uv * _uiData.uvScale + _uiData.uvOffset;
	_out.color = _uiData.color;
	_out.texIndex = _uiData.texIndex;	// 実際のSRV番号をPSへ渡す

	// 切り抜きはピクセルごとに見るので、位置と矩形をそのままPSへ渡す
	_out.ndc = _ndc;
	_out.clipRect = _uiData.clipRect;

	_out.postEffectFlags = _uiData.postEffectFlags;

	// 効果 : ノイズの座標はUIの原点からのずれ。
	// 枠・中身・文字のどのクアッドも同じ原点から測るので、1枚の絵として崩れる
	_out.noiseUV = (_ndc - _uiData.effectOriginNdc) * _uiData.noiseUVPerNdc;
	_out.effectType = _uiData.effectType;
	_out.noiseTexIndex = _uiData.noiseTexIndex;
	_out.effectProgress = _uiData.effectProgress;
	_out.edgeWidth = _uiData.edgeWidth;
	_out.edgeColor = _uiData.edgeColor;
	return _out;
}
