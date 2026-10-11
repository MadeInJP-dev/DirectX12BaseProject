#include "Mesh.h"


#include "../../../Resource/Manager/ResourceManager/ResourceManager.h"

#include "../../../MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/MeshBufferAllocator/MeshBufferAllocator.h"

bool Engine::Resource::Mesh::CreateFloat(
	const ResourceBuildContext& a_ctx,
	const std::vector<MeshVertexFloat>& a_vertices,
	const std::vector<MeshFace>& a_face,
	const std::vector<MeshSubset>& a_subsets,
	bool a_isSkinMesh
)
{
	auto* _pMeshBufferAllocator = a_ctx.pMeshBufferAllocator;
	if (!_pMeshBufferAllocator)
	{
		ENGINE_ERRLOG(false, "メッシュ作成時にメッシュバッファアロケーターがコンテキストに設定されていません");
		return false;
	}
	if (!a_ctx.CanRecordCopy())
	{
		ENGINE_ERRLOG(false, "メッシュ作成時にコピーコマンドリストがコンテキストに設定されていません");
		return false;
	}

	// 自分のメンバへ移す : 以降は引数ではなくメンバを参照する
	// (呼び出し元がメンバを渡してくることがあるため、自己代入になる場合はスキップ)
	if (&m_vertices != &a_vertices)
	{
		m_vertices = a_vertices;
		m_face = a_face;
		m_subsets = a_subsets;
	}
	m_isSkinMesh = a_isSkinMesh;

	// インデックス配列作成
	std::vector<uint32_t> _indices;
	_indices.reserve(m_face.size() * 3);
	for (const auto& _f : m_face)
	{
		_indices.push_back(_f.idx[0]);
		_indices.push_back(_f.idx[1]);
		_indices.push_back(_f.idx[2]);
	}

	// メタデータ作成
	CreateMeshMetaData(m_vertices, m_subsets, m_isSkinMesh);

	// ラスタライザーデータ(IASetVertexBuffers用の個別VB/IB)はここでは作らない。
	// モデルの描画経路はメッシュシェーダーとレイトレだけで、どちらもメガバッファを
	// 直接読む。中身はメガバッファへ入れた頂点/インデックスと同一なので、
	// ここで作るとロードした全ジオメトリの未使用コピーがUPLOADヒープに常駐する。
	//
	// 作る経路は CreateRasterData() として残してある。IAを使うパスを足すときは、
	// そのパスが使うメッシュに対してだけ呼ぶこと。
	// (パーティクルとUIのポリゴンインスタンシングはこれを使わない。
	//  共有のクアッドを RenderContext が持っていて、そちらを張る)

	// レイトレデータ : メガバッファへの転送コマンドを積む
	m_opRtData.emplace();
	m_opRtData->vertexHandle = _pMeshBufferAllocator->AllocateVertex(a_ctx, m_vertices);

	// ボーン割り当てはスキンメッシュだけが持つ(スキニングのときだけ読む)
	if (m_isSkinMesh)
	{
		m_opRtData->skinVertexHandle = _pMeshBufferAllocator->AllocateSkinVertex(a_ctx, m_vertices);
	}
	m_opRtData->indexHandle = _pMeshBufferAllocator->AllocateIndex(a_ctx, _indices);

	// メッシュシェーダー用データ : メッシュレット生成と転送コマンドの記録
	m_opMeshShaderData.emplace();
	CreateMeshShaderData(a_ctx, m_vertices, m_face);

	// レイ用データ(BLAS)の構築コマンドを積む
	// メガバッファへの転送コマンドより後に積むこと
	if (a_ctx.CanRecordCompute())
	{
		CreateRtData(a_ctx, m_subsets);
	}

	return true;
}

void Engine::Resource::Mesh::CreateMeshMetaData(
	const std::vector<MeshVertexFloat>& a_vertices,
	const std::vector<MeshSubset>& a_subsets, 
	bool a_isSkinMesh
)
{
	m_meshMetaData.Create(a_vertices,a_subsets,a_isSkinMesh);
}

void Engine::Resource::Mesh::CreateRasterData(const ResourceBuildContext& a_ctx, const std::vector<MeshVertexFloat>& a_vertices, const std::vector<MeshFace>& a_face, DXGI_FORMAT a_indexFormat)
{
	auto& _raster = m_opRasterData.emplace();
	_raster.Create(a_ctx.pDevice, a_ctx.pHeapManager, a_vertices, a_face, a_indexFormat);
}

void Engine::Resource::Mesh::CreateRtData(
	const ResourceBuildContext& a_ctx,
	const std::vector<MeshSubset>& a_subset
)
{
	m_opRtData->Create(a_ctx, a_subset);
}

void Engine::Resource::Mesh::CreateMeshShaderData(
	const ResourceBuildContext& a_ctx,
	const std::vector<MeshVertexFloat>& a_vertices,
	const std::vector<MeshFace>& a_face
)
{
	// メッシュレット生成用パラメーター
	const size_t MAX_VERTS = 64;			// 最大頂点数
	const size_t MAX_PRIMS = 126;			// 最大プリミティブ数
	uint32_t _currentMeshletOffset = 0;
	uint32_t _currentVertexOffset = 0;
	uint32_t _currentPrimitiveOffset = 0;

	uint32_t _currentCullOffset = 0;

	// 全サブセットのデータをまとめるマスター配列
	std::vector<Engine::Resource::Meshlet> _masterMeshlets;
	std::vector<uint32_t> _masterUVI;
	std::vector<DirectX::MeshletTriangle> _masterPrimitives;
	std::vector<SubsetMeshletData> _subsetMeshletData;
	std::vector<DirectX::CullData> _cullData;

	// 位置情報だけの配列を作成
	std::vector<Math::Vector3> _positions;
	_positions.reserve(a_vertices.size());
	for (const auto& v : a_vertices)
	{
		_positions.push_back(v.pos);
	}

	// DirectXMesh は XMFLOAT3 の配列しか受け取らない。
	// Math::Vector3 とはバイナリ配置が同じ(static_assert 済み)なので、
	// 詰め替えずに先頭アドレスをそのまま渡す
	const DirectX::XMFLOAT3* _pPositions =
		reinterpret_cast<const DirectX::XMFLOAT3*>(_positions.data());

	for (const auto& _subset : m_meshMetaData.subsets)
	{
		std::vector<uint32_t> _subsetIndices;
		// このサブセットが持つ面だけを抽出
		for (uint32_t i = 0; i < _subset.faceCount; ++i)
		{
			auto& _f = a_face[_subset.faceStart + i];
			_subsetIndices.push_back(_f.idx[0]);
			_subsetIndices.push_back(_f.idx[1]);
			_subsetIndices.push_back(_f.idx[2]);
		}

		// サブセット単体で ComputeMeshlets を実行
		std::vector<DirectX::Meshlet> _dxMeshlets;
		std::vector<uint8_t> _uniqueVertexIB;
		std::vector<DirectX::MeshletTriangle> _primitiveIndices;

		// メッシュレット生成
		auto _hr = DirectX::ComputeMeshlets(
			_subsetIndices.data(), 
			_subsetIndices.size() / 3,
			_pPositions, 
			_positions.size(),
			nullptr,
			_dxMeshlets, 
			_uniqueVertexIB,
			_primitiveIndices,
			MAX_VERTS, MAX_PRIMS
		);
		if (FAILED(_hr))
		{
			ENGINE_ERRLOG(false, "Meshletの生成に失敗");
			return;
		}

		// 前回のサブセットまでのオフセットを足してマスター配列に追加
		for (const auto& _dxm : _dxMeshlets)
		{
			Engine::Resource::Meshlet _m = {};
			_m.vertexCount = _dxm.VertCount;
			_m.vertexOffset = _dxm.VertOffset + _currentVertexOffset;
			_m.primitiveCount = _dxm.PrimCount;
			_m.primitiveOffset = _dxm.PrimOffset + _currentPrimitiveOffset;
			_masterMeshlets.push_back(_m);
		}

		// ---------------------------------------------------------
		// uniqueVertexIndices を uint32_t に統一して変換
		// ---------------------------------------------------------
		std::vector<uint32_t> _convertedUVI;
		// 最初から32bitで出力されているのでそのままコピー
		const uint32_t* _p32 = reinterpret_cast<const uint32_t*>(_uniqueVertexIB.data());
		size_t _indexCount = _uniqueVertexIB.size() / sizeof(uint32_t);

		_convertedUVI.assign(_p32, _p32 + _indexCount);

		// ---------------------------------------------------------
		// Meshlet単位で当たり判定を付与
		// ---------------------------------------------------------
		std::vector<DirectX::CullData> _subsetCullData(_dxMeshlets.size());
		_hr = DirectX::ComputeCullData(
			_pPositions,				// 頂点座標の配列 : 全頂点
			_positions.size(),			// 頂点座標の総数
			_dxMeshlets.data(),			// 生成されたサブセットのメッシュレット
			_dxMeshlets.size(),			// メッシュレット数
			_p32,						// 32bit化された UVI 配列
			_indexCount,				// UVI の要素数
			_primitiveIndices.data(),	// プリミティブ配列
			_primitiveIndices.size(),	// プリミティブ数
			_subsetCullData.data()		// 出力先
		);
		if (FAILED(_hr))
		{
			ENGINE_ERRLOG(false,"CullDataの生成に失敗");
			return;
		}
		// 生成した CullData をマスター配列に結合
		_cullData.insert(_cullData.end(), _subsetCullData.begin(), _subsetCullData.end());

		// 変換した配列をマスター配列の後ろに結合
		_masterUVI.insert(_masterUVI.end(), _convertedUVI.begin(), _convertedUVI.end());
		_masterPrimitives.insert(_masterPrimitives.end(), _primitiveIndices.begin(), _primitiveIndices.end());

		// 描画システムに渡すためのサブセットごとのカタログ情報を記録
		SubsetMeshletData _smd = {};
		_smd.meshletOffset = _currentMeshletOffset;
		_smd.meshletCount = static_cast<uint32_t>(_dxMeshlets.size());
		_smd.cullOffset = _currentCullOffset;
		_smd.cullCount = static_cast<uint32_t>(_subsetCullData.size());
		_subsetMeshletData.push_back(_smd);

		// 次のサブセットのためにオフセットを進める
		_currentMeshletOffset += static_cast<uint32_t>(_dxMeshlets.size());
		_currentVertexOffset += static_cast<uint32_t>(_convertedUVI.size());
		_currentPrimitiveOffset += static_cast<uint32_t>(_primitiveIndices.size());
		_currentCullOffset += static_cast<uint32_t>(_subsetCullData.size());
	}

	// すべてのループが終わったら、構造体にマスター配列をムーブ
	auto& _meshShaderData = m_opMeshShaderData.value();
	_meshShaderData.meshlets = std::move(_masterMeshlets);
	_meshShaderData.uniqueVertexIndices = std::move(_masterUVI);
	_meshShaderData.primitiveIndices = std::move(_masterPrimitives);
	_meshShaderData.subsetMeshlets = std::move(_subsetMeshletData);
	_meshShaderData.cullData = std::move(_cullData);

	// メガバッファへの転送コマンドを積む
	auto* _pMeshBufferAllocator = a_ctx.pMeshBufferAllocator;
	if (!_pMeshBufferAllocator) return;

	_meshShaderData.meshletHandle =
		_pMeshBufferAllocator->AllocateMeshlet(a_ctx, _meshShaderData.meshlets);
	_meshShaderData.uniqueVertexIndicesHandle =
		_pMeshBufferAllocator->AllocateUniqueVertIndices(a_ctx, _meshShaderData.uniqueVertexIndices);
	_meshShaderData.primitiveIndicesHandle =
		_pMeshBufferAllocator->AllocateTriangles(a_ctx, _meshShaderData.primitiveIndices);
	_meshShaderData.cullDataHandle =
		_pMeshBufferAllocator->AllocateCullData(a_ctx, _meshShaderData.cullData);
}

void Engine::Resource::Mesh::Release()
{
	// ハンドルの登録
	auto* _pGE = MainEngine::Instance().RefGraphicsEngine();
	ENGINE_ERRLOG(_pGE,"メッシュ解放時にGraphicsEngineが存在しません");

	// メガバッファにアロケート
	auto* _pMeshBufferAllocator = _pGE->RefMeshBufferAllocator();
	ENGINE_ERRLOG(_pMeshBufferAllocator, "メッシュ解放時にメッシュバッファアロケーターが存在しません");

	// 登録されているハンドルの解放
	// 各ドメインデータは必要なもののみ実体化されるため、持っているものだけ解放する
	if (m_opRtData.has_value())
	{
		_pMeshBufferAllocator->ReserveFreeStaticVertex(m_opRtData->vertexHandle);
		_pMeshBufferAllocator->ReserveFreeSkinVertex(m_opRtData->skinVertexHandle);
		_pMeshBufferAllocator->ReserveFreeIndex(m_opRtData->indexHandle);
	}

	if (m_opMeshShaderData.has_value())
	{
		_pMeshBufferAllocator->ReserveFreeMeshlet(m_opMeshShaderData->meshletHandle);
		_pMeshBufferAllocator->ReserveFreeUniqueVertIndices(m_opMeshShaderData->uniqueVertexIndicesHandle);
		_pMeshBufferAllocator->ReserveFreeTriangles(m_opMeshShaderData->primitiveIndicesHandle);
		_pMeshBufferAllocator->ReserveFreeMeshletCullData(m_opMeshShaderData->cullDataHandle);
	}

	// 各meshデータ解放
	m_meshMetaData.Release();
	if (m_opRasterData.has_value())
	{
		m_opRasterData->Release();
	}
	if (m_opRtData.has_value())
	{
		m_opRtData->Release();
	}
	ENGINE_LOG("メッシュの解放 : Release");
}

void Engine::Resource::Mesh::Save(const std::string& a_fileDir, const std::string& a_name)
{
	Persistence::Archive _ar(Persistence::Archive::EMode::Save, a_fileDir, a_name, "mesh");

	// 頂点数の保存
	size_t _vertexCount = m_vertices.size();
	_ar.Field("VertexCount", _vertexCount);

	// 頂点データの保存
	int _v = 0;
	for (auto& _vert : m_vertices)
	{
		std::string _vStr = "Vert[" + std::to_string(_v) + "].";

		_ar.Field(_vStr + "Pos", _vert.pos);
		_ar.Field(_vStr + "Normal", _vert.normal);
		_ar.Field(_vStr + "UV", _vert.uv);
		_ar.Field(_vStr + "Tangent", _vert.tangent);
		_ar.Field(_vStr + "Color", _vert.color);

		int _i = 0;
		for (auto& _skIdx : _vert.skinIndexList)
		{
			_ar.Field(_vStr + "SkList" + std::to_string(_i), _skIdx);
			_i++;
		}
		_i = 0;
		for (auto& _skWeit : _vert.skinWeightList)
		{
			_ar.Field(_vStr + "SkWeit" + std::to_string(_i), _skWeit);
			_i++;
		}
		_v++;
	}

	// 面数の保存
	size_t _faceCount = m_face.size();
	_ar.Field("FaceCount", _faceCount);

	// 面データの保存
	int _i = 0;
	for (auto& _face : m_face)
	{
		int _j = 0;
		for (auto& _idx : _face.idx)
		{
			_ar.Field("Face" + std::to_string(_i) + "_" + std::to_string(_j), _idx);
			_j++;
		}
		_i++;
	}

	// サブセット数の保存
	size_t _subsetCount = m_subsets.size();
	_ar.Field("SubsetCount", _subsetCount);

	// サブセットの保存
	_i = 0;
	for (auto& _subset : m_subsets)
	{
		std::string _iStr = std::to_string(_i);
		_ar.Field("Subset_MaterialNumber_" + _iStr, _subset.materialNumber);
		_ar.Field("Subset_faceStart_" + _iStr, _subset.faceStart);
		_ar.Field("Subset_faceCount_" + _iStr, _subset.faceCount);
		_i++;
	}

	_ar.Field("IsSkinMesh", m_isSkinMesh);

	// 以前はこの後ろに当たり判定用の三角形(と、さらに前は BVH)を書いていた。
	// 物理空間は上の頂点と面から形状を作るので、今は書かない。
	// 古いファイルの末尾に残っている分は、読み込みで読まれずに無視される。
	// ここより後ろにフィールドを足すなら、先にすべての .mesh を再変換すること
	// (古いファイルの末尾の残骸を読んでしまう)
}

void Engine::Resource::Mesh::Load(const ResourceBuildContext& a_ctx, const std::string& a_fileDir, const std::string& a_name)
{
	Persistence::Archive _ar(Persistence::Archive::EMode::Load, a_fileDir, a_name, "mesh");

	// 頂点数を読み込んでリサイズ
	size_t _vertexCount = 0;
	_ar.Field("VertexCount", _vertexCount);
	m_vertices.resize(_vertexCount);

	// 頂点データの読み込み（Saveと完全に同じキー名にする）
	int _v = 0;
	for (auto& _vert : m_vertices)
	{
		std::string _vStr = "Vert[" + std::to_string(_v) + "].";

		_ar.Field(_vStr + "Pos", _vert.pos);
		_ar.Field(_vStr + "Normal", _vert.normal);
		_ar.Field(_vStr + "UV", _vert.uv);
		_ar.Field(_vStr + "Tangent", _vert.tangent);
		_ar.Field(_vStr + "Color", _vert.color);

		int _i = 0;
		for (auto& _skIdx : _vert.skinIndexList)
		{
			_ar.Field(_vStr + "SkList" + std::to_string(_i), _skIdx); // std::to_stringに修正
			_i++;
		}
		_i = 0;
		for (auto& _skWeit : _vert.skinWeightList)
		{
			_ar.Field(_vStr + "SkWeit" + std::to_string(_i), _skWeit); // std::to_stringに修正
			_i++;
		}
		_v++;
	}

	// 面数を読み込んでリサイズ
	size_t _faceCount = 0;
	_ar.Field("FaceCount", _faceCount);
	m_face.resize(_faceCount);

	// 面データの読み込み
	int _i = 0;
	for (auto& _face : m_face)
	{
		int _j = 0;
		for (auto& _idx : _face.idx)
		{
			_ar.Field("Face" + std::to_string(_i) + "_" + std::to_string(_j), _idx);
			_j++;
		}
		_i++;
	}

	// サブセット数を読み込んでリサイズ
	size_t _subsetCount = 0;
	_ar.Field("SubsetCount", _subsetCount);
	m_subsets.resize(_subsetCount);

	// サブセットの読み込み
	_i = 0;
	for (auto& _subset : m_subsets)
	{
		std::string _iStr = std::to_string(_i);
		_ar.Field("Subset_MaterialNumber_" + _iStr, _subset.materialNumber);
		_ar.Field("Subset_faceStart_" + _iStr, _subset.faceStart);
		_ar.Field("Subset_faceCount_" + _iStr, _subset.faceCount);
		_i++;
	}

	_ar.Field("IsSkinMesh", m_isSkinMesh);

	// 古いファイルはこの後ろに当たり判定用の三角形が残っているが、読まない(Save の説明を参照)

	// 読み込んだデータでGPUリソースを構築 : 実体はコンテキストのコマンドリストへ積まれる
	CreateFloat(
		a_ctx,
		m_vertices,
		m_face,
		m_subsets,
		m_isSkinMesh
	);
}

void Engine::Resource::Mesh::Load(const ResourceBuildContext& a_ctx, const std::string& a_filePath)
{
	auto _fileDir = Core::File::GetDirFromPath(a_filePath);
	auto _fileName = Core::File::GetFileNameWithoutExtension(a_filePath);

	Load(a_ctx, _fileDir, _fileName);
}

