#include "Engine/Graphics/RenderingPipeline/RenderGraph/Resource/VirtualResource/VirtualResource.h"

// CD3DX12_* のヘルパーはここだけで使う。
// プリコンパイル済みヘッダーへ置くと全翻訳単位に広がるため
#pragma warning(push, 0)
#include "d3dx12.h"
#pragma warning(pop)


// 占有サイズの見積もりと実体の生成で、同じ仕様書を通すために要る
#include "Engine/Resource/Data/Texture/IO/Creater/TextureCreater.h"

namespace Engine::Graphics::Pipeline
{
	void VirtualResource::SetupFromOutputSlot(const Slot& a_slot, UINT64 a_baseWidth, UINT a_baseHeight)
	{
		m_resourceID = a_slot.resourceID;
		m_name = a_slot.name;
		m_type = a_slot.type;
		m_format = a_slot.format;

		// スロットが言ってきた値は宣言としてそのまま持つ。
		// 0 は「解像度に従う」の意味なので、ここで潰してはいけない
		m_declWidth = a_slot.width;
		m_declHeight = a_slot.height;
		m_scale = a_slot.scale;

		m_baseWidth = a_baseWidth;
		m_baseHeight = a_baseHeight;

		m_usage = ToUsage(a_slot.accessType, true);

		m_clearColor = a_slot.clearColor;
		m_isClear = (a_slot.loadOp == ELoadOp::Clear);

		m_isImported = false;
		m_isTemporal = a_slot.isTemporal;

		// 自前で作るリソースはフレームの入口では COMMON。
		// 毎フレーム同じ状態から始まるように、最後もここへ戻す
		m_initialState[0] = D3D12_RESOURCE_STATE_COMMON;
		m_initialState[1] = D3D12_RESOURCE_STATE_COMMON;
		ResetStateToInitial();

		// 区間は実行順が決まってから RenderGraph が入れる
		ResetLifetime();

		// 実サイズと占有サイズは、生まれたこの時点で出しておく。
		// 読み手が用途フラグを足すと変わりうるので、MergeSlot でも出し直す
		ResolveSizeAndAllocation();
	}

	void VirtualResource::SetupAsImported(const std::string& a_name, EPassSlotType a_type, D3D12_RESOURCE_STATES a_initialState)
	{
		// 外部リソースは作り手のパスが居ないので、識別子は名前から起こす。
		// 差し込む側とパスの出力ピンは、この名前で待ち合わせる
		m_resourceID = ResourceID::FromImportName(a_name);
		m_name = a_name;
		m_type = a_type;

		// 外部で作られた実体をそのまま使うので、こちらでサイズやフォーマットは決めない。
		// ヒープも食わないので占有サイズは 0 のまま
		m_format = DXGI_FORMAT_UNKNOWN;
		m_declWidth = 0;
		m_declHeight = 0;
		m_baseWidth = 0;
		m_baseHeight = 0;
		m_width = 0;
		m_height = 0;
		m_scale = 1.f;
		m_usage = Resource::ETextureUsage::None;

		m_allocationSize = 0;
		m_allocationAlignment = 0;

		m_clearColor = { 0.f, 0.f, 0.f, 1.f };
		m_isClear = false;

		m_isImported = true;

		// 外部リソースはフレームをまたいだ入れ替えをこちらで面倒見ない
		m_isTemporal = false;

		// 外部リソースは向こうが渡してきた状態で入ってくる
		m_initialState[0] = a_initialState;
		m_initialState[1] = a_initialState;
		ResetStateToInitial();

		// 区間は実行順が決まってから RenderGraph が入れる。
		// 外部リソースは使い回せないが、どこで触られているかは同じように分かる
		ResetLifetime();
	}

	void VirtualResource::MergeSlot(const Slot& a_slot)
	{
		const bool _isWrite = !a_slot.isIn;

		// 用途フラグは触ったぶんだけ足していく。
		// 「Aパスが書いて、B・Cパスが読む」なら RTV|SRV が立った状態になる
		m_usage = m_usage | ToUsage(a_slot.accessType, _isWrite);

		// 外部リソースは向こうが持っている実体が正なので、要件は上書きしない
		if (m_isImported) return;

		// どこか1つでも Temporal と言っていたら履歴つきとして扱う
		if (a_slot.isTemporal) m_isTemporal = true;

		if (_isWrite)
		{
			// フォーマットとサイズは書き込み側が主導権を持つ
			if (a_slot.format != DXGI_FORMAT_UNKNOWN) m_format = a_slot.format;
			if (a_slot.width != 0) m_declWidth = a_slot.width;
			if (a_slot.height != 0) m_declHeight = a_slot.height;
			m_scale = a_slot.scale;

			// 一度でもクリア指定が入ったら、クリアバリューを持つリソースとして作る
			if (a_slot.loadOp == ELoadOp::Clear)
			{
				m_isClear = true;
				m_clearColor = a_slot.clearColor;
			}
		}

		// 書き手はサイズとフォーマットを、読み手は用途フラグを変える。
		// どちらも占有サイズに効くので、足し込むたびに出し直す
		ResolveSizeAndAllocation();
	}

	void VirtualResource::ResolveSize(UINT64 a_baseWidth, UINT a_baseHeight, D3D12::Device* a_pDevice)
	{
		// 外部リソースは実体が向こうにあるので触らない
		if (m_isImported) return;

		m_baseWidth = a_baseWidth;
		m_baseHeight = a_baseHeight;
		m_pDevice = a_pDevice;

		// テクスチャで宣言が 0 なら、土台の解像度が無いと決めようがない。
		// バッファは宣言のバイト数だけで決まるので解像度は要らない
		if (!IsBuffer() && (m_declWidth == 0 || m_declHeight == 0) &&
			(a_baseWidth == 0 || a_baseHeight == 0))
		{
			ENGINE_WARNING("[VirtualResource] 描画解像度が設定されていないためサイズを決められません : %s", m_name.c_str());
			return;
		}

		ResolveSizeAndAllocation();
	}

	// 宣言値と土台の解像度から実サイズを出す。
	// 宣言側は触らないので、何度呼んでも同じ結果になる
	void VirtualResource::ResolveSizeAndAllocation()
	{
		if (m_isImported) return;

		if (IsBuffer())
		{
			// バッファは width をバイト数として使っているので、解像度を掛けても意味がない
			m_width = m_declWidth;
			m_height = 1;

			CalcAllocationSize();
			return;
		}

		m_width = m_declWidth;
		m_height = m_declHeight;

		// 宣言が 0 のところだけ解像度から埋める。
		// スケール指定 : ハーフ解像度のGIバッファなどはここで 0.5 が掛かる
		if (m_width == 0 || m_height == 0)
		{
			// 土台がまだ来ていないなら決められない。
			// 異常かどうかは呼び手(ResolveSize)が判断する
			if (m_baseWidth == 0 || m_baseHeight == 0) return;

			if (m_width == 0)  m_width = static_cast<UINT64>(static_cast<float>(m_baseWidth) * m_scale);
			if (m_height == 0) m_height = static_cast<UINT>(static_cast<float>(m_baseHeight) * m_scale);
		}

		// 0 にすると生成に失敗するので、最低1ピクセルは残す
		if (m_width == 0)  m_width = 1;
		if (m_height == 0) m_height = 1;

		CalcAllocationSize();
	}

	bool VirtualResource::HasUsage(Resource::ETextureUsage a_usage) const
	{
		return (m_usage & a_usage) != Resource::ETextureUsage::None;
	}

	// 実体を作るときの要件を、テクスチャ生成の宣言へ落とす。
	//
	// 見積もり(CalcAllocationSize)も生成(CreateEntity)も必ずここを通す。
	// 別々に組むと、片方だけ直したときに見積もりと実体が静かにずれる
	Resource::TextureCreateDesc VirtualResource::ToTextureCreateDesc() const
	{
		Resource::TextureCreateDesc _desc = {};
		_desc.name = m_name;
		_desc.width = m_width;
		_desc.height = m_height;
		_desc.format = m_format;
		_desc.usage = m_usage;

		// RTV / DSV はクリアバリューを作成時に渡しておかないと、
		// クリアのたびにドライバ側で最適化が効かず警告も出る
		if (HasUsage(Resource::ETextureUsage::RTV) ||
			HasUsage(Resource::ETextureUsage::DSV))
		{
			_desc.optClearValue = m_clearColor;
		}

		return _desc;
	}

	//======================================================================================
	//
	// 実体
	//
	//======================================================================================
	VirtualResource::~VirtualResource()
	{
		ReleaseEntity();
	}

	// 実体をヒープ上へ置くか。
	//
	// 置けない条件はどれも「そもそも席を持っていない」ことに帰着する。
	// 履歴つき(Temporal)は使い回しの対象外なので席を持たず、
	// 2枚の実体が同じオフセットへ重なる心配もここで消える
	bool VirtualResource::ResolvePlacement(ID3D12Heap* a_pHeap, uint64_t* a_pOutOffset) const
	{
		if (a_pOutOffset) *a_pOutOffset = 0;

		// ヒープが用意できていない(サイズ0・Tier1・作成失敗)
		if (!a_pHeap) return false;

		// 外部リソースの実体はグラフの外の持ち物
		if (m_isImported) return false;

		// バッファはまだ placed の口が無い(GPUBuffer 側が未対応)
		if (IsBuffer()) return false;

		// 席に着いていないものは実体を独り占めするので、個別に作る
		if (m_allocationInfo.slotIndex == AllocationInfo::INVALID_SLOT_INDEX) return false;

		// 大きさを見積もれていないなら席の大きさも 0 なので、置いても収まらない
		if (m_allocationSize == 0) return false;

		if (a_pOutOffset) *a_pOutOffset = m_allocationInfo.offset;
		return true;
	}

	bool VirtualResource::CreateEntity(D3D12::Device* a_pDevice, D3D12::DescriptorHeapManager* a_pHeapManager, ID3D12Heap* a_pHeap)
	{
		// 作り直しなので、前の実体はここで手放す
		ReleaseEntity();

		if (m_isImported)
		{
			ENGINE_WARNING("[VirtualResource] 外部リソースは ImportEntity で入れること : %s", m_name.c_str());
			return false;
		}

		if (!a_pDevice)
		{
			ENGINE_WARNING("[VirtualResource] デバイスが無いので実体を作れません : %s", m_name.c_str());
			return false;
		}

		const uint32_t _entityCount = GetEntityCount();

		//----------------------------------------------------------------------------------
		// バッファ
		//----------------------------------------------------------------------------------
		if (IsBuffer())
		{
			if (m_width == 0)
			{
				ENGINE_WARNING("[VirtualResource] バッファのサイズが0です : %s", m_name.c_str());
				return false;
			}

			D3D12::GPUBufferDesc _desc = {};
			_desc.elementNum = 1;
			_desc.strideSize = static_cast<size_t>(m_width);	// width にバイト数が入っている
			_desc.heapType = D3D12_HEAP_TYPE_DEFAULT;
			_desc.memoryCategory = D3D12::EVideoMemoryCategory::RenderGraph;

			// UAV として触るなら生成時にフラグを立てておく必要がある
			_desc.flags = HasUsage(Resource::ETextureUsage::UAV)
				? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
				: D3D12_RESOURCE_FLAG_NONE;

			for (uint32_t _slice = 0; _slice < _entityCount; ++_slice)
			{
				auto _upBuffer = std::make_unique<D3D12::GPUBuffer>();

				// GPUBuffer::Create はビューを作らない(グラフのバッファはビューを持たない)ので、
				// ディスクリプタヒープは渡さない
				if (!_upBuffer->Create(a_pDevice, _desc))
				{
					ENGINE_WARNING("[VirtualResource] バッファの生成に失敗しました : %s", m_name.c_str());
					ReleaseEntity();
					return false;
				}

				m_entity[_slice].pResource = _upBuffer.get();
				m_entity[_slice].upBuffer = std::move(_upBuffer);
			}

			return true;
		}

		//----------------------------------------------------------------------------------
		// テクスチャ
		//----------------------------------------------------------------------------------
		if (m_format == DXGI_FORMAT_UNKNOWN)
		{
			ENGINE_WARNING("[VirtualResource] フォーマットが決まっていません : %s", m_name.c_str());
			return false;
		}
		if (m_width == 0 || m_height == 0)
		{
			ENGINE_WARNING("[VirtualResource] サイズが0です : %s", m_name.c_str());
			return false;
		}

		// 席を確保したときの見積もりと同じ宣言を通す。
		// 別に組むと確保した席に収まらない
		const Resource::TextureCreateDesc _texDesc = ToTextureCreateDesc();

		uint64_t _heapOffset = 0;
		const bool _isPlaced = ResolvePlacement(a_pHeap, &_heapOffset);

		for (uint32_t _slice = 0; _slice < _entityCount; ++_slice)
		{
			auto _upTexture = std::make_unique<Resource::Texture>();

			if (_isPlaced) _upTexture->Create(a_pHeapManager, a_pHeap, _heapOffset, _texDesc);
			else           _upTexture->Create(a_pHeapManager, _texDesc);

			// Texture::Create は失敗を返さないので、実体が入ったかで見る
			if (!_upTexture->GetResource())
			{
				ENGINE_WARNING("[VirtualResource] テクスチャの生成に失敗しました : %s (placed=%d offset=%llu)",
					m_name.c_str(), _isPlaced ? 1 : 0, _heapOffset);

				_upTexture->Release();
				ReleaseEntity();
				return false;
			}

			// 個別に作ったぶんもグラフの持ち物として数える(placed はヒープで数えているので素通り)
			_upTexture->SetMemoryCategory(D3D12::EVideoMemoryCategory::RenderGraph);

			m_entity[_slice].pResource = _upTexture.get();
			m_entity[_slice].upTexture = std::move(_upTexture);
		}

		m_isPlaced = _isPlaced;
		return true;
	}

	void VirtualResource::ImportEntity(D3D12::GPUResource* a_pResource)
	{
		// 実体を持っていたなら手放してから参照へ切り替える
		ReleaseEntity();

		// 外部の実体はこちらで解放しないので、指すだけにする
		m_entity[0].pResource = a_pResource;
		m_entity[1].pResource = a_pResource;
	}

	void VirtualResource::ReleaseEntity()
	{
		for (Entity& _entity : m_entity)
		{
			// unique_ptr の破棄でも ComPtr は解放されるが、Release() を先に呼ぶことで
			// ディスクリプタヒープのハンドルも確実に返却し、破棄タイミングに依存しないようにする。
			// 外部から借りているだけのものは実体を持たないので、ここは素通りする
			if (_entity.upTexture) _entity.upTexture->Release();
			if (_entity.upBuffer)  _entity.upBuffer->Release();

			_entity.upTexture.reset();
			_entity.upBuffer.reset();
			_entity.pResource = nullptr;
		}

		m_isPlaced = false;
	}

	void VirtualResource::CalcAllocationSize()
	{
		m_allocationSize = 0;
		m_allocationAlignment = 0;

		// 外部リソースの実体はグラフの外の持ち物なので、こちらのヒープは食わない
		if (m_isImported) return;

		// デバイスはコンパイル時(ResolveSize)に受け取る。
		// それより前に要件が足し込まれたときは見積もれないので 0 のまま置いておき、
		// コンパイルの ResolveSize で出し直す
		auto* _pDevice = m_pDevice;
		if (!_pDevice) return;

		//----------------------------------------------------------------------------------
		// 見積もりに渡す仕様書は、実体を作るときのものと一字一句同じでないといけない。
		// 食い違うと、確保した席にリソースが収まらない
		//----------------------------------------------------------------------------------
		D3D12_RESOURCE_DESC _desc = {};

		if (IsBuffer())
		{
			// まだサイズが決まっていない : 決まってから出し直される
			if (m_width == 0) return;

			// GPUBuffer::Create が組むものに合わせる(width にバイト数が入っている)
			_desc = CD3DX12_RESOURCE_DESC::Buffer(
				m_width,
				HasUsage(Resource::ETextureUsage::UAV)
					? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
					: D3D12_RESOURCE_FLAG_NONE);
		}
		else
		{
			// 要件がまだ揃っていない : 揃ってから出し直される
			if (m_width == 0 || m_height == 0) return;
			if (m_format == DXGI_FORMAT_UNKNOWN) return;

			// 実体を作るときと同じ宣言から起こす。
			// 用途フラグは詰み方と大きさを変えるので、ここを別に組んではいけない
			_desc = Resource::BuildTextureResourceDesc(ToTextureCreateDesc());
		}

		D3D12_RESOURCE_ALLOCATION_INFO _info = _pDevice->GetResourceAllocationInfo(0,1,&_desc);

		// 通らない組み合わせを渡すと SizeInBytes が UINT64_MAX で返る。
		// そのまま足すとヒープサイズが桁あふれするので、0 のままにしておく
		if (_info.SizeInBytes == UINT64_MAX)
		{
			ENGINE_WARNING("[VirtualResource] 占有サイズを見積もれませんでした : %s", m_name.c_str());
			return;
		}

		m_allocationSize = _info.SizeInBytes;
		m_allocationAlignment = _info.Alignment;
	}

	Resource::ETextureUsage VirtualResource::ToUsage(EAccessType a_accessType, bool a_isWrite)
	{
		if (a_isWrite)
		{
			// 書いた結果は後続のパスが読むのが普通なので、SRV も一緒に立てておく
			switch (a_accessType)
			{
			case EAccessType::RTV:			return Resource::ETextureUsage::RTV | Resource::ETextureUsage::SRV;
			case EAccessType::DepthWrite:	return Resource::ETextureUsage::DSV | Resource::ETextureUsage::SRV;
			case EAccessType::UAV:			return Resource::ETextureUsage::UAV | Resource::ETextureUsage::SRV;
			default:						return Resource::ETextureUsage::None;
			}
		}

		switch (a_accessType)
		{
		case EAccessType::SRV:			return Resource::ETextureUsage::SRV;
		case EAccessType::UAV:			return Resource::ETextureUsage::UAV;
		// 深度を読むパスは、DSV で深度テストしつつシェーダーからも引くことがある(粒のぼかし)
		case EAccessType::DepthRead:	return Resource::ETextureUsage::DSV | Resource::ETextureUsage::SRV;
		default:						return Resource::ETextureUsage::None;
		}
	}

	D3D12_RESOURCE_STATES VirtualResource::ToResourceState(EAccessType a_accessType)
	{
		switch (a_accessType)
		{
			// シェーダーから読むものは、どのステージから引かれても通るように両方立てる
		case EAccessType::SRV:			return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case EAccessType::RTV:			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case EAccessType::UAV:			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			// 読み取り専用の DSV で深度テストしながら、同じパスでシェーダーからも読めるようにする
			// (どれも読み取りのステートなので同時に立ててよい)
		case EAccessType::DepthRead:	return D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case EAccessType::DepthWrite:	return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		case EAccessType::CopySrc:		return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case EAccessType::CopyDst:		return D3D12_RESOURCE_STATE_COPY_DEST;
		default:						return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	bool VirtualResource::IsReadOnlyState(D3D12_RESOURCE_STATES a_state)
	{
		// 読み取り専用のステートだけを並べたもの。
		// これに収まっていれば、同じ塊の中で複数のパスが同時に読んでも問題ない
		constexpr D3D12_RESOURCE_STATES READ_ONLY_MASK =
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_DEPTH_READ |
			D3D12_RESOURCE_STATE_COPY_SOURCE;

		// COMMON(=0) は「読み取り専用」ではないので弾く
		if (a_state == D3D12_RESOURCE_STATE_COMMON) return false;

		return (a_state & ~READ_ONLY_MASK) == 0;
	}
}
