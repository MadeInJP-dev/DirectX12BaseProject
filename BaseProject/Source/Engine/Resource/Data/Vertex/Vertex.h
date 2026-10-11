#pragma once

namespace Engine::Resource
{

	// 頂点
	struct Vertex
	{
		Math::Vector3 pos = {};
		Math::Color color = Engine::Color::RED;
	};

	struct SimpleVertex
	{
		Math::Vector4 pos = { 0.0f,0.0f,0.0f,0.0f };
		Math::Vector2 uv = { 0.0f,0.0f };
	};

	//==========================================================
	// メッシュ用 頂点情報
	//==========================================================
	struct MeshVertex8bit
	{
		Math::Vector3		pos = { 0.0f,0.0f,0.0f };				// 座標
		Math::Vector2		uv = { 0.0f,0.0f };					// uv座標
		unsigned int			color = 0xFFFFFFFF;					// RGBA(各色0～255のUINT型)
		Math::Vector3		normal = { 0.5f,0.5f,0.5f };			// 法線
		Math::Vector3		tangent = { 1.0f,1.0f,1.0f };				// 接線

		std::array<short, 4>	skinIndexList = {};			// スキニングIndexリスト
		std::array<float, 4>	skinWeightList = {};			// スキニングウェイトリスト
	};
	struct MeshVertexFloat
	{
		Math::Vector3		pos = { 0.0f,0.0f,0.0f };					// 座標
		Math::Vector3		normal = { 0.5f,0.5f,0.5f };					// 法線
		Math::Vector2		uv = { 0.0f,0.0f };						// uv座標
		Math::Vector3		tangent = { 1.0f,1.0f,1.0f };				// 接線
		Math::Color			color = { 1.0f,1.0f,1.0f,1.0f };					// RGBA(各色0.0f～1.0fのFLOAT型)

		std::array<short, 4>	skinIndexList = {};			// スキニングIndexリスト
		std::array<float, 4>	skinWeightList = {};			// スキニングウェイトリスト
	};

	//==========================================================
	// メガバッファへ積む頂点(GPU 用に詰めた形)
	//
	// 読み込みや保存は MeshVertexFloat のまま行い、メガバッファへ積むときにこの形へ変換する。
	// シェーダー側の並びは Asset/Shader/Common/RootParameters/Vertex.hlsli と同じにすること。
	//
	// 法線・接線は 16 ビットの八面体符号化(snorm16 × 2)で 4 バイトに詰める。
	// 復号の誤差は角度で 0.01 度程度で、陰影には出ない
	//==========================================================

	// 静的な頂点 : 32 バイト。BLAS は先頭の位置をこの間隔で読む
	struct MeshVertexPacked
	{
		Math::Vector3	pos = {};			// 座標(BLAS が読むので先頭に置く)
		uint32_t		normal = 0;			// 法線(八面体符号化)
		uint32_t		tangent = 0;		// 接線(八面体符号化)
		Math::Vector2	uv = {};			// uv 座標
		uint32_t		color = 0;			// 色(RGBA8。R が下位バイト)

		static MeshVertexPacked From(const MeshVertexFloat& a_src);
	};

	// スキンメッシュだけが持つボーンの割り当て : 16 バイト。
	// 静的な頂点とは別に確保し、スキニングのときだけ読む
	struct MeshSkinVertex
	{
		std::array<uint16_t, 4> boneIndex = {};		// ボーン番号
		std::array<uint16_t, 4> boneWeight = {};	// 重み(unorm16 : 65535 で 1.0)

		static MeshSkinVertex From(const MeshVertexFloat& a_src);
	};

	// スキニング後の頂点 : 20 バイト。
	// 変わるのは座標・法線・接線だけなので、uv や色は静的な頂点から読む。
	// 動的 BLAS は先頭の位置をこの間隔で読む
	struct AnimatedVertex
	{
		Math::Vector3	pos = {};
		uint32_t		normal = 0;			// 八面体符号化
		uint32_t		tangent = 0;		// 八面体符号化
	};

	static_assert(sizeof(MeshVertexPacked) == 32, "Vertex.hlsli の MeshVertex と大きさを合わせること");
	static_assert(sizeof(MeshSkinVertex) == 16, "Vertex.hlsli の SkinVertex と大きさを合わせること");
	static_assert(sizeof(AnimatedVertex) == 20, "Vertex.hlsli の AnimatedVertex と大きさを合わせること");

	//==========================================================
	// レイトレ
	//==========================================================
	struct RTVertex
	{
		Math::Vector3 pos;
		Math::Vector3 normal;
		Math::Vector2 uv;
		Math::Vector3 tangent;
		Math::Color color;

		RTVertex& operator=(const MeshVertexFloat& a_vertFloat)
		{
			pos = a_vertFloat.pos;
			normal = a_vertFloat.normal;
			uv = a_vertFloat.uv;
			tangent = a_vertFloat.tangent;
			color = a_vertFloat.color;

			return *this;
		}
	};
}