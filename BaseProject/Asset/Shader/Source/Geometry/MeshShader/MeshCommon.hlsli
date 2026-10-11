// ヘルパー関数
#include "../../../Common/Math/Normal.hlsli"

// ルートパラメーターの構造体
#include "../../../Common/RootParameters/CameraData.hlsli"
#include "../../../Common/RootParameters/MeshShaderData.hlsli"
#include "../../../Common/RootParameters/Vertex.hlsli"
#include "../../../Common/RootParameters/BonePalletData.hlsli"

//==========================================================================================
// ルートパラメーター : 全メッシュシェーダー共通
//
//    0 : CBV(b0)            カメラ
//    1 : SRV(t0)            インスタンス
//    2 : SRV(t1)            マテリアル
//    3 : SRV(t2)            メッシュレット
//    4 : SRV(t3)            ユニーク頂点インデックス
//    5 : SRV(t4)            プリミティブインデックス
//    6 : SRV(t5)            頂点
//    7 : SRV(t6)            スキニング済み頂点
//    8 : SRV(t7)            メッシュレットのカリングデータ
//    9 : RootConstants(b1)  描く順のインスタンス番号の表を引く土台
//   10 : SRV(t8)            前フレームのスキニング済み頂点(モーションベクター用)
//   11 : SRV(t9)            描く順のインスタンス番号の表(インスタンシング用)
//
// ※ 追加は必ず末尾へ。C++側は添字でバインドしているので
//    (RenderContext::BindMeshInstance / BindMeshlet、DrawQueueDispathMesh の 9 番など)、
//    間に挟むと既存のバインドが全部ずれる
//==========================================================================================
#define MESHGLOBAL_ROOT_SIG \
    "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
    "CBV(b0)," \
    "SRV(t0)," \
    "SRV(t1)," \
    "SRV(t2)," \
    "SRV(t3)," \
    "SRV(t4)," \
    "SRV(t5)," \
    "SRV(t6)," \
    "SRV(t7)," \
    "RootConstants(num32BitConstants=1, b1)," \
    "SRV(t8)," \
    "SRV(t9)," \
    "StaticSampler(s0, " \
    "    filter = FILTER_MIN_MAG_MIP_LINEAR, " \
    "    addressU = TEXTURE_ADDRESS_WRAP, " \
    "    addressV = TEXTURE_ADDRESS_WRAP, " \
    "    addressW = TEXTURE_ADDRESS_WRAP)"

cbuffer CBCamera : register(b0)
{
	CameraData g_camera;
}

// 描く順のインスタンス番号の表(g_drawInstanceIndices)を引く土台。
// インスタンスデータの番号そのものではない点に注意 : 1回のディスパッチで
// 同じメッシュを並べて描くので、増幅シェーダーは「土台 + SV_GroupID.y」で表を引いて
// そのインスタンスのデータ番号を得る
cbuffer RootConstants : register(b1)
{
	uint g_baseInstanceIndex;
};

StructuredBuffer<MeshInstanceData>	g_instanceData			: register(t0);
StructuredBuffer<MeshMaterial>		g_materialData			: register(t1);

StructuredBuffer<Meshlet>			g_meshletData			: register(t2);
StructuredBuffer<uint>				g_uniqueVertexIndices	: register(t3);	// 各メッシュレットが使う頂点番号
StructuredBuffer<uint>				g_primitiveIndices		: register(t4);

StructuredBuffer<MeshVertex>		g_vertices				: register(t5);	// 静的な頂点(詰めた形)
StructuredBuffer<AnimatedVertex>	g_animatedVertices		: register(t6);	// スキニング後の座標・法線・接線
StructuredBuffer<MeshletCullData>	g_cullData				: register(t7);
StructuredBuffer<float3>			g_prevAnimatedPositions	: register(t8);	// 前フレームのスキニング済みの位置
StructuredBuffer<uint>				g_drawInstanceIndices	: register(t9);	// 描く順 → インスタンスデータの番号

SamplerState smp : register(s0);

// ==========================================================
// G-Bufferパスのピクセルシェーダーへ渡す出力構造体
// ==========================================================
struct VertexOutput
{
	float4 pos : SV_Position;
	float3 worldPos : POSITION;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float2 uv : TEXCOORD;

    // モーションベクター（Velocity）用
	float4 curClipPos : POSITION1;
	float4 prevClipPos : POSITION2;

	uint instanceID : INSTANCE_ID;
};

// ==========================================================
// ASからMSにわたるペイロード
// ==========================================================
struct PayloadStruct
{
	// グループ内で生き残ったメッシュレットの数
	uint SurvivingMeshlets;

    // 生き残ったメッシュレットのID。ASのスレッドグループサイズと合わせること
	uint MeshletIndices[32];

	uint instanceID;
};
