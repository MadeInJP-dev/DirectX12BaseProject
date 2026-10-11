// メガバッファへ積む頂点(GPU 用に詰めた形)と、その復号。
// C++ の Engine::Resource::MeshVertexPacked / MeshSkinVertex / AnimatedVertex(Vertex.h)と同じ並びなので、
// 変えるときは両方を同時に直すこと。
//
// メッシュシェーダー・レイトレ・スキニングは、詰めた形のままバッファから読み、
// ここの関数で Vertex(復号した形)へ戻して使う
#ifndef ROOTPARAM_VERTEX_HLSLI
#define ROOTPARAM_VERTEX_HLSLI

// 静的な頂点 : 32 バイト。BLAS は先頭の座標をこの間隔で読む
struct MeshVertex
{
	float3 pos;
	uint normal;		// 八面体符号化(snorm16 × 2)
	uint tangent;		// 八面体符号化(snorm16 × 2)
	float2 uv;
	uint color;			// RGBA8(R が下位バイト)
};

// スキンメッシュだけが持つボーンの割り当て : 16 バイト
struct SkinVertex
{
	uint2 boneIndex;	// 16 ビット × 4(下位から順に 0〜3 番目)
	uint2 boneWeight;	// unorm16 × 4(65535 で 1.0)
};

// スキニング後の頂点 : 20 バイト。変わらない uv と色は静的な頂点から読む
struct AnimatedVertex
{
	float3 pos;
	uint normal;		// 八面体符号化
	uint tangent;		// 八面体符号化
};

// 復号した頂点(シェーダーの中で使う形)
struct Vertex
{
	float3 pos;
	float3 normal;
	float3 tangent;
	float2 uv;
};

//------------------------------------------------------------------------------------------
// 八面体符号化
//
// 単位ベクトルを八面体へ投影し、下半分を折り返して正方形へ開いたものを snorm16 × 2 で持つ。
// C++ 側の符号化(Vertex.cpp の EncodeOctNormal)と同じ手順
//------------------------------------------------------------------------------------------
float3 DecodeOctNormal(uint a_packed)
{
	// 下位・上位 16 ビットを符号付きで取り出す
	float2 _e = float2(asint(uint2(a_packed << 16, a_packed)) >> 16) / 32767.0f;
	_e = max(_e, -1.0f);

	float3 _n = float3(_e, 1.0f - abs(_e.x) - abs(_e.y));

	// 下半分(折り返したところ)を元へ戻す
	float _fold = saturate(-_n.z);
	_n.x += (_n.x >= 0.0f) ? -_fold : _fold;
	_n.y += (_n.y >= 0.0f) ? -_fold : _fold;
	return normalize(_n);
}

uint EncodeOctNormal(float3 a_vector)
{
	float _l1 = abs(a_vector.x) + abs(a_vector.y) + abs(a_vector.z);

	// 長さが無いものは向きを決められないので +Z にしておく
	if (_l1 <= 1e-20f) return 0;

	float2 _e = a_vector.xy / _l1;
	if (a_vector.z < 0.0f)
	{
		float2 _folded = 1.0f - abs(_e.yx);
		_e.x = _folded.x * ((_e.x >= 0.0f) ? 1.0f : -1.0f);
		_e.y = _folded.y * ((_e.y >= 0.0f) ? 1.0f : -1.0f);
	}

	int2 _i = int2(round(clamp(_e, -1.0f, 1.0f) * 32767.0f));
	return (asuint(_i.x) & 0xFFFFu) | (asuint(_i.y) << 16);
}

float4 DecodeColorRGBA8(uint a_packed)
{
	return float4(
		a_packed & 0xFFu,
		(a_packed >> 8) & 0xFFu,
		(a_packed >> 16) & 0xFFu,
		a_packed >> 24) / 255.0f;
}

//------------------------------------------------------------------------------------------
// 復号
//------------------------------------------------------------------------------------------
Vertex DecodeMeshVertex(MeshVertex a_src)
{
	Vertex _v;
	_v.pos = a_src.pos;
	_v.normal = DecodeOctNormal(a_src.normal);
	_v.tangent = DecodeOctNormal(a_src.tangent);
	_v.uv = a_src.uv;
	return _v;
}

// スキニング後の座標・法線・接線に、静的な頂点の uv を合わせる
Vertex DecodeAnimatedVertex(AnimatedVertex a_animated, MeshVertex a_static)
{
	Vertex _v;
	_v.pos = a_animated.pos;
	_v.normal = DecodeOctNormal(a_animated.normal);
	_v.tangent = DecodeOctNormal(a_animated.tangent);
	_v.uv = a_static.uv;
	return _v;
}

uint DecodeBoneIndex(SkinVertex a_skin, uint a_slot)
{
	uint _pair = (a_slot < 2) ? a_skin.boneIndex.x : a_skin.boneIndex.y;
	return ((a_slot & 1u) == 0) ? (_pair & 0xFFFFu) : (_pair >> 16);
}

float DecodeBoneWeight(SkinVertex a_skin, uint a_slot)
{
	uint _pair = (a_slot < 2) ? a_skin.boneWeight.x : a_skin.boneWeight.y;
	uint _value = ((a_slot & 1u) == 0) ? (_pair & 0xFFFFu) : (_pair >> 16);
	return _value / 65535.0f;
}

#endif
