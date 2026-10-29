// メッシュシェーダーパスのバッファ要素。
// ラスタライザパスの InstanceData / SubsetData とは別物なので、Mesh 接頭辞で分けている。
// ※ CPU 側 Engine::Graphics::MeshInstanceData / MeshMaterial と並びを合わせること
#ifndef ROOTPARAM_MESH_SHADER_DATA_HLSLI
#define ROOTPARAM_MESH_SHADER_DATA_HLSLI

// サブメッシュ単位。参照するマテリアルは1つ
struct MeshInstanceData
{
	float4x4 worldMat;
	float4x4 prevWorldMat;

	uint materialOffset;

	// メガバッファから自分のぶんを切り出すためのオフセット
	uint meshletOffset;
	uint vertexOffset;
	uint uviOffset;
	uint primitiveOffset;

	uint animatedVertexStart;
	uint isAnimated;

	uint cullStart;
	uint meshletCount;		// 範囲外ディスパッチを弾くための総数

	uint lodStart;			// LOD の表(MeshLodData)の先頭。lodCount が 0 なら使わない
	uint lodCount;			// LOD の段数(LOD0 を含まない)。0 なら LOD0 だけ
	float fade;				// フェード係数(1 = 見える / 0 = 見えない)。1 未満はディザーで画素を捨てる(DitherFade.hlsli)
};

// LOD の1段(サブセット単位)。LOD0 はインスタンスデータそのものなので、並ぶのは LOD1 から。
// 境界球はどの段にも同じものが入っている(段を選ぶ前に読むのは先頭だけ)
struct MeshLodData
{
	float4 boundingSphere;	// モデル全体を囲む球(ワールド空間。xyz = 中心 / w = 半径)

	float screenSize;		// 画面の高さに対する直径の割合。これより小さく映るとこの段で描く
	uint meshletOffset;		// メッシュレット数 0 ならこの段では描かない
	uint meshletCount;
	uint cullStart;

	uint vertexOffset;
	uint uviOffset;
	uint primitiveOffset;
	uint pad;
};

struct MeshMaterial
{
	float4 baseColor;

	float3 emissive;
	float metallic;

	float roughness;

	// マテリアルから独立した自己発光(加算)。
	// emissive はエミッシブテクスチャに掛ける倍率なので、
	// テクスチャを持たないモデルは何倍しても光らない
	float3 emissiveAdd;

	// テクスチャのSRVインデックス(ディスクリプタヒープ直接参照)
	int albedoIndex;
	int metaRoughnessIndex;
	int emissiveIndex;
	int normalIndex;
};

struct Meshlet
{
	uint vertexCount;		// 最大64
	uint vertexOffset;		// 頂点インデックス配列内の開始位置
	uint primitiveCount;	// 最大126
	uint primitiveOffset;	// プリミティブ配列内の開始位置
};

// ASでのメッシュレット単位カリング用
struct MeshletCullData
{
	float3 BoundingSphereCenter;
	float BoundingSphereRadius;
	uint NormalCone;		// 8ビット×4のパック
	float ApexOffset;
};

#endif
