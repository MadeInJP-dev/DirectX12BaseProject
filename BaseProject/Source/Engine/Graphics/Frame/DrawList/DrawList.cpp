#include "Engine/Graphics/Frame/DrawList/DrawList.h"

#include "Engine/ECS/World/World.h"

namespace Engine::Graphics
{
	namespace
	{
		// vector を空にしつつ、次フレームぶんの容量を確保し直す
		template<typename T>
		void ClearAndReserve(std::vector<T>& a_vec, size_t a_reserveCount)
		{
			a_vec.clear();
			a_vec.reserve(a_reserveCount);
		}
	}

	void DrawLists::Clear()
	{
		// 描画命令
		ClearAndReserve(m_lightWeightDrawItemVec, 10000);
		ClearAndReserve(m_drawInstanceIndexVec, 10000);
		ClearAndReserve(m_uiDrawItemVec, 10000);
		ClearAndReserve(m_dynamicRayRequestVec, 1000);
		ClearAndReserve(m_skinningDispatchItemVec, 1000);

		// ボーンパレットと、ワールドごとの土台の対応表
		ClearAndReserve(m_boneMatrixVec, 10000);
		m_boneBaseIndexMap.clear();

		// オブジェクトデータ
		ClearAndReserve(m_meshInstanceDataVec, 10000);

		// サブセット情報
		ClearAndReserve(m_meshMaterialDataVec, 10000);

		// LOD の表
		ClearAndReserve(m_meshLodDataVec, 10000);
	}

	//==========================================================================================
	// モデルの描画アイテム
	//==========================================================================================
	void DrawLists::AddItem(const LightWeightDrawItem& a_item)
	{
		m_lightWeightDrawItemVec.push_back(a_item);
	}

	void DrawLists::ResolveTransparentSortKeys(const Math::Vector3& a_cameraPos)
	{
		for (auto& _item : m_lightWeightDrawItemVec)
		{
			if (!_item.isTransparent) continue;

			//--------------------------------------------------------------------------------------
			// 距離の2乗を、そのまま並べ替えに使える整数へ直す
			//
			// 0 以上の float はビット列を符号なし整数として見ても大小が変わらない。
			// ビットを反転すれば「遠いほど小さい」になり、昇順に並べると奥から描かれる。
			// 距離そのものではなく2乗で比べる(平方根は大小を変えないので要らない)。
			// 深さはオブジェクトの原点で測る : メッシュを貫くような大きい半透明は
			// 並びが甘くなることがあるが、オブジェクト単位で奥から描く目的には足りる
			//--------------------------------------------------------------------------------------
			const Math::Vector3 _toItem = _item.sortPos - a_cameraPos;
			const float _distSq = (std::max)(_toItem.LengthSquared(), 0.0f);
			const uint32_t _bits = std::bit_cast<uint32_t>(_distSq);
			const uint32_t _farFirst = static_cast<uint32_t>(~_bits);

			_item.sortKey.transparentBits.farFirstDepth = _farFirst;
		}
	}

	void DrawLists::SortItems()
	{
		std::sort(
			m_lightWeightDrawItemVec.begin(), m_lightWeightDrawItemVec.end(),
			[](const LightWeightDrawItem& a, const LightWeightDrawItem& b)
			{
				return a.sortKey.value < b.sortKey.value;
			}
		);

		// 描く順のインスタンス番号の表を、並べ終えた順で作り直す
		m_drawInstanceIndexVec.clear();
		m_drawInstanceIndexVec.reserve(m_lightWeightDrawItemVec.size());
		for (const auto& _item : m_lightWeightDrawItemVec)
		{
			m_drawInstanceIndexVec.push_back(_item.meshInstanceIndex);
		}
	}

	std::span<const LightWeightDrawItem> DrawLists::GetPassItems(uint8_t a_passIndex, UINT* a_pOutFirstIndex) const
	{
		// 探したいパスのキーの最小値と最大値を求める。
		// パス番号は RenderSortKey の最上位8bit(56〜63)に置いてある
		constexpr uint32_t PASS_INDEX_SHIFT = 56;
		uint64_t _minKey = static_cast<uint64_t>(a_passIndex) << PASS_INDEX_SHIFT;
		uint64_t _maxKey = _minKey | ((1ull << PASS_INDEX_SHIFT) - 1ull); // 下位56ビットをすべて1にする

		// ソート済み配列から開始位置を見つける
		auto _itStart = std::lower_bound(
			m_lightWeightDrawItemVec.begin(),
			m_lightWeightDrawItemVec.end(),
			_minKey,
			[](const LightWeightDrawItem& a_item, uint64_t a_value)
			{
				return a_item.sortKey.value < a_value;
			}
		);

		// ソート済み配列から終了位置を見つける
		auto _itEnd = std::upper_bound(
			_itStart,		// 開始位置から探す
			m_lightWeightDrawItemVec.end(),
			_maxKey,
			[](uint64_t a_value, const LightWeightDrawItem& a_item)
			{
				return a_value < a_item.sortKey.value;
			}
		);

		if (a_pOutFirstIndex)
		{
			*a_pOutFirstIndex = static_cast<UINT>(std::distance(m_lightWeightDrawItemVec.begin(), _itStart));
		}

		return std::span<const LightWeightDrawItem>(_itStart, _itEnd);
	}

	//==========================================================================================
	// メッシュシェーダー用
	//==========================================================================================
	UINT DrawLists::AddInstanceData(const MeshInstanceData& a_instanceData)
	{
		UINT _index = static_cast<UINT>(m_meshInstanceDataVec.size());
		m_meshInstanceDataVec.push_back(a_instanceData);
		return _index;
	}

	bool DrawLists::AddMeshLodData(std::span<const MeshLodData> a_levels, UINT& a_outStart)
	{
		if (m_meshLodDataVec.size() + a_levels.size() > MESH_LOD_DATA_CAPACITY) return false;

		a_outStart = static_cast<UINT>(m_meshLodDataVec.size());
		m_meshLodDataVec.insert(m_meshLodDataVec.end(), a_levels.begin(), a_levels.end());
		return true;
	}

	UINT DrawLists::AddMeshMaterial(const MeshMaterial& a_meshMaterial)
	{
		UINT _index = static_cast<UINT>(m_meshMaterialDataVec.size());
		m_meshMaterialDataVec.push_back(a_meshMaterial);
		return _index;
	}

	//==========================================================================================
	// UI
	//==========================================================================================
	void DrawLists::AddUI(const UIData& a_data)
	{
		m_uiDrawItemVec.push_back(a_data);
	}

	void DrawLists::SortUIByLayer()
	{
		// 小さいものから描く = 大きいほど手前。
		// 同じ値のものは積んだ順を崩さないよう stable_sort を使う
		// (1つのUIが持つ飾りは配列順で重なっているため、崩すと絵が入れ替わる)
		std::stable_sort(
			m_uiDrawItemVec.begin(), m_uiDrawItemVec.end(),
			[](const UIData& a, const UIData& b)
			{
				return a.layer < b.layer;
			}
		);
	}

	//==========================================================================================
	// GPUスキニング / レイトレ
	//==========================================================================================
	void DrawLists::AddSkinning(const SkinningDispatchItem& a_item)
	{
		m_skinningDispatchItemVec.push_back(a_item);
	}

	void DrawLists::AddDynamicRayRequest(const Raytracing::DynamicRaytracingRequest& a_request)
	{
		m_dynamicRayRequestVec.push_back(a_request);
	}

	//======================================================================================
	// ボーンパレットへこのワールドのボーン行列を積む
	//--------------------------------------------------------------------------------------
	// ボーン行列はワールド(シーン)ごとの RangePool に入っていて、その添字も
	// ワールドごとに 0 から始まる。GPU側のボーンパレットは1本しかないので、
	// 複数のシーンを重ねて描くときは連結したうえで土台を足してやる必要がある。
	//
	// ポーズ画面はゲームのシーンへ重ねて出す(Push)ので、描くワールドが2つになる。
	// ここで両方を積んでおかないと、片方のキャラのボーンが単位行列でも他人のものでもない
	// 場所を指し、頂点が一点に潰れて消えたように見える。
	//
	// 呼ばれるのは描画フェーズ(GameManager::Draw)の中で、ボーン行列自体は
	// それより前の Animation フェーズで確定しているので、この時点の値で正しい。
	//======================================================================================
	uint32_t DrawLists::AcquireBoneBaseIndex(ECS::World& a_world)
	{
		// 同じワールドをこのフレームで既に積んでいればその位置を返す
		auto _it = m_boneBaseIndexMap.find(&a_world);
		if (_it != m_boneBaseIndexMap.end()) return _it->second;

		const uint32_t _baseIndex = static_cast<uint32_t>(m_boneMatrixVec.size());

		if (a_world.HasResource<Pool::RangePool<Resource::BoneMatrix>>())
		{
			auto& _boneMatPool = a_world.RefResource<Pool::RangePool<Resource::BoneMatrix>>();

			// プールは最初から10000要素ぶん確保されているので、丸ごと積むと
			// ワールドを2つ重ねただけでGPU側のボーンパレットが溢れる。
			// 実際に使われている末尾までで足りる(ハンドルの添字は必ずこの内側)
			const auto& _data = _boneMatPool.GetAllData();
			const size_t _usedCount = (std::min)(
				static_cast<size_t>(_boneMatPool.GetUsedCount()), _data.size());

			m_boneMatrixVec.insert(m_boneMatrixVec.end(), _data.begin(), _data.begin() + _usedCount);
		}

		m_boneBaseIndexMap.emplace(&a_world, _baseIndex);
		return _baseIndex;
	}
}
