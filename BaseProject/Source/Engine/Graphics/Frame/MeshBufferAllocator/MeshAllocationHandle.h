#pragma once
namespace Engine::Graphics
{
	/// <summary>
	/// メッシュシェーダーで描画されるものすべてが持つ
	/// </summary>
	struct MeshAllocationHandle
	{
		RangeHandle<Resource::MeshVertexPacked>	vertexHandle;				// 頂点開始位置
		RangeHandle<Resource::Meshlet>			meshletHandle;				// メッシュレット開始位置
		RangeHandle<uint32_t>					primitiveHandle;			// プリミティブ開始位置
		RangeHandle<uint32_t>					uniqueVertexHandle;		// 頂点インデックス開始位置
		bool     isValid = false;			// 使用中か否か
	};
}