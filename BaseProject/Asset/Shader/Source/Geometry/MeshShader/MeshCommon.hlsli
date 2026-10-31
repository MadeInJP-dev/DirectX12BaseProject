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
//   12 : SRV(t10)           LOD の表
//   13 : RootConstants(b2)  前方ライティング(ライト配列の番号・数・環境光)。
//                           半透明パス(MeshTransparentPS)だけが張り、他のパスは読まない
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
    "SRV(t10)," \
    "RootConstants(num32BitConstants=8, b2)," \
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
StructuredBuffer<MeshLodData>		g_meshLodData			: register(t10);	// LOD の表

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

	// 増幅シェーダーが選んだ LOD の段の、メガバッファ上の位置。
	// メッシュシェーダーはインスタンスデータではなくこちらから頂点とプリミティブを引く
	uint vertexOffset;
	uint uviOffset;
	uint primitiveOffset;
};

// ==========================================================
// LOD の選択
// ----------------------------------------------------------
// 描く段のメッシュレット・頂点・プリミティブの位置
// ==========================================================
struct MeshLodRange
{
	uint meshletOffset;
	uint meshletCount;
	uint cullStart;
	uint vertexOffset;
	uint uviOffset;
	uint primitiveOffset;
};

// インスタンスをどの段で描くかを、いま張られているカメラ(パスのカメラ)で決める。
//
// 大きさは「画面の高さに対する、モデルの直径の割合」で測る。
//   直径(NDC) = 2 × 半径 × proj._22 / w  →  割合 = 半径 × proj._22 / w
// 透視投影なら w は奥行き、正射影(影のカスケード)なら w = 1 で proj._22 = 1 / 箱の半分の高さ
// になるので、同じ式でカスケードの箱に対する大きさになる。
// 段の screenSize より小さく映るほど粗い段へ進む(screenSize は細かい段ほど大きい)。
//
// ZPre と GBuffer は同じカメラ・同じ式で選ぶので、段が食い違って深度の EQUAL が崩れることはない。
// アニメーションするメッシュには段が積まれない(lodCount = 0)
MeshLodRange SelectMeshLod(MeshInstanceData a_inst)
{
	MeshLodRange _range;
	_range.meshletOffset = a_inst.meshletOffset;
	_range.meshletCount = a_inst.meshletCount;
	_range.cullStart = a_inst.cullStart;
	_range.vertexOffset = a_inst.vertexOffset;
	_range.uviOffset = a_inst.uviOffset;
	_range.primitiveOffset = a_inst.primitiveOffset;

	if (a_inst.lodCount == 0) return _range;

	const float4 _sphere = g_meshLodData[a_inst.lodStart].boundingSphere;
	const float4 _clipCenter = mul(float4(_sphere.xyz, 1.0f), g_camera.viewProj);
	const float _screenSize = _sphere.w * abs(g_camera.proj._22) / max(abs(_clipCenter.w), 1e-4f);

	for (uint _k = 0; _k < a_inst.lodCount; ++_k)
	{
		const MeshLodData _level = g_meshLodData[a_inst.lodStart + _k];
		if (_screenSize >= _level.screenSize) break;

		_range.meshletOffset = _level.meshletOffset;
		_range.meshletCount = _level.meshletCount;
		_range.cullStart = _level.cullStart;
		_range.vertexOffset = _level.vertexOffset;
		_range.uviOffset = _level.uviOffset;
		_range.primitiveOffset = _level.primitiveOffset;
	}
	return _range;
}

// ==========================================================
// 座標の変換 : ローカル → ワールド → クリップ
// ----------------------------------------------------------
// 深度だけを描くパス(DepthOnlyMS)と、色を描くパス(UberMS)の両方がここを通す。
// GBuffer は ZPre の深度と EQUAL で比べるので、2つのシェーダーで深度が1ビットでも
// 違うと面が抜ける。precise を付けて、コンパイラが掛け算と足し算をまとめたり
// (FMA)並べ替えたりしないようにし、どちらのシェーダーでも同じ計算順にそろえる
// ==========================================================
float4 TransformMeshPosition(float3 a_localPos, float4x4 a_worldMat, out float3 a_outWorldPos)
{
	precise float4 _worldPos = mul(float4(a_localPos, 1.0f), a_worldMat);
	precise float4 _clipPos = mul(_worldPos, g_camera.viewProj);

	a_outWorldPos = _worldPos.xyz;
	return _clipPos;
}
