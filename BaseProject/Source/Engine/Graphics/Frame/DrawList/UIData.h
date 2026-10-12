#pragma once
namespace Engine::Graphics
{
	// UIに掛けるポストプロセスの印(UIData::postEffectFlags のビット)。
	// UIPass がこれを見て、効果ごとのマスクへ書き分ける
	// ※ HLSL 側(UIData.hlsli)と値を合わせること
	inline constexpr UINT UI_POST_EFFECT_SCAN_LINE = 1u << 0;	// ScanLinePass を掛ける

	// UIデータ
	// StructuredBuffer<UIData> と1バイトもズレないよう、16バイト(float4)境界を意識して並べる。
	// HLSLの構造化バッファは float2 が16バイト境界をまたぐ位置に来ると次の境界へ押し出される。
	// 各行がちょうど float4 に収まる順序にしておけばパディングのズレが起きない。
	// (row0: pos+axisX / row1: axisY+uvOffset / row2: color / row3: layer+texIndex+uvScale
	//  / row4: 湾曲 / row5: clipRect / row6: postEffectFlags)
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

		// 湾曲
		//
		// 「弧の中心から横へ dx 離れた点を、下へ k*dx^2 ずらす」だけの形にしてある。
		// 開き角・半径・弧の中心といった作り手が触る値は、CPU側(Decoration::Resolve)で
		// この4つへ畳んである。
		//
		// こうしているのは、1つのUIが枠・中身・文字と複数のクアッドに分かれるため。
		// クアッドごとに自分の幅で曲げると、幅の違う中身(ゲージの残量など)と枠が
		// 別々の弧に乗ってしまう。ずれをUIの共通ローカル(px)で測れば1本の弧に乗る
		float curveK = 0.0f;				// 反りの強さ(1/px)。0で曲げない
		float curveOffsetX = 0.0f;			// 弧の中心からこのクアッドの中心までの横ずれ(px)
		float curveHalfWidth = 0.0f;		// このクアッドの半幅(px)
		float curveInvHalfHeight = 0.0f;	// このクアッドの半分の高さの逆数(1/px)

		// 切り抜き(NDC)。(minX, minY, maxX, maxY) の外へ出たピクセルは UIPS が捨てる。
		// スクロールする一覧のように、決まった範囲の外へはみ出した項目を切るために使う。
		// 既定は何も切らないよう十分に広くしてある。row5 にまるごと入れているので境界はずれない
		Math::Vector4 clipRect = { -1.0e30f, -1.0e30f, 1.0e30f, 1.0e30f };

		// 掛けるポストプロセスの印(UI_POST_EFFECT_*)。
		// 立っているぶんだけ UIPass がマスクへ書き、そのマスクを見たパスがこのUIにだけ効果を掛ける。
		// row6 を丸ごと使うので、余りは詰め物
		UINT postEffectFlags = 0;
		float pad0 = 0.0f;
		float pad1 = 0.0f;
		float pad2 = 0.0f;

		// 曲げるかどうか。
		// 曲げるものは横に分割した板ポリで描く必要があるので、
		// 描く側(RenderContext::DrawUI)がこれを見て使う板ポリを選ぶ。
		//
		// 頂点シェーダー(UIVS)側の分岐より必ず広く拾うこと。
		// こちらが取りこぼすと、曲げるつもりのUIが4頂点の板で来て曲がらない
		// (逆に多めに拾うぶんには、分割板でまっすぐ描かれるだけで害はない)
		bool IsCurved() const { return curveK != 0.0f; }
	};

	// UIの切り抜き矩形 : 座標はピクセル(左上原点/Y下向き)。
	// SubmitUI へ渡すと、この外へ出たぶんを描かない(UIData::clipRect へ直して積まれる)
	struct UIClipRect
	{
		Math::Vector2 min = {};		// 左上(px)
		Math::Vector2 max = {};		// 右下(px)
	};
}
