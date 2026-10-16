#pragma once
#include "Engine/Graphics/Frame/DrawList/DrawCommand.h"
#include "Engine/Graphics/Frame/DrawList/MeshDrawData.h"
#include "Engine/Graphics/Frame/DrawList/UIData.h"
#include "Engine/Graphics/Frame/DrawList/BoundaryData.h"
#include "Engine/Graphics/Raytracing/DynamicRaytracingData.h"

namespace Engine::Graphics
{
	/// <summary>
	/// 描画要求と受付の配列を保持するクラス
	/// CPU側のデータなのでバックバッファ分保持はしない
	///
	/// 積むのは描画フェーズ(GraphicsEngine の Submit 系)、読むのは Execute の中
	/// (レンダーコンテキストとフレーム計算)。そのフレームのうちに使い切るので、
	/// GPUへ上げたコピーだけがフレーム数ぶんあればよい(レンダーコンテキスト側が持つ)。
	/// 中身は GraphicsEngine::EndFrame で Clear() される
	/// </summary>
	class DrawLists
	{
	public:

		// フレームの終わりに呼ぶ : 空にしつつ、次フレームぶんの容量を確保し直す
		void Clear();

		//--------------------------------------------------------------------------------------------
		// モデルの描画アイテム
		//--------------------------------------------------------------------------------------------
		void AddItem(const LightWeightDrawItem& a_item);

		// 半透明アイテムのソートキーへ、カメラからの距離を入れる。
		// カメラが確定してから(エディターカメラの上書きも済んでから)、SortItems の前に呼ぶ
		void ResolveTransparentSortKeys(const Math::Vector3& a_cameraPos);

		// ソートキー順に並べる。GetPassItems() はこの後でしか引けない。
		// 並べたあとで、描く順のインスタンス番号の表(GetDrawInstanceIndexVec)も作り直す
		void SortItems();

		// 指定したパス番号のアイテムだけを返す(ソート済みであること)。
		// a_pOutFirstIndex には、返した範囲の先頭がソート済み配列全体の何番目かを入れる
		// (描く順のインスタンス番号の表を引くときの土台になる)
		std::span<const LightWeightDrawItem> GetPassItems(uint8_t a_passIndex, UINT* a_pOutFirstIndex = nullptr) const;

		// 積まれた描画アイテムの数(全パスぶん)
		size_t GetItemCount() const { return m_lightWeightDrawItemVec.size(); }

		//--------------------------------------------------------------------------------------------
		// 描く順のインスタンス番号の表(インスタンシング用)
		//
		// ソート済みアイテムの i 番目が使うインスタンスデータの番号を、i 番目に入れたもの。
		// 増幅シェーダーは「土台 + SV_GroupID.y」でこの表を引いてインスタンスデータへ辿るので、
		// 同じメッシュを並んだアイテムぶん1回のディスパッチでまとめて描ける。
		//
		// インスタンスデータ自体はパスをまたいで共有していて、パスごとに並び順(PSO)が違うため、
		// データの側を並べ替えても全部のパスで連続にはできない。そこで表を1枚挟んでいる。
		// 全パスのアイテムが1本のソート済み配列に入っているので、表もフレームに1本で足りる
		//--------------------------------------------------------------------------------------------
		const std::vector<uint32_t>& GetDrawInstanceIndexVec() const { return m_drawInstanceIndexVec; }

		//--------------------------------------------------------------------------------------------
		// メッシュシェーダー用 : 追加した位置(添字)を返す
		//--------------------------------------------------------------------------------------------
		UINT AddInstanceData(const MeshInstanceData& a_instanceData);
		UINT AddMeshMaterial(const MeshMaterial& a_meshMaterial);

		// LOD の表 : 1つのインスタンスの段をまとめて積み、先頭の位置を返す。
		// 容量(MESH_LOD_DATA_CAPACITY)に収まらなければ積まずに false
		bool AddMeshLodData(std::span<const MeshLodData> a_levels, UINT& a_outStart);

		const std::vector<MeshInstanceData>& GetInstanceDataVec() const { return m_meshInstanceDataVec; }
		const std::vector<MeshMaterial>& GetMeshMaterialVec() const { return m_meshMaterialDataVec; }
		const std::vector<MeshLodData>& GetMeshLodDataVec() const { return m_meshLodDataVec; }

		//--------------------------------------------------------------------------------------------
		// UI
		//--------------------------------------------------------------------------------------------
		void AddUI(const UIData& a_data);

		// レイヤー順に並べる。
		// UIパスは深度を持たないので、重なりを決めるのは描く順そのもの
		void SortUIByLayer();

		const std::vector<UIData>& GetUIDataVec() const { return m_uiDrawItemVec; }

		//--------------------------------------------------------------------------------------------
		// 境界面 : 面1枚ずつ積む。描くのは BoundaryPass
		//--------------------------------------------------------------------------------------------
		void AddBoundary(const BoundaryDrawData& a_data);
		const std::vector<BoundaryDrawData>& GetBoundaryDataVec() const { return m_boundaryDrawDataVec; }

		//--------------------------------------------------------------------------------------------
		// GPUスキニング
		//--------------------------------------------------------------------------------------------
		void AddSkinning(const SkinningDispatchItem& a_item);
		const std::vector<SkinningDispatchItem>& GetSkinningItems() const { return m_skinningDispatchItemVec; }

		//--------------------------------------------------------------------------------------------
		// アニメーション用レイトレインスタンス作成命令
		//--------------------------------------------------------------------------------------------
		void AddDynamicRayRequest(const Raytracing::DynamicRaytracingRequest& a_request);
		const std::vector<Raytracing::DynamicRaytracingRequest>& GetDynamicRayRequests() const { return m_dynamicRayRequestVec; }

		//--------------------------------------------------------------------------------------------
		// ボーンパレット
		//
		// このワールドのボーン行列をパレットへ積み、GPU上の開始位置を返す。
		// 同じフレームで同じワールドを二度呼んでも積み直さず、最初に積んだ位置を返す
		//--------------------------------------------------------------------------------------------
		uint32_t AcquireBoneBaseIndex(ECS::World& a_world);
		const std::vector<Resource::BoneMatrix>& GetBoneMatrixVec() const { return m_boneMatrixVec; }

	private:

		// ソートキー持ち描画コマンドリスト
		std::vector<LightWeightDrawItem> m_lightWeightDrawItemVec = {};

		// ソート済みアイテムの並び順で引くインスタンス番号の表
		std::vector<uint32_t> m_drawInstanceIndexVec = {};

		// オブジェクト単位データ
		std::vector<MeshInstanceData> m_meshInstanceDataVec = {};

		// サブセット単位データ
		std::vector<MeshMaterial> m_meshMaterialDataVec = {};

		// LOD の表(インスタンスごとに段の数だけ並ぶ)
		std::vector<MeshLodData> m_meshLodDataVec = {};

		// UI用アイテム配列
		std::vector<UIData> m_uiDrawItemVec = {};

		// 境界面の配列(面1枚ずつ)
		std::vector<BoundaryDrawData> m_boundaryDrawDataVec = {};

		// GPUスキニング配列
		std::vector<SkinningDispatchItem> m_skinningDispatchItemVec = {};

		// アニメーション用レイトレインスタンス作成命令
		std::vector<Raytracing::DynamicRaytracingRequest> m_dynamicRayRequestVec = {};

		//--------------------------------------------------------------------------------------------
		// ボーンパレット
		//--------------------------------------------------------------------------------------------
		// ボーン行列はシーン(ワールド)ごとのプールに入っていて、添字も 0 から振り直される。
		// ポーズ画面のようにシーンを重ねて描くときは複数のワールドを1フレームで描くので、
		// ここで全ワールド分を1本に連結し、各ワールドの土台(開始位置)を覚えておく。
		std::vector<Resource::BoneMatrix> m_boneMatrixVec = {};
		std::unordered_map<const ECS::World*, uint32_t> m_boneBaseIndexMap = {};
	};
}
