#pragma once

namespace Engine::ECS
{
	class World;
}

namespace Engine::Resource
{
	class Model;
	struct BoneMatrix;
	struct NodePoseMatrix;
}

namespace Engine::Graphics::Raytracing
{
	struct DynamicRaytracingData;
}

namespace Engine::Graphics
{
	//==========================================================================================
	// スキニングモデルのGPU用描画命令
	//
	// 以下の項目は必須なため入力時に入れること
	// .pWorld
	// .modelHandle
	// .boneHandle
	// .nodeposeHandle
	// .rtAnimHandle
	//==========================================================================================
	struct GPUSkiningSubmitDesc
	{
		ECS::World* pWorld = nullptr;								// シーンのワールド

		Handle<Resource::Model> modelHandle;						// スキニング対象のモデル
		RangeHandle<Resource::BoneMatrix> boneHandle;				// ボーン行列配列ハンドル
		RangeHandle<Resource::NodePoseMatrix> nodeposeHandle;		// ノードポーズ行列配列ハンドル

		Handle<Raytracing::DynamicRaytracingData> rtAnimHandle;		// RT用のインスタンスハンドル
	};
}