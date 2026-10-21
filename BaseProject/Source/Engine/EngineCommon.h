#pragma once
//==========================================================================================
// 
// 共通仕様
// 
//==========================================================================================
// ---- Core(Engine の下にある道具箱) ----
#include "Core/Core.h"

//==========================================================================================
// Core の取り込み
//
// Engine の中から Core:: を付けずに使えるようにする。
// using namespace Core にしないのは、Core::GUID が Windows の ::GUID と曖昧になるため
//==========================================================================================
namespace Engine
{
	// 名前空間はそのまま別名で
	namespace Math = Core::Math;
	namespace String = Core::String;
	namespace File = Core::File;
	namespace TypeInfo = Core::TypeInfo;
	namespace Algorithm = Core::Algorithm;
	namespace BinaryHelper = Core::BinaryHelper;
	namespace Debug = Core::Debug;

	// 型・定数
	using Core::GUID;
	using Core::DEFAULT_GUID;

	// enum class のフラグ演算(演算子は名前で引けないと使えないため)
	using Core::operator|;
	using Core::operator|=;
	using Core::operator&;
	using Core::operator&=;
	using Core::operator~;
	using Core::HasFlag;
}

// ---- 共通変数・固定値 ----
#include "Engine/Common/Color.h"						// 色
#include "Engine/Common/ID.h"							// ID
#include "Engine/Common/Index.h"						// インデックス
#include "Engine/Common/Handle.h"						// ハンドル
#include "Engine/Common/EngineConfigTypes.h"			// エンジン基盤設定

// ---- マクロ ---- 
#include "Engine/Common/Macros/ClassMacros.h"			// クラス用マクロ

// ---- 外部ライブラリ連携 ----
// JSONHelper.h は AssetDatabase.cpp でしか使わないので、そちらで読む
// (json.hpp を要求するヘッダーをここへ置くと全翻訳単位に乗る)
#include "Engine/Graphics/D3D12/D3D12Types.h"					// D3D12の共通設定
#include "Engine/Graphics/D3D12/D3D12Helper.h"							// D3D12関連のヘルパー関数

// ---- プール ----
#include "Utility/Pool/HandlePool/HandlePool.h"			// ハンドル管理ストレージ
#include "Utility/Pool/ItemPool/ItemPool.h"				// 実体管理ストレージ
#include "Utility/Pool/ItemPool/AtomicItemPool.h"		// 実体管理ストレージ(スレッドセーフ版)
#include "Utility/Pool/RangePool/RangePool.h"			// レンジ管理ストレージ
#include "Utility/Pool/RangeAllocator/RangeAllocator.h"	// レンジ管理

// ジョブシステム
#include "JobSystem/Core/Job/Job.h"

//==========================================================================================
// 
// 保存
// 
//==========================================================================================
#include "Persistence/Archive/Archive.h"

//==========================================================================================
// 
// ストレージ管理
// 
//==========================================================================================
namespace Engine::Resource
{
	using Index = uint16_t;
	using Generation = uint16_t;
	using ID = uint32_t;

	namespace Limits
	{
		constexpr ID			INVALID_ID = std::numeric_limits<ID>::max();
		constexpr Index			INVALID_INDEX = std::numeric_limits<Index>::max();
		constexpr Generation	INVALID_GENERATION = std::numeric_limits<Generation>::max();
	}

	inline Index GetIndex(ID a_id)
	{
		return static_cast<Index>(a_id & 0xFFFF);
	}
	inline Generation GetGeneration(ID a_id)
	{
		return static_cast<Generation>(a_id >> 16);
	}
	inline ID GetID(Index a_idx,Generation a_gen)
	{
		return static_cast<ID>(a_gen) << 16 | a_idx;
	}
}


#include "Engine/Resource/Common/Common.h"


//==========================================================================================
// 
// DirectX12ラッパー
// 
//==========================================================================================
#include "Engine/Graphics/D3D12/D3DObject/DescriptorHeap/DescriptorHeap.h"

//------------------------------------------------------------------------------------------
// オブジェクト
//------------------------------------------------------------------------------------------
#include "Engine/Graphics/D3D12/D3DObject/PipelineState/PipelineState.h"

//------------------------------------------------------------------------------------------
// バッファー
//------------------------------------------------------------------------------------------
#include "Engine/Graphics/D3D12/D3DObject/GPUResource/GPUResource.h"
#include "Engine/Graphics/D3D12/GPUBuffer/VertexBuffer/DynamicVertexBuffer.h"			// 頂点バッファ
#include "Engine/Graphics/D3D12/GPUBuffer/IndexBuffer/DynamicIndexBuffer.h"				// ダイナミックインデックスバッファ
#include "Engine/Graphics/D3D12/GPUBuffer/StructuredBuffer/StaticStructuredBuffer.h"		// スタティックストラクチャバッファ
#include "Engine/Graphics/D3D12/GPUBuffer/StructuredBuffer/DynamicStructuredBuffer.h"	// ダイナミックストラクチャバッファ
#include "Engine/Graphics/D3D12/GPUBuffer/MegaBuffer/MegaRWStructuredBuffer/MegaRWStructuredBuffer.h"	// RWストラクチャバッファ
#include "Engine/Graphics/D3D12/GPUBuffer/ByteAddressBuffer/ByteAddressBuffer.h"				// バイトアドレスバッファ
#include "Engine/Graphics/D3D12/GPUBuffer/ByteAddressBuffer/StaticByteAddressBuffer.h"			// スタティックバイトアドレスバッファ
#include "Engine/Graphics/D3D12/GPUBuffer/RWStructuredBuffer/RWStructuredBuffer.h"		// GPU用UAV構造体バッファ
#include "Engine/Resource/Data/Vertex/Vertex.h"									// 頂点データ
#include "Engine/Graphics/D3D12/GPUBuffer/MegaBuffer/MegaStructuredBuffer/MegaStructuredBuffer.h"
//==========================================================================================
// 
// 入力
// 
//==========================================================================================
#include "Input/Core/InputAction.h"									// アクションID
#include "Input/InputManager/InputManager.h"
#include "Input/InputCollector/InputCollector.h"
#include "Input/InputDevice/Axis/InputAxisBase.h"
#include "Input/InputDevice/Button/InputButtonBase.h"
//==========================================================================================
// 
// レイトレ用構造体
// 
//==========================================================================================
#include "Engine/Graphics/Raytracing/BLAS/BLAS.h"

//==========================================================================================
// 
// ECS
// 
//==========================================================================================
#include "Engine/ECS/ECSCommon.h"
#include "ECS/Component/CompEditContext.h"
//==========================================================================================
// 
// リソース
// 
//==========================================================================================

#include "Resource/Common/ResourceBuildContext.h"
#include "Resource/Common/ResourceRef.h"

//-----------------------------------------------------------------------------------------
// データ
#include "Resource/Data/Shader/Shader.h"
#include "Engine/Resource/Data/Texture/Texture.h"							// テクスチャ
#include "Engine/Resource/Data/Mesh/Mesh.h"									// メッシュ
#include "Engine/Resource/Data/Animation/Animation.h"						// アニメーションデータ
#include "Engine/Resource/Data/Material/Material.h"							// マテリアル
#include "Engine/Resource/Data/Node/Node.h"									// ノード
#include "Engine/Resource/Data/Model/Model.h"								// モデル
#include "Engine/Resource/Data/QuadPolygon/QuadPolygon.h"					// クアッドポリゴン
#include "Resource/Data/Prefab/Prefab.h"									// プレハブ
#include "Audio/SoundGroup.h"												// 音のグループ(サウンドより先に読む)
#include "Resource/Data/Sound/Sound.h"										// サウンド
#include "Resource/Data/AudioBehavior/AudioBehavior.h"					// サウンドの流れ(始動/継続/終了)
#include "Resource/Data/AnimatorAsset/AnimatorAsset.h"						// アニメーション
#include "Resource/Data/Particles/ParticlesAsset.h"							// パーティクル
#include "Resource/Data/EffectAsset/EffectAsset.h"							// エフェクト(パーティクル+メッシュのまとめ)
#include "Resource/Data/EffectPrefab/EffectPrefab.h"						// エフェクトプレハブ(炊いたら時間で消える大きな演出)
#include "Resource/Data/Font/Font.h"									// フォント(.ttf/.otf/.ttc)
#include "Resource/Data/UIMaterial/UIMaterial.h"							// UIマテリアル(UIに掛ける効果)
#include "Resource/Data/UIPreset/UIPreset.h"								// UIプリセット(飾りの見た目。指している飾りへ配る)
// 
//-----------------------------------------------------------------------------------------


//==========================================================================================
// 
// レイトレ用構造体
// 
//==========================================================================================

#include "Engine/Graphics/Animation/Common/AnimatedMeshVertex.h"

//#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshAllocationHandle.h"

//==========================================================================================
// 
// 描画
// 
//==========================================================================================
#include "Engine/Graphics/GraphicsCommon.h"


//==========================================================================================
// 
// エディター
// 
//==========================================================================================
#include "Engine/EditorField/EditorField.h"		// 編集UIの入口(エディターの外はこれだけを使う)
// エディター内部の描画ヘルパー(EditorHelper.h)は ImGui を使うので EditorPCH.h にある

//==========================================================================================
// 
// アニメーション
// 
//==========================================================================================
#include "Engine/Graphics/Animation/AnimationEvaluator/AnimationEvaluator.h"



//==========================================================================================
// 
// コンテキスト関係
// 
//==========================================================================================
#include "ECS/System/SystemContext.h"			// ECSのシステム間の共通データ






