#pragma once
#include "Engine/Graphics/PipelineState/PSOKey.h"
#include "Engine/Graphics/Frame/DrawList/MeshDrawData.h"
#include "Engine/Graphics/Raytracing/DynamicRaytracingData.h"

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
		/// <param name="a_world">ECSワールドポインタ</param>
		/// <param name="a_pModel">モデルポインタ</param>
		/// <param name="dynamicHandle">変形後のデータを入れるインスタンス</param>
		/// <param name="nodePoseHandle">ノード行列</param>
		void SubmitSkinning(
			ECS::World& a_world,
			const Resource::Model* a_pModel,
			const Handle<Raytracing::DynamicRaytracingData> dynamicHandle,
			const RangeHandle<Resource::NodePoseMatrix> nodePoseHandle,
			const RangeHandle<Resource::BoneMatrix> boneHandle
		);

		//--------------------------------------------------------------------------------------------
		// 描画コマンド : モデル
		//--------------------------------------------------------------------------------------------

		/// <summary>
		/// 指定したモデルを指定の座標に描画する命令 : 即時実行ではなく、コマンドとしてためたのちに一括で実行される
		/// </summary>
		/// <param name="a_world">ワールド</param>
		/// <param name="a_pModel">モデルのポインタ</param>
		/// <param name="a_worldMatrix">ワールド行列</param>
		/// <param name="a_albedoScale">カラースケール</param>
		/// <param name="a_emissiveScale">エミッシブスケール(エミッシブテクスチャに掛ける倍率)</param>
		/// <param name="a_emissiveAdd">マテリアルに依らない自己発光(加算・1.0超え可)</param>
		void SubmitModel(
			ECS::World& a_world,
			const Resource::Model* a_pModel,
			const Math::Matrix& a_worldMatrix,
			const Math::Color& a_albedoScale = Color::WHITE,
			const Math::Vector3& a_emissiveScale = {1,1,1},
			const Math::Vector3& a_emissiveAdd = {0,0,0}
		);
		/// <summary>
		/// 指定したモデルを指定の座標に描画する命令 : 即時実行ではなく、コマンドとしてためたのちに一括で実行される
		/// </summary>
		/// <param name="a_world">ワールド</param>
		/// <param name="a_pModel">モデルのポインタ</param>
		/// <param name="a_worldMatrix">ワールド行列</param>
		/// <param name="a_prevMatrix">過去ワールド行列</param>
		/// <param name="a_albedoScale">カラースケール</param>
		/// <param name="a_emissiveScale">エミッシブスケール(エミッシブテクスチャに掛ける倍率)</param>
		/// <param name="a_emissiveAdd">マテリアルに依らない自己発光(加算・1.0超え可)</param>
		void SubmitModel(
			ECS::World& a_world,
			const Resource::Model* a_pModel,
			const Math::Matrix& a_worldMatrix,
			const Math::Matrix& a_prevMatrix,
			const Math::Color& a_albedoScale = Color::WHITE,
			const Math::Vector3& a_emissiveScale = { 1,1,1 },
			const Math::Vector3& a_emissiveAdd = { 0,0,0 }
		);
		/// <summary>
		/// 指定したモデルを指定の座標に描画する命令 : 即時実行ではなく、コマンドとしてためたのちに一括で実行される
		/// </summary>
		/// <param name="a_world">ワールド</param>
		/// <param name="a_pModel">モデルのポインタ</param>
		/// <param name="a_worldMatrix">ワールド行列</param>
		/// <param name="a_prevMatrix">過去ワールド行列</param>
		/// <param name="a_boneHandle">ボーン行列配列ハンドル</param>
		/// <param name="a_nodePoseHandle">スケルトンポーズ行列配列ハンドル</param>
		/// <param name="a_animData">アニメーション後頂点配列</param>
		/// <param name="a_albedoScale">カラースケール</param>
		/// <param name="a_emissiveScale">エミッシブスケール(エミッシブテクスチャに掛ける倍率)</param>
		/// <param name="a_emissiveAdd">マテリアルに依らない自己発光(加算・1.0超え可)</param>
		void SubmitModel(
			ECS::World& a_world,
			const Resource::Model* a_pModel,
			const Math::Matrix& a_worldMatrix,
			const Math::Matrix& a_prevMatrix,
			const RangeHandle<Resource::BoneMatrix>& a_boneHandle,
			const RangeHandle<Resource::NodePoseMatrix>& a_nodePoseHandle,
			const Handle<Raytracing::DynamicRaytracingData>& a_animData,
			const Math::Color& a_albedoScale = Color::WHITE,
			const Math::Vector3& a_emissiveScale = { 1,1,1 },
			const Math::Vector3& a_emissiveAdd = { 0,0,0 }
		);

		/// <summary>
		/// レイトレワールドに登録するアニメーションモデル
		/// </summary>
		/// <param name="a_worldMat">ワールド行列</param>
		/// <param name="a_colorScale">色スケール</param>
		/// <param name="a_emissiveScale">エミッシブスケール</param>
		/// <param name="dynamicHandle">ダイナミックリソースハンドル</param>
		/// <param name="nodePoseHandle">ノードポーズハンドル</param>
		void SubmitModel(
			const Math::Matrix& a_worldMat,				// ワールド行列
			const Math::Color& a_colorScale,			// 色スケール
			const Math::Vector3& a_emissiveScale,		// エミッシブスケール
			const Engine::Handle<Raytracing::DynamicRaytracingData> dynamicHandle,
			const Engine::Handle<Resource::NodePoseMatrix> nodePoseHandle,
			const Math::Vector3& a_emissiveAdd = { 0,0,0 }	// 自己発光(加算)
		);

		/// <summary>
		/// グラウンドエフェクト用のグラウンド指定
		/// </summary>
		/// <param name="a_world">ワールド</param>
		/// <param name="a_pModel">モデルのポインタ</param>
		/// <param name="a_worldMatrix">ワールド行列</param>
		void SubmitGroundModel(
			ECS::World& a_world,
			const Resource::Model* a_pModel,
			const Math::Matrix& a_worldMatrix
		);
		//--------------------------------------------------------------------------------------------
		// 描画コマンド : UI
		//--------------------------------------------------------------------------------------------
		/// <summary>
		/// UI描画命令。座標系はピクセル(左上原点/Y下向き)。回転・アスペクト補正・
		/// ピボットはエンジン側でピクセル空間で計算するため、斜め回転でも歪まない。
		/// </summary>
		/// <param name="a_texHandle">テクスチャハンドル</param>
		/// <param name="a_pixelPos">ピボットのスクリーン座標(px, 左上原点)</param>
		/// <param name="a_pixelSize">表示フルサイズ(px)</param>
		/// <param name="a_color">色</param>
		/// <param name="a_rotationDeg">回転(度, 時計回り)</param>
		/// <param name="a_layer">Z順</param>
		/// <param name="a_uvOffset">UVオフセット</param>
		/// <param name="a_pivot">回転軸/基準点(正規化[0,1], 0.5=中心)</param>
		/// <param name="a_uvScale">
		/// UVに掛ける倍率(uv * uvScale + uvOffset)。既定は等倍。
		/// 1枚に並べた絵から1コマだけ出すときに、倍率でコマの大きさを指定する
		/// </param>
		/// <param name="a_curveK">
		/// 湾曲の強さ(1/px)。0で曲げない。
		/// 弧の中心から横へ dx(px) 離れた点が k*dx^2 だけ下がる。
		/// 開き角などの作り手が触る値からの変換は Decoration::Resolve が持つ
		/// </param>
		/// <param name="a_curveOffsetX">
		/// 弧の中心から、このクアッドの中心までの横ずれ(px)。
		/// 1つのUIが枠・中身・文字と複数のクアッドに分かれても、
		/// これを正しく渡せば全部が同じ1本の弧に乗る
		/// </param>
		/// <param name="a_pClipRect">
		/// 切り抜く矩形(px)。この外へ出たぶんは描かない。nullptr なら切らない。
		/// スクロールする一覧のように、範囲の外へはみ出した項目を切るときに渡す
		/// </param>
		/// <param name="a_postEffectFlags">
		/// このUIに掛けるポストプロセスの印(UI_POST_EFFECT_*)。0 なら何も掛けない。
		/// UIPass がマスクへ書き、マスクを繋いだパス(ScanLinePass など)がここにだけ効果を掛ける
		/// </param>
		void SubmitUI(
			const Handle<Resource::Texture>& a_texHandle,
			const Math::Vector2& a_pixelPos,
			const Math::Vector2& a_pixelSize,
			const Math::Color& a_color = {},
			float a_rotationDeg = 0,
			float a_layer = 0,
			const Math::Vector2& a_uvOffset = {},
			const Math::Vector2& a_pivot = { 0.5f, 0.5f },
			const Math::Vector2& a_uvScale = { 1.0f, 1.0f },
			float a_curveK = 0.0f,
			float a_curveOffsetX = 0.0f,
			const UIClipRect* a_pClipRect = nullptr,
			UINT a_postEffectFlags = 0
		);

		/// <summary>
		/// UI描画命令(サイズはテクスチャ原寸×スケール)。座標系はピクセル。
		/// </summary>
		/// <param name="a_texHandle">テクスチャハンドル</param>
		/// <param name="a_pixelPos">ピボットのスクリーン座標(px, 左上原点)</param>
		/// <param name="a_scale">テクスチャ原寸に掛けるスケール</param>
		/// <param name="a_color">色</param>
		/// <param name="a_rotationDeg">回転(度, 時計回り)</param>
		/// <param name="a_layer">Z順</param>
		/// <param name="a_uvOffset">UVオフセット</param>
		/// <param name="a_pivot">回転軸/基準点(正規化[0,1], 0.5=中心)</param>
		/// <param name="a_curveK">
		/// 湾曲の強さ(1/px)。0で曲げない。
		/// 弧の中心から横へ dx(px) 離れた点が k*dx^2 だけ下がる。
		/// 開き角などの作り手が触る値からの変換は Decoration::Resolve が持つ
		/// </param>
		/// <param name="a_curveOffsetX">
		/// 弧の中心から、このクアッドの中心までの横ずれ(px)。
		/// 1つのUIが枠・中身・文字と複数のクアッドに分かれても、
		/// これを正しく渡せば全部が同じ1本の弧に乗る
		/// </param>
		void SubmitUI(
			const Handle<Resource::Texture>& a_texHandle,
			const Math::Vector2& a_pixelPos,
			float a_scale = 1.0f,
			const Math::Color& a_color = Math::Color::White(),
			float a_rotationDeg = 0,
			float a_layer = 0,
			const Math::Vector2& a_uvOffset = {},
			const Math::Vector2& a_pivot = { 0.5f, 0.5f },
			float a_curveK = 0.0f,
			float a_curveOffsetX = 0.0f
		);

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
			PSOKey a_psoKey);

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
			PSOKey a_psoKey);

		// ピクセル空間で回転・アスペクト補正・ピボットを解決し、UIData(NDC基底)を
		// 1件バッファへ積む(SubmitUI 各オーバーロード共通)。
		void PushUIData(
			uint32_t a_texIndex,
			const Math::Vector2& a_pixelPos,
			const Math::Vector2& a_pixelSize,
			const Math::Color& a_color,
			float a_rotationDeg,
			float a_layer,
			const Math::Vector2& a_uvOffset,
			const Math::Vector2& a_pivot,
			const Math::Vector2& a_uvScale = { 1.0f, 1.0f },
			float a_curveK = 0.0f,
			float a_curveOffsetX = 0.0f,
			const UIClipRect* a_pClipRect = nullptr,
			UINT a_postEffectFlags = 0
		);

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
