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
	// 湾曲
	//
	// 板ポリは横に分割してある(GraphicsEngine::kCurveDivision)ので、
	// ここで頂点を持ち上げ下げすると本当に曲がる。
	//
	// 弧の中心から横へ dx 離れた点を、下へ k*dx^2 ずらすだけ(円弧の二次近似)。
	// 横は一切動かさないので、曲げてもUIの幅は変わらない。
	//
	// dx はUIの共通ローカル(px)で測る。枠・中身・文字と複数のクアッドに分かれても、
	// 全部が同じ1本の弧に乗るようにするため(クアッドごとに自分の幅で曲げると、
	// 幅の違う中身と枠が別々の曲がり方になってしまう)。
	//
	// 開き角・半径・弧の中心はCPU側(Decoration::Resolve)で k と横ずれへ畳んである
	//------------------------------------------------------------------
	if (_uiData.curveK != 0.0f)
	{
		// この頂点が弧の中心からどれだけ横にずれているか(px)
		float _dx = _uiData.curveOffsetX + _q.x * _uiData.curveHalfWidth;

		// 反り(px)。正のkで中央が上に膨らむ(山なり)
		float _bowPixel = -_uiData.curveK * _dx * _dx;

		// クアッド座標(半分の高さが1)へ直して足す
		_q.y += _bowPixel * _uiData.curveInvHalfHeight;
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
