#pragma once
#include "Engine/Graphics/Raytracing/BLAS/BLAS.h"

namespace Engine::Graphics::Raytracing
{
	// ===================================================================================
	// アニメーションするモデルのレイトレ用データ
	//
	// スキニング後の頂点でメッシュごとに BLAS を作り直すので、
	// 静的なモデルと違ってインスタンスごとに BLAS と変形後のバッファを持つ。
	// ===================================================================================

	// １メッシュにつき一つ
	struct SkinningMeshData
	{
		// コンピュートシェーダーで書き込む変形後のバッファ
		RangeHandle<Resource::AnimatedVertex> animatedVertexHandle = {};

		// インスタンス専用のBLAS
		Graphics::Raytracing::BLAS instanceBLAS;

		// どのメッシュの参照先か
		Handle<Resource::Mesh> meshHandle;
	};

	/// <summary>
	/// レイアニメーション用構造体
	/// </summary>
	struct DynamicRaytracingData
	{
		// モデルのスキニングするメッシュすべて
		std::vector<SkinningMeshData> meshDataVec;
	};

	struct DynamicRaytracingInitRequest
	{
		Handle<Graphics::Animation::SkinningMeshData> skiningInstanceHandle = {};

		// 初期化先のハンドル
		Handle<DynamicRaytracingData> dynamicInstanceHandle;
		// 初期化に必要な元モデルのハンドル
		Engine::Handle<Engine::Resource::Model> modelHandle;
	};

	struct DynamicRaytracingRequest
	{
		Math::Matrix worldMat;				// ワールド行列
		Math::Color colorScale;			// 色スケール
		Math::Vector3 emissiveScale;		// エミッシブスケール
		Math::Vector3 emissiveAdd;			// 自己発光(加算・1.0超え可)

		Engine::Handle<DynamicRaytracingData> dynamicHandle = {};
		Engine::Handle<Resource::NodePoseMatrix> nodePoseHandle = {};
	};
}
