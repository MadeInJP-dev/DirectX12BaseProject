#pragma once
namespace Engine::Graphics
{
	// UIに掛けるポストプロセスの印(UIData::postEffectFlags のビット)。
	// UIPass がこれを見て、効果ごとのマスクへ書き分ける
	// ※ HLSL 側(UIData.hlsli)と値を合わせること
	inline constexpr UINT UI_POST_EFFECT_SCAN_LINE = 1u << 0;	// ScanLinePass を掛ける

	// UIに掛ける効果(UIData::effectType)。UIPS がこれを見て色の作り方を変える。
	// 選ぶのは UI のマテリアル(Engine::Resource::UIMaterial)
	// ※ HLSL 側(UIData.hlsli の UI_EFFECT_*)と値を合わせること。値は保存されるので末尾へ足す
	enum class EUIEffect : uint32_t
	{
		None = 0,		// 何もしない(テクスチャ × 色)
		Dissolve = 1,	// ノイズで虫食いに消す。縁に色を乗せる
	};

	// ノイズのテクスチャを持たないときの印(UIData::noiseTexIndex)。シェーダー内で作ったノイズを使う
	// ※ HLSL 側(UI_NOISE_TEX_NONE)と値を合わせること
	inline constexpr UINT UI_NOISE_TEX_NONE = 0xFFFFFFFFu;

	// UIデータ
	// StructuredBuffer<UIData> と1バイトもズレないよう、16バイト(float4)境界を意識して並べる。
	// HLSLの構造化バッファは float2 が16バイト境界をまたぐ位置に来ると次の境界へ押し出される。
	// 各行がちょうど float4 に収まる順序にしておけばパディングのズレが起きない。
	// (row0: pos+axisX / row1: axisY+uvOffset / row2: color / row3: layer+texIndex+uvScale
	//  / row4: 湾曲 / row5: clipRect / row6: postEffectFlags+効果 / row7: ノイズの座標
	//  / row8: 縁の色 / row9: 縁の幅+湾曲の半分の高さ)
	//
	// 回転・アスペクト補正・ピボットはCPU(SubmitUI)側でピクセル空間で計算し、
	// クアッド頂点(-1..1)を線形変換する基底(axisX/axisY)とNDC中心座標(pos)として渡す。
	// シェーダーは pos + axisX*q.x + axisY*q.y を計算するだけでよい。
	struct UIData
	{
		Math::Vector2 pos;			// クアッド中心のNDC座標(平行移動成分)
		Math::Vector2 axisX;		// クアッドx方向の基底(NDC, 回転・アスペクト込み)

		Math::Vector2 axisY;		// クアッドy方向の基底(NDC, 回転・アスペクト込み)
		Math::Vector2 uvOffset;		// UVをずらす際のオフセット

		Math::Vector4 color;		// 色調補正

		// 重なり順 : 大きいほど手前。
		// UIパスは深度を持たないので、これを見てCPU側が積んだ順を並べ替える
		float layer;
		UINT texIndex;				// SRVインデックス

		// UVに掛ける倍率。1つのテクスチャに並べた絵を切り出すために使う
		// (数字の 0〜9 を横に並べたものから1文字だけ出す、など)。
		// uv * uvScale + uvOffset の順で効く。既定は等倍。
		// row3 の余りに入れているので、構造体の大きさは変わらない
		Math::Vector2 uvScale = { 1.0f, 1.0f };

		// 湾曲 : UIを1つの円に巻き付ける
		//
		// 弧の頂点から横へ dx・下へ dy 離れた点を、角度 dx/R・半径 R-dy の位置へ移す
		// (R は円の半径。弧の長さが dx のままなので、曲げても伸び縮みしない)。
		// 開き角・深さ・弧の頂点といった作り手が触る値は、CPU側(Decoration::Resolve)で
		// 曲率と頂点からのずれへ畳んである。
		//
		// こうしているのは、1つのUIが枠・中身・文字と複数のクアッドに分かれるため。
		// クアッドごとに自分の幅で曲げると、幅の違う中身(ゲージの残量など)と枠が
		// 別々の弧に乗ってしまう。ずれをUIの共通ローカル(px)で測れば1つの円に乗る
		float curvature = 0.0f;				// 曲率(1/px, 符号付き)。正で山なり・負で谷。0で曲げない
		float curveOffsetX = 0.0f;			// 弧の頂点からこのクアッドの中心までの横ずれ(px, 右が正)
		float curveOffsetY = 0.0f;			// 弧の頂点からこのクアッドの中心までの縦ずれ(px, 下が正)
		float curveHalfWidth = 0.0f;		// このクアッドの半幅(px)

		// 切り抜き(NDC)。(minX, minY, maxX, maxY) の外へ出たピクセルは UIPS が捨てる。
		// スクロールする一覧のように、決まった範囲の外へはみ出した項目を切るために使う。
		// 既定は何も切らないよう十分に広くしてある。row5 にまるごと入れているので境界はずれない
		Math::Vector4 clipRect = { -1.0e30f, -1.0e30f, 1.0e30f, 1.0e30f };

		// 掛けるポストプロセスの印(UI_POST_EFFECT_*)。
		// 立っているぶんだけ UIPass がマスクへ書き、そのマスクを見たパスがこのUIにだけ効果を掛ける
		UINT postEffectFlags = 0;

		//------------------------------------------------------------------
		// 効果(UIマテリアル)
		//
		// ノイズは「UIの原点からのずれ」で引く。1つのUIが枠・中身・文字と
		// 複数のクアッドに分かれても、同じ座標でノイズを引けば1枚の絵として崩れる
		// (クアッドごとのUVで引くと、枠と文字がばらばらに虫食いになる)。
		// 湾曲と同じ考え方で、ずれはUIの共通の座標で測る
		//------------------------------------------------------------------
		UINT effectType = 0;						// EUIEffect
		UINT noiseTexIndex = UI_NOISE_TEX_NONE;		// ノイズのSRVインデックス。無ければシェーダー内のノイズ
		float effectProgress = 0.0f;				// 進み具合(0 で何もしない・1 で全部消える)

		// row7 : ノイズの座標。noiseUV = (ndc - effectOriginNdc) * noiseUVPerNdc
		// NDC で渡すのは、画面の解像度を知らない VS でもそのまま計算できるようにするため
		Math::Vector2 effectOriginNdc = {};			// UIの原点(NDC)
		Math::Vector2 noiseUVPerNdc = {};			// NDC 1 あたりのノイズの UV(縦横の比と上下の向きを含む)

		// row8 : 縁の色(a は乗せる強さ)
		Math::Vector4 edgeColor = {};

		// row9 : 縁の幅(ノイズの値の幅)と、湾曲に使うこのクアッドの半分の高さ(px)。余りは詰め物
		float edgeWidth = 0.0f;
		float curveHalfHeight = 0.0f;
		float pad0 = 0.0f;
		float pad1 = 0.0f;

		// 曲げるかどうか。
		// 曲げるものは横に分割した板ポリで描く必要があるので、
		// 描く側(RenderContext::DrawUI)がこれを見て使う板ポリを選ぶ。
		//
		// 頂点シェーダー(UIVS)側の分岐より必ず広く拾うこと。
		// こちらが取りこぼすと、曲げるつもりのUIが4頂点の板で来て曲がらない
		// (逆に多めに拾うぶんには、分割板でまっすぐ描かれるだけで害はない)
		bool IsCurved() const { return curvature != 0.0f; }
	};
	static_assert(sizeof(UIData) == 16 * 10, "UIData は float4 の10行ぶん。HLSL 側と並びを合わせること");

	// UIの切り抜き矩形 : 座標はピクセル(左上原点/Y下向き)。
	// SubmitUI へ渡すと、この外へ出たぶんを描かない(UIData::clipRect へ直して積まれる)
	struct UIClipRect
	{
		Math::Vector2 min = {};		// 左上(px)
		Math::Vector2 max = {};		// 右下(px)
	};
}
