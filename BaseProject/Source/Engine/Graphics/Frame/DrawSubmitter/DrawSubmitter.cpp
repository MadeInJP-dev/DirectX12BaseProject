#include "Engine/Graphics/Frame/DrawSubmitter/DrawSubmitter.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Device/RenderDevice.h"
#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshBufferAllocator.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"
#include "Engine/Graphics/RenderingPipeline/CameraPipelineManager/CameraPipelineManager.h"
#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"

#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/ECS/World/World.h"
#include "Engine/Graphics/Frame/DrawList/UIData.h"

namespace Engine::Graphics
{
	void DrawSubmitter::Init(GraphicsEngine* a_pGraphicsEngine)
	{
		// 積み込みは1フレームに何万回も通るので、引く先はここで控えておく。
		// どれも GraphicsEngine の持ち物で、このクラスより長生きする
		m_pDrawLists = a_pGraphicsEngine->RefDrawLists();
		m_pResourceManager = a_pGraphicsEngine->RefResourceManager();
		m_pPipelineStateManager = a_pGraphicsEngine->RefPipelineStateManager();
		m_pMeshBufferAllocator = a_pGraphicsEngine->RefMeshBufferAllocator();
		m_pCameraPipelines = a_pGraphicsEngine->RefCameraPipelines();
		m_pRenderDevice = a_pGraphicsEngine->RefRenderDevice();
		m_renderWidth = a_pGraphicsEngine->GetRenderWidth();
		m_renderHeight = a_pGraphicsEngine->GetRenderHeight();
	}

	void DrawSubmitter::SubmitSkinning(
		ECS::World& a_world,
		const Resource::Model* a_pModel,
		const Handle<Raytracing::DynamicRaytracingData> dynamicHandle,
		const RangeHandle<Resource::NodePoseMatrix> nodePoseHandle,
		const RangeHandle<Resource::BoneMatrix> boneHandle
	)
	{
		// このワールドのボーン行列をパレットへ積み、GPU上の土台を得る
		const uint32_t _boneBaseIndex = m_pDrawLists->AcquireBoneBaseIndex(a_world);

		const auto& _drawCmdVec = a_pModel->GetDrawCommandVec();
		for (const auto& _cmd : _drawCmdVec)
		{
			// マテリアル取得
			auto* _pMaterial = (*m_pResourceManager).Get(_cmd.materialHandle);
			if (!_pMaterial) continue;

			// メッシュ取得
			auto* _pMesh = (*m_pResourceManager).Get(_cmd.meshHandle);
			if (!_pMesh) continue;

			// レイトレ用データを持たないメッシュはスキニング登録できない
			if (!_pMesh->HasRtData()) continue;

			SkinningDispatchItem _item = {};
			_item.pWorld = &a_world;
			_item.staticVertexHandle = _pMesh->GetRtData().vertexHandle;
			_item.skinVertexHandle = _pMesh->GetRtData().skinVertexHandle;
			_item.staticIndexHandle = _pMesh->GetRtData().indexHandle;
			_item.nodePoseMat = nodePoseHandle;
			_item.animHandle = dynamicHandle;
			_item.boneHandle = boneHandle;

			// スキニングのコンピュートが読むのはGPU上の位置なので土台を足す
			_item.boneBufferStart = _boneBaseIndex + boneHandle.startIndex;

			auto& _pool = a_world.RefResource<Pool::ItemPool<Raytracing::DynamicRaytracingData>>();
			auto* _data = _pool.Get(dynamicHandle);
			if (!_data) continue;

			for (auto& _meshData : _data->meshDataVec)
			{
				if (_cmd.meshHandle == _meshData.meshHandle)
				{
					_item.animatedHandle = _meshData.animatedVertexHandle;
				}
			}

			m_pDrawLists->AddSkinning(_item);
		}
	}
	void DrawSubmitter::SubmitModel(
		ECS::World& a_world,
		const Resource::Model* a_pModel,
		const Math::Matrix& a_worldMatrix,
		const Math::Color& a_albedoScale,
		const Math::Vector3& a_emissiveScale,
		const Math::Vector3& a_emissiveAdd
	)
	{
		SubmitModel(
			a_world,
			a_pModel,
			a_worldMatrix,
			a_worldMatrix,
			a_albedoScale,
			a_emissiveScale,
			a_emissiveAdd
		);
	}

	void DrawSubmitter::SubmitModel(
		ECS::World& /*a_world*/,
		const Resource::Model* a_pModel,
		const Math::Matrix& a_worldMatrix,
		const Math::Matrix& a_prevMatrix,
		const Math::Color& a_albedoScale,
		const Math::Vector3& a_emissiveScale,
		const Math::Vector3& a_emissiveAdd
	)
	{
		if (!a_pModel) return;

		// モデルが持っている描画コマンド（サブセット）を展開
		const auto& _drawCmdVec = a_pModel->GetDrawCommandVec();

		for (const auto& _cmd : _drawCmdVec)
		{
			// -----------------------------------------------------
			// リソースの取得と検証
			// -----------------------------------------------------
			const Resource::Mesh* _pMesh = nullptr;
			const Resource::Material* _pMaterial = nullptr;
			if (!FetchDrawResources(_cmd, _pMesh, _pMaterial)) continue;

			// -----------------------------------------------------
			// 行列計算
			// -----------------------------------------------------
			Math::Matrix _nodeTransMat(a_pModel->GetOriginalNodeVec()[_cmd.nodeIndex].worldTransform);
			Math::Matrix _mat = _nodeTransMat * a_worldMatrix;
			Math::Matrix _prevMat = _nodeTransMat * a_prevMatrix;

			// -----------------------------------------------------
			// PermutationFlags の構築
			// -----------------------------------------------------
			// この経路は静的モデル専用(アニメーションするモデルはボーンを受け取る方の SubmitModel)
			constexpr bool IS_ANIMATION = false;
			uint32_t _flags = static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::None);
			_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::Static);

			if (_cmd.alphaMode == Engine::Resource::EAlpha::Mask) {
				_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::AlphaMasked);
			}

			Engine::Graphics::PSOKey _psoKey = {};
			_psoKey.permutationFlags = _flags;

			// -----------------------------------------------------
			// 各パスへの描画アイテム登録(共通処理)
			// -----------------------------------------------------
			RegisterDrawCommandToPasses(
				_cmd, _pMesh, _pMaterial,
				_mat, _prevMat,
				IS_ANIMATION, 0 /*animatedVertexStart*/,
				a_albedoScale, a_emissiveScale, a_emissiveAdd, _psoKey);
		}
	}

	void DrawSubmitter::SubmitModel(
		ECS::World& a_world,
		const Resource::Model* a_pModel,
		const Math::Matrix& a_worldMatrix,
		const Math::Matrix& a_prevMatrix,
		const RangeHandle<Resource::BoneMatrix>& a_boneHandle,
		const RangeHandle<Resource::NodePoseMatrix>& a_nodePoseHandle,
		const Handle<Raytracing::DynamicRaytracingData>& a_animData,
		const Math::Color& a_albedoScale,
		const Math::Vector3& a_emissiveScale,
		const Math::Vector3& a_emissiveAdd
	)
	{
		// ノード行列取得
		auto& _nodePosePool = a_world.RefResource<Pool::RangePool<Resource::NodePoseMatrix>>();
		const auto& _nodePoseMatVec = _nodePosePool.GetRange(a_nodePoseHandle);

		// アニメーション後データ (※1つのモデルに対して共通ならループ外で取得・チェックすると効率的です)
		auto& _pool = a_world.RefResource<Pool::ItemPool<Raytracing::DynamicRaytracingData>>();
		auto* _data = _pool.Get(a_animData);
		if (!_data) return;

		// このワールドのボーン行列をパレットへ積む。
		// 戻り値の土台位置はここでは使わない(頂点をスキニングするのはコンピュートの
		// スキニングパスで、描画側はその結果の頂点バッファを読むだけ)。
		// ただし積むこと自体はそのパスに要るので、呼び出しを外してはいけない
		m_pDrawLists->AcquireBoneBaseIndex(a_world);

		// モデルが持っている描画コマンド（サブセット）を展開
		const auto& _drawCmdVec = a_pModel->GetDrawCommandVec();
		for (const auto& _cmd : _drawCmdVec)
		{
			// メッシュ・マテリアルの取得と検証
			const Resource::Mesh* _pMesh = nullptr;
			const Resource::Material* _pMaterial = nullptr;
			if (!FetchDrawResources(_cmd, _pMesh, _pMaterial)) continue;

			// -----------------------------------------------------
			// アニメーション用頂点オフセットの検索
			// -----------------------------------------------------
			uint32_t _animatedVertexStart = 0;
			if (_data)
			{
				for (const auto& _meshData : _data->meshDataVec)
				{
					if (_cmd.meshHandle == _meshData.meshHandle)
					{
						_animatedVertexStart = _meshData.animatedVertexHandle.startIndex;
						break; // 見つかったらループを抜ける
					}
				}
			}

			// ノードのワールド行列を確定
			// モデル差し替え直後などで描画コマンドとポーズ領域のサイズが食い違った場合は
			// クラッシュさせずこのコマンドの描画をスキップする
			if (_cmd.nodeIndex >= _nodePoseMatVec.size()) continue;
			Math::Matrix _nodeTransMat(_nodePoseMatVec[_cmd.nodeIndex].world);
			Math::Matrix _mat = _nodeTransMat * a_worldMatrix;

			Math::Matrix _prevMat = _nodeTransMat * a_prevMatrix;

			// =========================================================
			// PermutationFlags を構築
			// =========================================================
			uint32_t _flags = static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::None);

			// アニメーション判定
			bool _isAnimation = (a_boneHandle.count > 0);
			_flags |= static_cast<uint32_t>(_isAnimation ?
				Engine::Graphics::EShaderPermutationFlags::Skinned :
				Engine::Graphics::EShaderPermutationFlags::Static);

			// アルファモード判定
			if (_cmd.alphaMode == Engine::Resource::EAlpha::Mask) {
				_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::AlphaMasked);
			}

			// PSOKey作成
			Engine::Graphics::PSOKey _psoKey = {};
			_psoKey.permutationFlags = _flags;

			// =========================================================
			// 各パスへ描画アイテムを投げる(共通処理)
			// =========================================================
			RegisterDrawCommandToPasses(
				_cmd, _pMesh, _pMaterial,
				_mat, _prevMat,
				_isAnimation, _animatedVertexStart,
				a_albedoScale, a_emissiveScale, a_emissiveAdd, _psoKey);
		}
	}

	void DrawSubmitter::SubmitModel(const Math::Matrix& a_worldMat, const Math::Color& a_colorScale, const Math::Vector3& a_emissiveScale, const Engine::Handle<Raytracing::DynamicRaytracingData> dynamicHandle, const Engine::Handle<Resource::NodePoseMatrix> nodePoseHandle, const Math::Vector3& a_emissiveAdd)
	{

		m_pDrawLists->AddDynamicRayRequest(
			{ a_worldMat,a_colorScale,a_emissiveScale,a_emissiveAdd,dynamicHandle,nodePoseHandle }
		);
	}

	void DrawSubmitter::SubmitGroundModel(ECS::World& /*a_world*/, const Resource::Model* a_pModel, const Math::Matrix& a_worldMatrix)
	{
		if (!a_pModel) return;

		// モデルが持っている描画コマンド（サブセット）を展開
		const auto& _drawCmdVec = a_pModel->GetDrawCommandVec();

		for (const auto& _cmd : _drawCmdVec)
		{
			// -----------------------------------------------------
			// リソースの取得と検証
			// -----------------------------------------------------
			const Resource::Mesh* _pMesh = nullptr;
			const Resource::Material* _pMaterial = nullptr;
			if (!FetchDrawResources(_cmd, _pMesh, _pMaterial)) continue;

			// -----------------------------------------------------
			// 行列計算
			// -----------------------------------------------------
			Math::Matrix _nodeTransMat(a_pModel->GetOriginalNodeVec()[_cmd.nodeIndex].worldTransform);
			Math::Matrix _mat = _nodeTransMat * a_worldMatrix;
			Math::Matrix _prevMat = _nodeTransMat * a_worldMatrix;		// 動くことはない予定

			// -----------------------------------------------------
			// PermutationFlags の構築
			// -----------------------------------------------------
			// この経路は静的モデル専用(アニメーションするモデルはボーンを受け取る方の SubmitModel)
			constexpr bool IS_ANIMATION = false;
			uint32_t _flags = static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::None);
			_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::Static);

			if (_cmd.alphaMode == Engine::Resource::EAlpha::Mask) {
				_flags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::AlphaMasked);
			}

			Engine::Graphics::PSOKey _psoKey = {};
			_psoKey.permutationFlags = _flags;

			// -----------------------------------------------------
			// 各パスへの描画アイテム登録(共通処理)
			// -----------------------------------------------------
			RegisterDrawCommandToPasses(
				EGeometryQueue::Ground,
				_cmd, _pMesh, _pMaterial,
				_mat, _prevMat,
				IS_ANIMATION, 0 /*animatedVertexStart*/,
				{}, {}, {}, _psoKey);
		}
	}

	void DrawSubmitter::SubmitUI(const Handle<Resource::Texture>& a_texHandle, const Math::Vector2& a_screenPos, const Math::Vector2& a_screenRect, const Math::Color& a_color, float a_rotation, float a_layer, const Math::Vector2& a_uvOffset, const Math::Vector2& a_pivot, const Math::Vector2& a_uvScale, float a_curveK, float a_curveOffsetX, const UIClipRect* a_pClipRect, UINT a_postEffectFlags)
	{
		auto& _resourceManager = (*m_pResourceManager);

		// 読み込みが終わっていないものは、そのフレームは描かない。
		// 非同期ロード中のスロットには空の実体が入っているため、
		// ポインタのnullチェックだけでは弾けない
		if (!_resourceManager.IsReady(a_texHandle)) return;

		auto* _pTex = _resourceManager.Get(a_texHandle);
		if (!_pTex) return;

		// サイズは呼び出し側の指定値をそのまま使う
		PushUIData(_pTex->GetSRV().GetIndex(), a_screenPos, a_screenRect, a_color, a_rotation, a_layer, a_uvOffset, a_pivot, a_uvScale, a_curveK, a_curveOffsetX, a_pClipRect, a_postEffectFlags);
	}

	void DrawSubmitter::SubmitUI(const Handle<Resource::Texture>& a_texHandle, const Math::Vector2& a_screenPos, float a_scale, const Math::Color& a_color, float a_rotation, float a_layer, const Math::Vector2& a_uvOffset, const Math::Vector2& a_pivot, float a_curveK, float a_curveOffsetX)
	{
		auto& _resourceManager = (*m_pResourceManager);

		// 読み込み中のものは描かない : 空の実体のサイズを掛けても意味がない
		if (!_resourceManager.IsReady(a_texHandle)) return;

		auto* _pTex = _resourceManager.Get(a_texHandle);
		if (!_pTex) return;

		// テクスチャの元サイズにスケールを掛けたものを表示サイズにする
		Math::Vector2 _size = { _pTex->GetDesc().Width * a_scale, _pTex->GetDesc().Height * a_scale };
		PushUIData(_pTex->GetSRV().GetIndex(), a_screenPos, _size, a_color, a_rotation, a_layer, a_uvOffset, a_pivot, {1.0f,1.0f}, a_curveK, a_curveOffsetX);
	}

	//------------------------------------------------------------------------------------------
	// アニメーションするモデルの BLAS と頂点領域を用意する
	//
	// 要求はワールドごとに積まれる(AnimationModelStartSystem など)ので、ワールドを受け取る。
	// 以前は SceneManager の一番上のワールドだけを見ていたため、
	// 下に重なったシーンやエフェクトエディターのワールドの要求を取りこぼしていた。
	//
	// コマンドは専用のリストに積んで先に提出する。
	// 提出した順に流れるので、同じフレームの Execute(スキニング・BLAS更新)より前に構築される
	//------------------------------------------------------------------------------------------
	void DrawSubmitter::ProcessDynamicRaytracingInit(ECS::World& a_world)
	{
		// 必須リソースの存在チェック
		if (!a_world.HasResource<Pool::ItemPool<Raytracing::DynamicRaytracingData>>()) return;
		if (!a_world.HasResource<std::vector<Engine::Graphics::Raytracing::DynamicRaytracingInitRequest>>()) return;

		auto& _initRequestVec = a_world.RefResource<std::vector<Engine::Graphics::Raytracing::DynamicRaytracingInitRequest>>();
		if (_initRequestVec.empty()) return;

		auto& _dynamicPool = a_world.RefResource<Pool::ItemPool<Raytracing::DynamicRaytracingData>>();

		auto* _pDevice = m_pRenderDevice->RefDevice();
		auto* _pCmdList = m_pRenderDevice->AcquireDirectCommandList();

		// モデルのリソースからBLASと頂点バッファをコピー
		for (auto& _initReq : _initRequestVec)
		{
			// ターゲットとなるインスタンスデータと、ソースとなるモデルデータの取得
			auto* _pData = _dynamicPool.Ref(_initReq.dynamicInstanceHandle);
			auto* _pModel = (*m_pResourceManager).Get(_initReq.modelHandle);
			if (!_pData || !_pModel) continue;

			// モデル内の各メッシュごとに動的BLASを構築
			for (auto& _meshHandle : _pModel->GetMeshHandles())
			{
				// メッシュの有効性チェック
				auto* _pMesh = (*m_pResourceManager).Get(_meshHandle);
				if (!_pMesh || !_pMesh->HasRtData()) continue;

				// メッシュデータの追加と参照の取得
				_pData->meshDataVec.emplace_back();
				auto& _targetMeshData = _pData->meshDataVec.back();

				// インスタンス専用のアニメーション用頂点バッファ領域をメガバッファから割り当て
				UINT _vertexCount = _pMesh->GetRtData().vertexHandle.count;
				_targetMeshData.animatedVertexHandle = m_pMeshBufferAllocator->AllocateAnimatedVertex(_vertexCount);

				// サブメッシュ（マテリアル単位）ごとのジオメトリ情報を構築
				std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> _descVec = {};
				_descVec.reserve(_pMesh->GetMetaData().subsets.size());

				// レイトレーシング用データ作成
				for (auto& _subset : _pMesh->GetMetaData().subsets)
				{
					// ジオメトリ記述作成
					D3D12_RAYTRACING_GEOMETRY_DESC _desc = {};
					_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
					_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
					// 頂点バッファ
					_desc.Triangles.VertexBuffer.StartAddress =
						m_pMeshBufferAllocator->GetAnimatedVertexBuffer().GetGPUVirtualAddress() +
						(_targetMeshData.animatedVertexHandle.startIndex * sizeof(Resource::AnimatedVertex));
					_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Resource::AnimatedVertex);	// 先頭の座標だけをこの間隔で読む
					_desc.Triangles.VertexCount = _vertexCount;
					_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

					// インデックスバッファ
					_desc.Triangles.IndexBuffer =
						m_pMeshBufferAllocator->GetIndexBuffer().GetGPUVirtualAddress() +
						sizeof(uint32_t) * (_subset.faceStart * 3 + _pMesh->GetRtData().indexHandle.startIndex);
					_desc.Triangles.IndexCount = _subset.faceCount * 3;
					_desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

					_descVec.push_back(_desc);
				}
				_pData->meshDataVec.back().instanceBLAS.CreateDynamic(
					_pDevice,
					_pCmdList,
					_descVec
				);
				_pData->meshDataVec.back().meshHandle = _meshHandle;
			}
		}

		m_pRenderDevice->SubmitDirectCommandList(_pCmdList);

		// 処理が終われば命令を解放
		_initRequestVec.clear();
	}
	//------------------------------------------------------------------------------------------
	// テクスチャのSRV番号を引く。引けなければ -1
	//
	// シェーダーは負の番号を「テクスチャ無し」として扱う(MeshGBufferPS など)。
	// 次のどれでも -1 を返し、落とさずに素の値で描かせる。
	//   ・テクスチャを持たないマテリアル(法線マップ無しなど)
	//   ・非同期ロード中 : スロットには空の実体が入っていて、SRVはまだ無い
	//   ・読み込み失敗
	// 以前は見つからないと null を参照していた(Shipping では ERRLOG が消えるので素通りする)
	//------------------------------------------------------------------------------------------
	int DrawSubmitter::GetSRVIndexFromTextureHandle(const Handle<Resource::Texture>& a_texHandle) const
	{
		auto& _resManager = (*m_pResourceManager);
		if (!_resManager.IsReady(a_texHandle)) return -1;

		const auto* _pTex = _resManager.Get(a_texHandle);
		if (!_pTex) return -1;

		const auto& _srv = _pTex->GetSRV();
		if (!_srv.IsValid()) return -1;

		return static_cast<int>(_srv.GetIndex());
	}

	// シェーディングモデルはもう引かない。
	// 以前はここで解決できないと描画コマンドごと捨てていたので、
	// シェーディングモデルを持たないマテリアルは何も描かれなかった
	bool DrawSubmitter::FetchDrawResources(
		const Resource::ModelDrawCommand& a_cmd,
		const Resource::Mesh*& a_pOutMesh,
		const Resource::Material*& a_pOutMaterial)
	{
		auto& _resManager = (*m_pResourceManager);

		// メッシュ
		a_pOutMesh = _resManager.Get(a_cmd.meshHandle);
		if (!a_pOutMesh) return false;

		// マテリアル
		a_pOutMaterial = _resManager.Get(a_cmd.materialHandle);
		if (!a_pOutMaterial) return false;

		return true;
	}

	MeshMaterial DrawSubmitter::BuildMeshMaterial(
		const Resource::Material* a_pMaterial,
		const Math::Color& a_albedoScale,
		const Math::Vector3& a_emissiveScale,
		const Math::Vector3& a_emissiveAdd)
	{
		MeshMaterial _meshMaterial = {};
		_meshMaterial.baseColor = a_pMaterial->baseColor * a_albedoScale;
		_meshMaterial.emissive = a_pMaterial->emissive * a_emissiveScale;
		_meshMaterial.emissiveAdd = a_emissiveAdd;
		_meshMaterial.metallic = a_pMaterial->metallic;
		_meshMaterial.roughness = a_pMaterial->roughness;
		_meshMaterial.albedoIndex = GetSRVIndexFromTextureHandle(a_pMaterial->baseColorTex);
		_meshMaterial.metaRoughnessIndex = GetSRVIndexFromTextureHandle(a_pMaterial->metaRoughTex);
		_meshMaterial.emissiveIndex = GetSRVIndexFromTextureHandle(a_pMaterial->emissiveTex);
		_meshMaterial.normalIndex = GetSRVIndexFromTextureHandle(a_pMaterial->normalTex);
		return _meshMaterial;
	}

	void DrawSubmitter::RegisterDrawCommandToPasses(
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
		PSOKey a_psoKey)
	{
		// マテリアルの透明モードで、どちらのキューへ流すかを決める。
		// Mask はアルファで抜くだけで前後関係は不透明と同じ扱いなので Opaque
		const EGeometryQueue _queue = (a_pMaterial->alphaMode == Resource::EAlpha::Blend)
			? EGeometryQueue::Transparent
			: EGeometryQueue::Opaque;
		RegisterDrawCommandToPasses(
			_queue, a_cmd, a_pMesh, a_pMaterial, 
			a_mat, a_prevMat, a_isAnimation, a_animatedVertexStart, 
			a_albedoScale, a_emissiveScale, a_emissiveAdd, a_psoKey
		);
	}

	void DrawSubmitter::RegisterDrawCommandToPasses(const EGeometryQueue& a_eGeoQue, const Resource::ModelDrawCommand& a_cmd, const Resource::Mesh* a_pMesh, const Resource::Material* a_pMaterial, const Math::Matrix& a_mat, const Math::Matrix& a_prevMat, bool a_isAnimation, uint32_t a_animatedVertexStart, const Math::Color& a_albedoScale, const Math::Vector3& a_emissiveScale, const Math::Vector3& a_emissiveAdd, PSOKey a_psoKey)
	{
		const EGeometryQueue _queue = a_eGeoQue;

		const auto& _msData = a_pMesh->GetMeshShaderData();
		const auto& _subsetMeshlet = _msData.subsetMeshlets[a_cmd.subIdx];
		const bool _isTransparent = (_queue == EGeometryQueue::Transparent);

		//------------------------------------------------------------------------------------------
		// マテリアルとインスタンスのデータは、パスをまたいで1つを共有する
		//
		// どちらもパスに依存する値を持たない(ワールド行列・メッシュレットの位置・マテリアル値だけ)。
		// パスごとに作ると、同じ中身がパスの数 × カメラの数だけGPUバッファへ積まれ、
		// テクスチャのSRV番号も同じ回数だけ引き直すことになる。
		// 作るのは最初にアイテムを積めたパスのときだけ(PSOが無くて1つも積めなければ作らない)
		//------------------------------------------------------------------------------------------
		constexpr UINT NOT_CREATED = UINT_MAX;
		UINT _meshInstanceIndex = NOT_CREATED;
		auto _acquireInstanceIndex = [&]() -> UINT
			{
				if (_meshInstanceIndex != NOT_CREATED) return _meshInstanceIndex;

				const MeshMaterial _meshMaterial = BuildMeshMaterial(a_pMaterial, a_albedoScale, a_emissiveScale, a_emissiveAdd);

				MeshInstanceData _meshInstanceData = {};
				_meshInstanceData.worldMat = a_mat.Transpose();
				_meshInstanceData.prevWorldMat = a_prevMat.Transpose();
				_meshInstanceData.materialOffset = m_pDrawLists->AddMeshMaterial(_meshMaterial);
				_meshInstanceData.meshletOffset = _msData.meshletHandle.startIndex + _subsetMeshlet.meshletOffset;
				_meshInstanceData.vertexOffset = a_pMesh->GetRtData().vertexHandle.startIndex;
				_meshInstanceData.uviOffset = _msData.uniqueVertexIndicesHandle.startIndex;
				_meshInstanceData.primitiveOffset = _msData.primitiveIndicesHandle.startIndex;
				_meshInstanceData.cullStart = _msData.cullDataHandle.startIndex + _subsetMeshlet.cullOffset;
				_meshInstanceData.meshletCount = _subsetMeshlet.meshletCount;
				_meshInstanceData.animatedVertexStart = a_animatedVertexStart;
				_meshInstanceData.isAnimated = a_isAnimation ? 1 : 0;

				_meshInstanceIndex = m_pDrawLists->AddInstanceData(_meshInstanceData);
				return _meshInstanceIndex;
			};

		// モデルを受け取るパスへアイテムを流す。
		// パスごとにPSOもパス番号も違うので、アイテム自体はパスの数だけ積む
		for (auto* _pPipelinePass : m_pCameraPipelines->GetGeometryPasses(_queue))
		{
			if (!_pPipelinePass) continue;

			PSOKey _pipelineKey = a_psoKey;
			_pipelineKey.permutationFlags |= static_cast<uint32_t>(Engine::Graphics::EShaderPermutationFlags::MeshShader);
			_pipelineKey.psHandle = _pPipelinePass->GetDefaultPSHandle();

			auto _psoHandle = _pPipelinePass->RefPipelineBuilder().Request(_pipelineKey, m_pPipelineStateManager, *m_pResourceManager);

			// PSOを作れなかったアイテムは積まない : 描くときに引く先が無い。
			//
			// 番号そのものはハンドルと同じ16bitをソートキーに持たせてあるので、
			// もう「収まらない」ことは起きない。弾くのは無効ハンドルだけ。
			// ここは1フレームに何万回も通るので警告は出さない。
			// 理由(シェーダーがまだ読めていない等)は PipelineStateManager が
			// PSOごとに1回だけ知らせている
			if (!_psoHandle.IsValid()) continue;

			Engine::Graphics::LightWeightDrawItem _item = {};
			_item.meshHandle = a_cmd.meshHandle;
			_item.materialHandle = a_cmd.materialHandle;
			_item.isAnimation = a_isAnimation;
			_item.subIndex = a_cmd.subIdx;
			_item.meshInstanceIndex = _acquireInstanceIndex();
			_item.subsetMeshletCount = _subsetMeshlet.meshletCount;
			_item.psoID = _psoHandle.GetIndex();

			// ソートキー。
			// 半透明は奥から手前へ描かないと重なりが崩れるので、深さで並べる。
			// 深さはカメラが確定してから(SortItems の直前に)決めるので、ここでは位置だけ控える
			if (_isTransparent)
			{
				_item.isTransparent = true;
				_item.sortPos = { a_mat._41, a_mat._42, a_mat._43 };
				_item.sortKey.transparentBits.psoID = _psoHandle.GetIndex();
				_item.sortKey.transparentBits.passIndex = _pPipelinePass->GetPassIndex();
			}
			else
			{
				_item.sortKey.bits.subIndex = a_cmd.subIdx;
				_item.sortKey.bits.meshID = a_cmd.meshHandle.GetIndex();
				_item.sortKey.bits.materialID = a_cmd.materialHandle.GetIndex();
				_item.sortKey.bits.psoID = _psoHandle.GetIndex();
				_item.sortKey.bits.passIndex = _pPipelinePass->GetPassIndex();
			}

			m_pDrawLists->AddItem(_item);
		}
	}

	void DrawSubmitter::PushUIData(
		uint32_t a_texIndex,
		const Math::Vector2& a_pixelPos,
		const Math::Vector2& a_pixelSize,
		const Math::Color& a_color,
		float a_rotationDeg,
		float a_layer,
		const Math::Vector2& a_uvOffset,
		const Math::Vector2& a_pivot,
		const Math::Vector2& a_uvScale,
		float a_curveK,
		float a_curveOffsetX,
		const UIClipRect* a_pClipRect,
		UINT a_postEffectFlags
	)
	{
		// スクリーン解像度(px)
		const float _w = static_cast<float>(m_renderWidth);
		const float _h = static_cast<float>(m_renderHeight);
		if (_w <= 0.0f || _h <= 0.0f) return;

		// 回転(度→ラジアン)。回転はピクセル空間(等方)で行い、そのあとNDCへ変換する。
		// こうしないと、NDC空間(x,yで縮尺が違う)で回転させたときに斜めで画像が歪む。
		const float _rad = DirectX::XMConvertToRadians(a_rotationDeg);
		const float _cos = std::cos(_rad);
		const float _sin = std::sin(_rad);

		// 半サイズ(px)と、ピボット(正規化[0,1])からクアッド中心までのオフセット(px)。
		// (0.5 - pivot) * size がクアッド中心のピボットからのずれ。
		const Math::Vector2 _halfPx = { a_pixelSize.x * 0.5f, a_pixelSize.y * 0.5f };
		const Math::Vector2 _pivotOffPx = {
			(0.5f - a_pivot.x) * a_pixelSize.x,
			(0.5f - a_pivot.y) * a_pixelSize.y
		};

		// クアッド頂点 q∈[-1,1] に対し、ピクセル空間での最終座標は
		//   finalPx = centerPx + q.x*axisXpx + q.y*axisYpx
		// 回転はピボットを中心に行うので、centerPx = ピボット位置 + R*ピボットオフセット。
		//
		// ベースクアッドのUVは q.x=+1 がテクスチャ右、q.y=+1 がテクスチャ上(v=0)。
		// よって未回転時、ローカル+Xは画面右(+pixelX)、ローカル+Yは画面上(-pixelY)を向く。
		// この基底(+X=右, +Y=上)を回転行列 R(θ) で回す。
		//   axisXpx = R*( halfX,      0) = ( halfX*cos, halfX*sin)
		//   axisYpx = R*(     0, -halfY) = ( halfY*sin,-halfY*cos)
		const Math::Vector2 _centerPx = {
			a_pixelPos.x + (_pivotOffPx.x * _cos - _pivotOffPx.y * _sin),
			a_pixelPos.y + (_pivotOffPx.x * _sin + _pivotOffPx.y * _cos)
		};
		const Math::Vector2 _axisXpx = { _halfPx.x * _cos,  _halfPx.x * _sin };
		const Math::Vector2 _axisYpx = { _halfPx.y * _sin, -_halfPx.y * _cos };

		// ピクセル(左上原点/Y下向き) → NDC(中心原点/Y上向き)。
		// 点は原点シフトあり、方向ベクトルはスケールのみ(Yは符号反転)。
		UIData _data = {};
		_data.pos   = { _centerPx.x / _w * 2.0f - 1.0f, 1.0f - _centerPx.y / _h * 2.0f };
		_data.axisX = { _axisXpx.x * 2.0f / _w, -_axisXpx.y * 2.0f / _h };
		_data.axisY = { _axisYpx.x * 2.0f / _w, -_axisYpx.y * 2.0f / _h };
		_data.uvOffset = a_uvOffset;
		_data.uvScale = a_uvScale;
		_data.color = Math::DX::ToVector4(a_color);
		_data.layer = a_layer;
		_data.texIndex = a_texIndex;
		// 湾曲。
		// 反りは「弧の中心からの横ずれ(px)」で決まるので、シェーダーが px へ戻せるように
		// このクアッドの実寸(半分の大きさ)も一緒に送る。
		// NDCの基底(axisX/axisY)からは画面解像度なしにpxを復元できないため
		_data.curveK = a_curveK;
		_data.curveOffsetX = a_curveOffsetX;
		_data.curveHalfWidth = _halfPx.x;
		_data.curveInvHalfHeight = (_halfPx.y > 0.0f) ? (1.0f / _halfPx.y) : 0.0f;

		// 切り抜き。点と同じくピクセル → NDC へ直す。
		// Y は上下が入れ替わるので、ピクセルの下端(max.y)が NDC の下端(minY)になる
		if (a_pClipRect)
		{
			_data.clipRect = {
				a_pClipRect->min.x / _w * 2.0f - 1.0f,
				1.0f - a_pClipRect->max.y / _h * 2.0f,
				a_pClipRect->max.x / _w * 2.0f - 1.0f,
				1.0f - a_pClipRect->min.y / _h * 2.0f
			};
		}

		_data.postEffectFlags = a_postEffectFlags;

		m_pDrawLists->AddUI(_data);
	}
}
