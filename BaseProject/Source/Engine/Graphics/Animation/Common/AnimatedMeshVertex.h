#pragma once
namespace Engine::Graphics::Animation
{
	struct SkinningMeshData
	{
		// コンピュートシェーダーで書き込む変形後のバッファ
		RangeHandle<Resource::AnimatedVertex> animatedVertexHandle = {};

		// どのメッシュの参照先か
		Handle<Resource::Mesh> meshHandle;
	};

	struct AnimatedMeshVertex
	{
		std::vector<Graphics::Animation::SkinningMeshData> meshDataVec = {};
	};
}