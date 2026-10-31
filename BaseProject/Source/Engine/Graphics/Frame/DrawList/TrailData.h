#pragma once
namespace Engine::Graphics
{
	//==========================================================================================
	// トレイルの頂点(構造体バッファ t0)
	//
	// 帯は点ごとに左右2頂点を並べた三角形ストリップ。CPU がカメラへ向けて組んで上げる。
	// TransparentPass の TrailVS が SV_VertexID で引く(頂点バッファは張らない)。
	// ※ HLSL 側(Asset/Shader/Source/Geometry/Trail/Trail.hlsli の TrailVertex)と並びを合わせること。
	//    構造体バッファは詰めて並ぶので、パディングを足さないこと
	//==========================================================================================
	struct TrailVertex
	{
		Math::Vector3 pos;		// ワールド座標
		Math::Vector2 uv;		// U = 先頭 0 → 尾 1、V = 帯の端 0 → 端 1
		Math::Vector4 color;	// 強さ込みの色(a は濃さ)
	};
	static_assert(sizeof(TrailVertex) == 36, "TrailVertex は HLSL 側と同じ 36 バイトにすること");

	//==========================================================================================
	// トレイル1本ぶんの描画
	//
	// 頂点は全トレイルで1本の配列(DrawLists::GetTrailVertexVec)に続けて入っていて、
	// ここはその中の範囲と、描くときに要る値だけを持つ
	//==========================================================================================
	struct TrailDrawItem
	{
		UINT vertexStart = 0;		// 頂点配列の何番目からか
		UINT vertexCount = 0;		// 頂点の数(点の数 × 2)
		UINT textureIndex = 0xFFFFFFFFu;	// 帯に貼る絵の SRV 番号。無ければ 0xFFFFFFFF
		UINT isAdditive = 0;		// 1 なら加算で重ねる

		// 並べ替え用 : 帯の真ん中あたりの位置と、カメラからの距離の2乗(DrawLists::SortTransparentItems が入れる)
		Math::Vector3 sortPos = {};
		float sortDistSq = 0.0f;
	};
}
