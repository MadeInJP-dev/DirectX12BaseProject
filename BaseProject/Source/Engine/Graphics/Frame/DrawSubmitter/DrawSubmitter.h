#pragma once
#include "Engine/Graphics/PipelineState/PSOKey.h"
#include "Engine/Graphics/Frame/DrawList/MeshDrawData.h"
#include "Engine/Graphics/Raytracing/DynamicRaytracingData.h"
#include "Engine/Graphics/Frame/DrawSubmitter/Descs/GPUSkiningSbumitDesc.h"
#include "Engine/Graphics/Frame/DrawSubmitter/Descs/ModelSubmitDesc.h"
#include "Engine/Graphics/Frame/DrawSubmitter/Descs/SpriteSubmitDesc.h"

namespace Engine::ECS
{
	class World;
}

namespace Engine::Resource
{
	class ResourceManager;
	class Model;
	class Mesh;
	struct Material;
	class Texture;
	struct ModelDrawCommand;
}

namespace Engine::Graphics
{
	class GraphicsEngine;
	class RenderDevice;
	class DrawLists;
	class PipelineStateManager;
	class MeshBufferAllocator;
	class CameraPipelineManager;
	struct UIClipRect;

	//==========================================================================================
	// 描画要求の受け口
	//
	// アプリ側の描画システムから、モデル・UI・スキニングの要求を受け取り、
	// 描画アイテム(DrawLists)の形にして積む。積むだけで、GPUへ流すのは GraphicsEngine::Execute。
	//
	// モデルは「受け取るパス(ZPre・GBuffer など)の数だけ」アイテムになる。
	// パスの一覧は CameraPipelineManager がフレームの頭で作ったものを引く
	//==========================================================================================
	class DrawSubmitter
	{
	public:

		//--------------------------------------------------------------------------------------------
		// 初期化
		//--------------------------------------------------------------------------------------------
		// 引く先(どれも GraphicsEngine の持ち物)を控える。
		// DrawLists / PSO管理 / メッシュバッファ / カメラ管理 / デバイスを作った後に呼ぶこと
		void Init(GraphicsEngine* a_pGraphicsEngine);

		//--------------------------------------------------------------------------------------------
		// 計算コマンド : スキニング
		//--------------------------------------------------------------------------------------------

		/// <summary>
		/// ワールドに積まれたアニメーションモデルの初期化要求(BLAS・アニメ用頂点領域)を処理する。
		/// そのワールドの描画(PreDraw)より前に呼ぶこと : 描画側は用意された頂点領域の位置を読む
		/// </summary>
		/// <param name="a_world">要求を積んだワールド</param>
		void ProcessDynamicRaytracingInit(ECS::World& a_world);

		/// <summary>
		/// GPUスキニングさせる命令
		/// </summary>
		/// <param name="a_desc">スキニングの要求(必須項目は GPUSkiningSubmitDesc を参照)</param>
		void SubmitSkinning(const GPUSkiningSubmitDesc& a_desc);

		//--------------------------------------------------------------------------------------------
		// 描画コマンド : モデル
		//--------------------------------------------------------------------------------------------

		/// <summary>
		/// 指定したモデルを指定の座標に描画する命令 : 即時実行ではなく、コマンドとしてためたのちに一括で実行される
		/// </summary>
		/// <param name="a_desc">描画の要求(必須項目は ModelSubmitDesc を参照)</param>
		void SubmitModel(const ModelSubmitDesc& a_desc);

		/// <summary>
		/// アニメーションするモデルを描画する命令 : 即時実行ではなく、コマンドとしてためたのちに一括で実行される。
		/// 頂点はスキニングのパスが変形したものを読むので、同じフレームで SubmitSkinning も積むこと
		/// </summary>
		/// <param name="a_desc">描画の要求(必須項目は AnimationModelSubmitDesc を参照)</param>
		void SubmitModel(const AnimationModelSubmitDesc& a_desc);

		/// <summary>
		/// グラウンドエフェクト用のグラウンド指定
		/// </summary>
		/// <param name="a_desc">登録の要求(必須項目は GroundModelSubmitDesc を参照)</param>
		void SubmitGroundModel(const GroundModelSubmitDesc& a_desc);

		//--------------------------------------------------------------------------------------------
		// 描画コマンド : UI
		//--------------------------------------------------------------------------------------------

		/// <summary>
		/// UI描画命令。座標系はピクセル(左上原点/Y下向き)。回転・アスペクト補正・
		/// ピボットはエンジン側でピクセル空間で計算するため、斜め回転でも歪まない。
		///
		/// 湾曲(curveK / curveOffsetX)は、弧の中心から横へ dx(px) 離れた点が k*dx^2 だけ下がる。
		/// 1つのUIが枠・中身・文字と複数のクアッドに分かれても、
		/// curveOffsetX を正しく渡せば全部が同じ1本の弧に乗る。
		/// 開き角などの作り手が触る値からの変換は Decoration::Resolve が持つ
		/// </summary>
		/// <param name="a_desc">描画の要求(必須項目は SpriteSubmitDesc を参照)</param>
		void SubmitUI(const SpriteSubmitDesc& a_desc);

	private:

		// テクスチャハンドルからSRVのインデックスを取得する(引けなければ -1)
		int GetSRVIndexFromTextureHandle(const Handle<Resource::Texture>& a_texHandle) const;

		// 描画コマンドからメッシュ・マテリアルをまとめて取得する。
		// いずれかが取得できなければ false(呼び出し側はスキップする)。
		bool FetchDrawResources(
			const Resource::ModelDrawCommand& a_cmd,
			const Resource::Mesh*& a_pOutMesh,
			const Resource::Material*& a_pOutMaterial);

		// マテリアルとスケールからメッシュシェーダー用マテリアルデータを構築する。
		MeshMaterial BuildMeshMaterial(
			const Resource::Material* a_pMaterial,
			const Math::Color& a_albedoScale,
			const Math::Vector3& a_emissiveScale,
			const Math::Vector3& a_emissiveAdd);

		// 1つの描画コマンドを、モデルを受け取る全パスへ登録する共通処理。
		// (メッシュシェーダー用データ構築・PSO要求・描画アイテム登録をまとめて行う)
		void RegisterDrawCommandToPasses(
			const Resource::ModelDrawCommand& a_cmd,
			const Resource::Mesh* a_pMesh,
			const Resource::Material* a_pMaterial,
			const Math::Matrix& a_mat,
			const Math::Matrix& a_prevMat,
			bool a_isAnimation,
			uint32_t a_animatedVertexStart,
			const Math::Color& a_albedoScale,
			const Math::Vector3& a_emissiveScale,
			const Math::Vector3& a_emissiveAdd,
			PSOKey a_psoKey,
			uint32_t a_lodStart = 0,
			uint32_t a_lodCount = 0);

		// キュー指定 : 1つの描画コマンドを、モデルを受け取る全パスへ登録する共通処理。
		// (メッシュシェーダー用データ構築・PSO要求・描画アイテム登録をまとめて行う)
		void RegisterDrawCommandToPasses(
			const EGeometryQueue& a_eGeoQue,
			const Resource::ModelDrawCommand& a_cmd,
			const Resource::Mesh* a_pMesh,
			const Resource::Material* a_pMaterial,
			const Math::Matrix& a_mat,
			const Math::Matrix& a_prevMat,
			bool a_isAnimation,
			uint32_t a_animatedVertexStart,
			const Math::Color& a_albedoScale,
			const Math::Vector3& a_emissiveScale,
			const Math::Vector3& a_emissiveAdd,
			PSOKey a_psoKey,
			uint32_t a_lodStart = 0,
			uint32_t a_lodCount = 0);

		// LOD0 の a_cmdIndex 番目の描画コマンドに対応する LOD の段を、LOD の表へ積む。
		// 段が無い・表に収まらなければ a_outCount は 0(LOD0 だけで描く)
		void PushMeshLodData(
			const Resource::Model& a_model,
			size_t a_cmdIndex,
			const Resource::ModelDrawCommand& a_baseCmd,
			const Resource::Mesh* a_pBaseMesh,
			const Math::Vector4& a_worldSphere,
			uint32_t& a_outStart,
			uint32_t& a_outCount);

		// ピクセル空間で回転・アスペクト補正・ピボットを解決し、UIData(NDC基底)を
		// 1件バッファへ積む。
		void PushUIData(uint32_t a_texIndex, const SpriteSubmitDesc& a_desc);

	private:

		// 引く先(借り物)。どれも GraphicsEngine の持ち物
		DrawLists* m_pDrawLists = nullptr;
		Resource::ResourceManager* m_pResourceManager = nullptr;
		PipelineStateManager* m_pPipelineStateManager = nullptr;
		MeshBufferAllocator* m_pMeshBufferAllocator = nullptr;
		CameraPipelineManager* m_pCameraPipelines = nullptr;
		RenderDevice* m_pRenderDevice = nullptr;

		// 描画解像度(UIのピクセル座標をNDCへ直すのに使う)
		UINT m_renderWidth = 0;
		UINT m_renderHeight = 0;
	};
}
