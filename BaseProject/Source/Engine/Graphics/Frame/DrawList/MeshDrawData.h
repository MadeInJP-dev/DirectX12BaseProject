#pragma once
namespace Engine::Graphics
{
	// ---- メッシュシェーダー用構造体 ----
	struct MeshInstanceData
	{
		Math::Matrix worldMat;			// 現在フレームのワールド行列

		Math::Matrix prevWorldMat;		// １フレーム前のワールド行列

		uint32_t materialOffset;			// メッシュが参照するマテリアル
		uint32_t meshletOffset;				// メッシュレットオフセット
		uint32_t vertexOffset;				// 頂点オフセット
		uint32_t uviOffset;					// ユニーク頂点インデックスオフセット

		uint32_t primitiveOffset;			// プリミティブオフセット
		uint32_t animatedVertexStart;		// アニメーション頂点オフセット
		uint32_t isAnimated;				// アニメーションするかどうか
		uint32_t cullStart;					// カリングバッファオフセット

		uint32_t meshletCount;				// メッシュレットカウント
		uint32_t lodStart;					// LOD の表(MeshLodData)の先頭。lodCount が 0 なら使わない
		uint32_t lodCount;					// LOD の段数(LOD0 を含まない)。0 なら LOD0 だけ
		float fade;							// フェード係数(1 = 見える / 0 = 見えない)。1 未満はディザーで画素を捨てる
	};

	//==========================================================================================
	// LOD の1段(サブセット単位)
	//
	// 増幅シェーダーが、パスのカメラから見た大きさで段を選び、
	// 選んだ段のメッシュレット・頂点・プリミティブの位置から描く。
	// LOD0 はインスタンスデータそのものなので、ここに並ぶのは LOD1 から。
	// 境界球はどの段にも同じものを入れる(段を選ぶ前に読むのは先頭だけ)
	//==========================================================================================
	struct MeshLodData
	{
		Math::Vector4 boundingSphere;		// モデル全体を囲む球(ワールド空間。xyz = 中心 / w = 半径)

		float screenSize;					// 画面の高さに対する直径の割合。これより小さく映るとこの段で描く
		uint32_t meshletOffset;				// この段のメッシュレットオフセット(メッシュレット数 0 ならこの段では描かない)
		uint32_t meshletCount;				// この段のメッシュレットカウント
		uint32_t cullStart;					// この段のカリングバッファオフセット

		uint32_t vertexOffset;				// この段の頂点オフセット
		uint32_t uviOffset;					// この段のユニーク頂点インデックスオフセット
		uint32_t primitiveOffset;			// この段のプリミティブオフセット
		uint32_t pad;
	};

	// LOD の表の容量(要素数)。超えたぶんのモデルは LOD0 だけで描く
	constexpr UINT MESH_LOD_DATA_CAPACITY = 100000;
	struct MeshMaterial
	{
		// マテリアルのテクスチャスケール値
		Math::Color baseColor;

		Math::Vector3 emissive;
		float metallic;

		float roughness;

		// マテリアルとは独立した自己発光(ModelComponent の 発光色 × 発光強度)。
		// emissive はエミッシブテクスチャに掛ける倍率なので、テクスチャを持たない
		// モデルは何倍しても光らない。こちらは加算なので単体で光らせられる。
		Math::Vector3 emissiveAdd;

		// テクスチャのSRVインデックス
		int albedoIndex;					// アルベド
		int metaRoughnessIndex;			// メタリックラフネステクスチャ
		int emissiveIndex;
		int normalIndex;
	};
}
