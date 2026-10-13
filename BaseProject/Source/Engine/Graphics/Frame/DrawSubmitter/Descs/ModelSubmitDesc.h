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
	// モデル描画用命令構造体
	//
	// 以下の項目は必須なため入力時に入れること
	// .pWorld
	// .modelHandle
	// .worldMat
	// 
	// 三次元場を移動するエンティティなら以下も必須
	// .prevWorldMat
	//==========================================================================================
	struct ModelSubmitDesc
	{
		// 必須項目
		ECS::World* pWorld = nullptr;			// 描画するワールド
		Handle<Resource::Model> modelHandle;	// 描画対象のモデル
		Math::Matrix worldMat;					// ワールド行列

		// 過去行列 : 静的でなくワールド上をエンティティが移動するのなら入れる、アニメーションは別。
		// 入れなければ worldMat と同じ(止まっている)として扱う
		std::optional<Math::Matrix> prevWorldMat;

		// スタイル設定 : 必須ではないため初期値を入れる
		Math::Color colorScale = Color::WHITE;			// モデル全体のアルベドに掛かる色スケール
		Math::Vector3 emissiveScale = { 1,1,1 };		// モデル全体のエミッシブに掛かる色スケール

		Math::Vector3 bloomAdd = { 0,0,0 };				// モデル自体を発光させるブルーム加算色
	};

	//==========================================================================================
	// アニメーションモデル描画用命令構造体
	//
	// 以下の項目は必須なため入力時に入れること
	// .pWorld
	// .modelHandle
	// .worldMat
	// .boneHandle
	// .nodeposeHandle
	// .rtAnimHandle
	// 
	// 三次元場を移動するエンティティなら以下も必須
	// .prevWorldMat
	//==========================================================================================
	struct AnimationModelSubmitDesc
	{
		// 必須項目
		ECS::World* pWorld = nullptr;							// 描画するワールド
		Handle<Resource::Model> modelHandle;					// 描画対象のモデル
		Math::Matrix worldMat;									// ワールド行列

		RangeHandle<Resource::BoneMatrix> boneHandle;			// ボーン行列配列ハンドル
		RangeHandle<Resource::NodePoseMatrix> nodeposeHandle;	// ノードポーズ行列配列ハンドル
		Handle<Raytracing::DynamicRaytracingData> rtAnimHandle;	// BLASインスタンスのハンドル

		// 過去行列 : 静的でなくワールド上をエンティティが移動するのなら入れる、アニメーションは別。
		// 入れなければ worldMat と同じ(止まっている)として扱う
		std::optional<Math::Matrix> prevWorldMat;

		// スタイル設定 : 必須ではないため初期値を入れる
		Math::Color colorScale = Color::WHITE;			// モデル全体のアルベドに掛かる色スケール
		Math::Vector3 emissiveScale = { 1,1,1 };		// モデル全体のエミッシブに掛かる色スケール

		Math::Vector3 bloomAdd = { 0,0,0 };				// モデル自体を発光させるブルーム加算色
	};

	//==========================================================================================
	// 地形判別用モデルの登録用構造体 : 直接の描画はされずにシェーダー側での地面判定モデル用
	//
	// 以下の項目は必須なため入力時に入れること
	// .pWorld
	// .modelHandle
	// .worldMat
	//==========================================================================================
	struct GroundModelSubmitDesc
	{
		// 必須項目
		ECS::World* pWorld = nullptr;			// 描画するワールド
		Handle<Resource::Model> modelHandle;	// 描画対象のモデル
		Math::Matrix worldMat;					// ワールド行列
	};
}