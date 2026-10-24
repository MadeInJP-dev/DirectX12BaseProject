#pragma once

namespace Engine::Graphics::Raytracing
{
	class BLAS;

	// レイトレワールドに載せられるインスタンス数の上限。
	// TLAS・インスタンス/マテリアルのバッファ・シェーダーテーブルの大きさはすべてこれで決まる。
	// どれか1つだけ違う数にすると、多いほうのデータが少ないほうのバッファをはみ出すので、
	// 数を直接書かず必ずこれを使うこと。
	// ボスの死亡演出はボスの体(最大でボスの死亡の体力ぶんのボイド)を丸ごと載せて点光源の影を引くので、
	// シーンの物と合わせても溢れないだけの数にしてある
	inline constexpr UINT MAX_INSTANCE_NUM = 4096;

	// ===================================================================================
	// GPU(HLSL) 転送用データ構造
	// ※ StructuredBuffer として HLSL に送るため、16バイト(float4)アライメントを厳密に管理
	// ===================================================================================

	/// <summary>
	/// レイトレーシング空間上の1メッシュの描画パラメータ
	/// </summary>
	struct InstanceData
	{
		// --- 16 Bytes (Offset: 0) ---
		UINT materialOffset;    // Globalマテリアル配列における、このインスタンスの開始位置
		UINT vertexStart;       // 頂点バッファ内の参照開始オフセット (MegaBuffer または AnimatedBuffer 内)
		UINT indexStart;        // インデックスバッファ内の参照開始オフセット
		UINT indexCount;        // このインスタンスが持つインデックス数

		// --- 16 Bytes (Offset: 16) ---
		UINT isAnimated;        // アニメーション対象かどうかのフラグ (0: Static, 1: Animated)
		UINT animatedVertexStart; // アニメ済み頂点バッファ内の参照開始オフセット (isAnimated==1のとき使用)
		Math::Vector2 pad0;     // 16バイトアライメント用のパディング
	}; // Total: 32 Bytes

	/// <summary>
	/// PBRマテリアルデータ (サブメッシュごとに設定)
	/// </summary>
	struct Material
	{
		// --- 16 Bytes (Offset: 0) ---
		Math::Color         baseColor;              // rgb: BaseColor, a: Alpha

		// --- 16 Bytes (Offset: 16) ---
		Math::Vector3   emissive;               // 発光カラー
		float               metallic;               // 金属度

		// --- 16 Bytes (Offset: 32) ---
		float               roughness;              // 粗さ
		UINT                baseIndex;              // BaseColorテクスチャのSRVインデックス (Bindless)
		UINT                metaRoughnessIndex;     // Metallic/RoughnessテクスチャのSRVインデックス
		UINT                emissiveIndex;          // EmissiveテクスチャのSRVインデックス

		// --- 16 Bytes (Offset: 48) ---
		UINT                normalIndex;            // NormalマップテクスチャのSRVインデックス
		UINT                startIndexLocation;     // インデックスバッファ内のサブメッシュ開始位置
		Math::Vector2   pad0;                   // 16バイトアライメント用のパディング

		// --- 16 Bytes (Offset: 64) ---
		// マテリアルとは独立した自己発光(ModelComponent の 発光色 × 発光強度)。
		// emissive はマテリアルの発光色に倍率を掛けたものなので、発光しない
		// マテリアル(emissive = 0)は何倍しても光らない。こちらは加算なので単体で光る。
		// GBuffer側の SubSetData::emissiveAdd / MeshMaterial::emissiveAdd と同じ値が入る。
		Math::Vector3   emissiveAdd;            // 自己発光(加算・1.0超え可)
		float               pad1;                   // 16バイトアライメント用のパディング
	}; // Total: 80 Bytes

	// ===================================================================================
	// CPU側 レイワールド構築用データ構造
	// ===================================================================================

	/// <summary>
	/// レイワールド（TLAS）に登録するインスタンスの管理単位
	/// 静的・動的どちらのモデルもこの形式に正規化されてコミットされる
	/// </summary>
	struct Instance
	{
		// 空間情報
		Math::Matrix worldMat = Math::Matrix::Identity(); // TLASに登録する際のトランスフォーム
		const BLAS* pBLAS = nullptr;                    // 参照するBLAS（動的モデルの場合は毎フレーム更新されたBLASを指す）

		// --- ジオメトリ参照情報 ---
		// HLSLのBindless配列に渡すためのハンドル
		RangeHandle<Resource::MeshVertexPacked> megaVertexHandle = {};	// 静的: StaticMegaBuffer, 動的: AnimatedBuffer
		RangeHandle<uint32_t> megaIndexHandle = {};						 // IndexBuffer (基本的に静的と共通)

		// バッファ内の論理的なオフセット
		UINT vertexOffset = 0;                          // vertexStart に相当
		UINT indexOffset = 0;                           // indexStart に相当
		UINT indexCount = 0;                            // indexCount に相当

		// 動的モデルの場合の、アニメ済み頂点バッファ内の開始オフセット
		UINT animatedVertexOffset = 0;                  // animatedVertexStart に相当

		// 状態フラグ
		bool isAnimated = false;                        // 動的モデル判定用

		// --- 描画メタデータ ---
		std::vector<Material> submeshMaterials;         // サブメッシュごとのマテリアル情報
	};
}
