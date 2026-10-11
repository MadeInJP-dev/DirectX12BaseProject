// ルートパラメーターの構造体
#include "../../../Common/RootParameters/Vertex.hlsli"
#include "../../../Common/RootParameters/BonePalletData.hlsli"
#include "../../../Common/RootParameters/SkinningInfo.hlsli"

//==========================================================================================
// ルートパラメーター
//
//   0 : CBV(b0)         このディスパッチが担当する範囲
//   1 : SRVの番号(t0) ボーン行列
//   2 : SRVの番号(t1) 頂点メガバッファ
//   3 : SRVの番号(t2) インデックスメガバッファ
//   4 : UAVの番号(u0) 変形後頂点の書き込み先
//   5 : UAVの番号(u1) 前フレームの位置の書き込み先(モーションベクター用)
//   6 : SRVの番号(t3) ボーン割り当てメガバッファ
//==========================================================================================
#define SKINNING_ROOT_SIG \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
"CBV(b0)," \
"RootConstants(num32BitConstants=1, b100), " \
"RootConstants(num32BitConstants=1, b101), " \
"RootConstants(num32BitConstants=1, b102), " \
"RootConstants(num32BitConstants=1, b103), " \
"RootConstants(num32BitConstants=1, b104), " \
"RootConstants(num32BitConstants=1, b105)"

cbuffer CBSkinningInfo : register(b0)
{
	SkinningInfo g_info;
}

// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex0 : register(b100)
{
	uint g_bonePalletDataIndex;
}

StructuredBuffer<BonePallet> Get_bonePalletData() { StructuredBuffer<BonePallet> _r = ResourceDescriptorHeap[g_bonePalletDataIndex]; return _r; }
#define g_bonePalletData Get_bonePalletData()
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex1 : register(b101)
{
	uint g_vertexfloatDataIndex;
}

StructuredBuffer<MeshVertex> Get_vertexfloatData() { StructuredBuffer<MeshVertex> _r = ResourceDescriptorHeap[g_vertexfloatDataIndex]; return _r; }
#define g_vertexfloatData Get_vertexfloatData()
// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex2 : register(b102)
{
	uint g_indexDataIndex;
}

StructuredBuffer<uint> Get_indexData() { StructuredBuffer<uint> _r = ResourceDescriptorHeap[g_indexDataIndex]; return _r; }
#define g_indexData Get_indexData()

// UAVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex3 : register(b103)
{
	uint g_outputVertexIndex;
}

RWStructuredBuffer<AnimatedVertex> Get_outputVertex() { RWStructuredBuffer<AnimatedVertex> _r = ResourceDescriptorHeap[g_outputVertexIndex]; return _r; }
#define g_outputVertex Get_outputVertex()

// UAVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex4 : register(b104)
{
	uint g_outputPrevPositionIndex;
}

RWStructuredBuffer<float3> Get_outputPrevPosition() { RWStructuredBuffer<float3> _r = ResourceDescriptorHeap[g_outputPrevPositionIndex]; return _r; }
#define g_outputPrevPosition Get_outputPrevPosition()

// SRVの番号(ResourceDescriptorHeap の添字)。ルート定数で届く
cbuffer PassDescriptorIndex5 : register(b105)
{
	uint g_skinVertexDataIndex;
}

StructuredBuffer<SkinVertex> Get_skinVertexData() { StructuredBuffer<SkinVertex> _r = ResourceDescriptorHeap[g_skinVertexDataIndex]; return _r; }
#define g_skinVertexData Get_skinVertexData()
