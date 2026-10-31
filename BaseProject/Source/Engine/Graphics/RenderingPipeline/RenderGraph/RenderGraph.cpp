#include "Engine/Graphics/RenderingPipeline/RenderGraph/RenderGraph.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Manager/AssetDatabase/AssetDatabase.h"

// 設計図を書き出して実行用へ読み直すときに、中継ぎの JSON を持つ。
// プリコンパイル済みヘッダーへ置くと全翻訳単位に広がるため
#pragma warning(push, 0)
#include <nlohmannJSON/json.hpp>
#pragma warning(pop)

#include "Engine/Graphics/RenderingPipeline/PassMetaRegistry/PassMetaRegistry.h"
#include "Engine/Graphics/RenderingPipeline/RenderGraph/RenderGraphCompiler/RenderGraphCompiler.h"

#include "Engine/Graphics/RenderingPipeline/Core/Pass/Pass.h"
#include "Engine/Graphics/RenderingPipeline/RenderGraph/Resource/ResourceRegistry.h"
#include "Engine/Graphics/RenderingPipeline/RenderGraph/Resource/ResourceAllocator.h"
#include "Engine/Graphics/RenderingPipeline/RenderGraph/GraphHeap/GraphHeap.h"

// 実行時に触るもの
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/Frame/RenderContext/RenderContext.h"
#include "Engine/Graphics/D3D12/DescriptorHeapManager/DescriptorHeapManager.h"
#include "Engine/MainEngine.h"
#include "Engine/Graphics/Raytracing/RayEngine.h"


namespace Engine::Graphics::Pipeline
{
	// レジストリと割り当ては unique_ptr で持つので、
	// 完全型が見えるここで作って、ここで捨てる
	RenderGraph::RenderGraph()
		: m_upResourceRegistry(std::make_unique<ResourceRegistry>())
		, m_upResourceAllocator(std::make_unique<ResourceAllocator>())
	{}

	RenderGraph::~RenderGraph() = default;

	const std::vector<VirtualResource>& RenderGraph::GetVirtualResources() const
	{
		return m_upResourceRegistry->GetVirtualResources();
	}

	// スロットに割り当てられたGPUリソースを引く
	D3D12::GPUResource* PassContext::GetResource(const Slot& a_slot) const
	{
		if (!pGraph) return nullptr;
		return pGraph->RefGPUResource(a_slot);
	}

	void RenderGraph::Clear()
	{
		ReleaseResources();

		m_compilePasses.clear();
		m_endBarriers.clear();
		m_connectionMap.clear();
		m_passes.clear();

		// IDは振り直さない。
		// 振り直すと、保存済みのつなぎが持つピンIDと衝突する
	}

	//======================================================================================
	//
	// 設計図 / 実行インスタンス
	//
	//======================================================================================

	// 型IDからパスを作り直して、アーカイブから中身を流し込む
	std::unique_ptr<Pass> RenderGraph::CreatePassFromArchive(
		const PassMetaRegistry& a_registry, ID<Pass> a_typeID, Persistence::Archive& a_arch)
	{
		std::unique_ptr<Pass> _upPass = a_registry.Create(a_typeID);
		if (!_upPass)
		{
			ENGINE_WARNING("[RenderGraph] 未登録のパスタイプです : %u", a_typeID.value);
			return nullptr;
		}

		_upPass->SetTypeID(a_typeID);

		// 先に Init() でスロットを宣言させる。
		// ArchivePass はピンIDを「並び順で」写すので、器ができていないと入らない
		_upPass->Init();

		// 名前・GUID・ノード/ピンID・固有データが入る
		_upPass->ArchivePass(a_arch);

		return _upPass;
	}

	// 設計図のグラフから実行用のグラフを組み立てる。
	// パスは1つずつメモリ上のJSONへ書き出して読み直すので、
	// 各パスは Archive() さえ書いておけば複製にも対応できる(複製専用の関数を持たなくてよい)
	bool RenderGraph::BuildFrom(const RenderGraph& a_source, const PassMetaRegistry& a_registry)
	{
		Clear();

		// IDは設計図と同じものを使う。
		// 線はピン名で結び直すのでIDが被っても実害はないが、
		// エディター側と突き合わせるときに揃っていたほうが追いやすい
		m_idCounter = a_source.m_idCounter;

		m_passes.reserve(a_source.m_passes.size());
		for (const auto& _upSrcPass : a_source.m_passes)
		{
			if (!_upSrcPass) continue;

			// 設計図側を書き出して、実行用側で読み直す
			nlohmann::json _json = {};
			{
				Persistence::Archive _saveArch(Persistence::Archive::EMode::Save, _json);
				_upSrcPass->ArchivePass(_saveArch);
			}

			Persistence::Archive _loadArch(Persistence::Archive::EMode::Load, _json);
			std::unique_ptr<Pass> _upPass = CreatePassFromArchive(a_registry, _upSrcPass->GetTypeID(), _loadArch);
			if (!_upPass) continue;

			m_passes.push_back(std::move(_upPass));
		}

		// 配線はそのまま写す(GUIDとピン名で結んでいるので中身を作り替える必要がない)
		m_connectionMap = a_source.m_connectionMap;

		ApplyLinks();
		return true;
	}

	// 形は同じままなので、パスをGUIDで突き合わせて中身だけ写す
	void RenderGraph::SyncParamsFrom(const RenderGraph& a_source)
	{
		for (const auto& _upSrcPass : a_source.m_passes)
		{
			if (!_upSrcPass) continue;

			Pass* _pDst = FindPass(_upSrcPass->GetGUID());
			if (!_pDst) continue;

			nlohmann::json _json = {};
			{
				Persistence::Archive _saveArch(Persistence::Archive::EMode::Save, _json);
				_upSrcPass->ArchivePass(_saveArch);
			}

			Persistence::Archive _loadArch(Persistence::Archive::EMode::Load, _json);
			_pDst->ArchivePass(_loadArch);
		}
	}

	void RenderGraph::Archive(Persistence::Archive& a_arch, const PassMetaRegistry& a_registry)
	{
		a_arch.Field("idCounter", m_idCounter);

		//----------------------------------------------------------------------------------
		// パス
		//----------------------------------------------------------------------------------
		if (a_arch.IsLoading())
		{
			Clear();
			m_connectionMap.clear();
		}

		size_t _passCount = m_passes.size();
		if (a_arch.BeginArray("passes", _passCount))
		{
			for (size_t _i = 0; _i < _passCount; ++_i)
			{
				if (!a_arch.BeginObject(_i)) continue;

				if (a_arch.IsSaving())
				{
					Pass* _pPass = m_passes[_i].get();
					if (_pPass)
					{
						uint32_t _typeID = _pPass->GetTypeID().value;
						a_arch.Field("typeID", _typeID);
						_pPass->ArchivePass(a_arch);
					}
				}
				else
				{
					uint32_t _typeIDValue = 0;
					a_arch.Field("typeID", _typeIDValue);

					if (auto _upPass = CreatePassFromArchive(a_registry, ID<Pass>(_typeIDValue), a_arch))
					{
						m_passes.push_back(std::move(_upPass));
					}
				}

				a_arch.EndObject();
			}
			a_arch.EndArray();
		}

		//----------------------------------------------------------------------------------
		// 配線
		//
		// キーが接続元パスのGUIDなので、マップのままだと配列に落とせない。
		// 「接続元GUID + 線1本」の並びへ展開して保存する
		//----------------------------------------------------------------------------------
		std::vector<std::pair<Core::GUID, Connection>> _flatConnectionVec = {};
		if (a_arch.IsSaving())
		{
			for (const auto& [_srcGUID, _connectionVec] : m_connectionMap)
			{
				for (const Connection& _connection : _connectionVec)
				{
					_flatConnectionVec.emplace_back(_srcGUID, _connection);
				}
			}
		}

		size_t _connectionCount = _flatConnectionVec.size();
		if (a_arch.BeginArray("connections", _connectionCount))
		{
			if (a_arch.IsLoading()) _flatConnectionVec.resize(_connectionCount);

			for (size_t _i = 0; _i < _connectionCount && _i < _flatConnectionVec.size(); ++_i)
			{
				if (!a_arch.BeginObject(_i)) continue;

				a_arch.GUIDField("srcPassGUID", _flatConnectionVec[_i].first);
				_flatConnectionVec[_i].second.Archive(a_arch);

				a_arch.EndObject();
			}
			a_arch.EndArray();
		}

		if (a_arch.IsLoading())
		{
			for (auto& [_srcGUID, _connection] : _flatConnectionVec)
			{
				m_connectionMap[_srcGUID].push_back(_connection);
			}

			// 読み込んだ配線を入力スロットへ流し込む
			ApplyLinks();
		}
	}

	//======================================================================================
	//
	// パス
	//
	//======================================================================================
	Pass* RenderGraph::AddPass(const PassMetaRegistry& a_registry, ID<Pass> a_typeID)
	{
		std::unique_ptr<Pass> _upPass = a_registry.Create(a_typeID);
		if (!_upPass) return nullptr;

		// 表示名と型IDはレジストリ側の情報から入れる
		const PassMeta* _pMeta = a_registry.GetMeta(a_typeID);
		_upPass->SetTypeID(a_typeID);
		_upPass->SetName(_pMeta ? _pMeta->name : std::string("Pass"));

		// GUID発行 + スロット宣言 -> そのぶんのノード/ピンIDを確保
		_upPass->Init();
		_upPass->EnsureEditorIDs([this]() { return GenerateID(); });

		// 置き場所 : 全部同じ場所に出ると重なって掴めないので少しずつずらす。
		// ImNodes へ反映するのは編集UI側の仕事なので、ここでは値を決めるだけ
		const float _offset = static_cast<float>(m_passes.size() % 8) * 30.0f;
		_upPass->SetEditorPos(Math::Vector2(40.0f + _offset, 40.0f + _offset));

		Pass* _pPass = _upPass.get();
		m_passes.push_back(std::move(_upPass));
		return _pPass;
	}

	void RenderGraph::RemovePass(const Core::GUID& a_passGUID)
	{
		// このパスから出る線を消す
		m_connectionMap.erase(a_passGUID);

		// このパスへ入る線を消す
		for (auto& [_srcGUID, _connectionVec] : m_connectionMap)
		{
			_connectionVec.erase(
				std::remove_if(_connectionVec.begin(), _connectionVec.end(),
					[&a_passGUID](const Connection& a_connection) { return a_connection.dstPassGUID == a_passGUID; }),
				_connectionVec.end());
		}

		// 実体を消す
		m_passes.erase(
			std::remove_if(m_passes.begin(), m_passes.end(),
				[&a_passGUID](const std::unique_ptr<Pass>& a_upPass)
				{ return a_upPass && a_upPass->GetGUID() == a_passGUID; }),
			m_passes.end());

		// 消したパスを指していた Pass* が残るので、コンパイル結果は捨てる
		m_compilePasses.clear();
		m_endBarriers.clear();

		ApplyLinks();
	}

	Pass* RenderGraph::FindPass(const Core::GUID& a_passGUID)
	{
		for (auto& _upPass : m_passes)
		{
			if (_upPass && _upPass->GetGUID() == a_passGUID) return _upPass.get();
		}
		return nullptr;
	}

	Pass* RenderGraph::FindPassByNodeID(int a_nodeID)
	{
		for (auto& _upPass : m_passes)
		{
			if (_upPass && _upPass->GetNodeID() == a_nodeID) return _upPass.get();
		}
		return nullptr;
	}

	Pass* RenderGraph::FindPassByPinID(int a_pinID, Slot** a_ppOutSlot, bool* a_pOutIsInput)
	{
		for (auto& _upPass : m_passes)
		{
			if (!_upPass) continue;

			bool _isInput = false;
			if (Slot* _pSlot = _upPass->FindSlotByPinID(a_pinID, &_isInput))
			{
				if (a_ppOutSlot) *a_ppOutSlot = _pSlot;
				if (a_pOutIsInput) *a_pOutIsInput = _isInput;
				return _upPass.get();
			}
		}
		return nullptr;
	}

	//======================================================================================
	//
	// つなぎ
	//
	//======================================================================================
	bool RenderGraph::Link(
		const Core::GUID& a_srcPassGUID, uint32_t a_srcSlotID,
		const Core::GUID& a_dstPassGUID, uint32_t a_dstSlotID)
	{
		Pass* _pSrc = FindPass(a_srcPassGUID);
		Pass* _pDst = FindPass(a_dstPassGUID);
		if (!_pSrc || !_pDst) return false;

		// スロットが実在するかまで見る(IDだけ渡されても張らない)
		const Slot* _pSrcSlot = _pSrc->FindOutputSlot(a_srcSlotID);
		const Slot* _pDstSlot = _pDst->FindInputSlot(a_dstSlotID);
		if (!_pSrcSlot || !_pDstSlot) return false;

		// 自分自身へのつなぎは基本的に循環になるので通さない。
		// ただし Temporal は前フレームの結果を読むだけなので、
		// TAA のように「自分の履歴を自分で読む」構成は許す
		if (a_srcPassGUID == a_dstPassGUID && !_pSrcSlot->isTemporal) return false;

		// 繋いだ瞬間に弾けるものはここで弾く。
		// 残り(必須未接続・循環)はグラフ全体を見ないと分からないので Validate() 側
		std::string _reason = {};
		if (!IsConnectable(*_pSrcSlot, *_pDstSlot, &_reason))
		{
			ENGINE_WARNING("[RenderGraph] つなげない組み合わせです : %s.%s -> %s.%s (%s)",
				_pSrc->GetName().c_str(), _pSrcSlot->pinName.c_str(),
				_pDst->GetName().c_str(), _pDstSlot->pinName.c_str(),
				_reason.c_str());
			return false;
		}

		// 入力スロットは1本まで : 先に張られていた線を外してから張り直す
		DisconnectInputSlot(a_dstPassGUID, a_dstSlotID);

		Connection _connection = {};
		_connection.linkID = GenerateID();
		_connection.dstPassGUID = a_dstPassGUID;
		_connection.srcSlotID = a_srcSlotID;
		_connection.dstSlotID = a_dstSlotID;
		m_connectionMap[a_srcPassGUID].push_back(_connection);

		// つないだ内容をすぐ入力スロットへ反映(ノード表示にも出る)
		ApplyLinks();
		return true;
	}

	// 線1本を消す : どのパスから出ているか分からないので全部見る
	void RenderGraph::RemoveLink(int a_linkID)
	{
		for (auto& [_srcGUID, _connectionVec] : m_connectionMap)
		{
			_connectionVec.erase(
				std::remove_if(_connectionVec.begin(), _connectionVec.end(),
					[a_linkID](const Connection& a_connection) { return a_connection.linkID == a_linkID; }),
				_connectionVec.end());
		}
	}

	// 入力スロットは1本しか受けられないので、張り直す前に既存の線を外す
	void RenderGraph::DisconnectInputSlot(const Core::GUID& a_dstPassGUID, uint32_t a_dstSlotID)
	{
		for (auto& [_srcGUID, _connectionVec] : m_connectionMap)
		{
			_connectionVec.erase(
				std::remove_if(_connectionVec.begin(), _connectionVec.end(),
					[&](const Connection& a_connection)
					{ return a_connection.dstPassGUID == a_dstPassGUID && a_connection.dstSlotID == a_dstSlotID; }),
				_connectionVec.end());
		}
	}

	void RenderGraph::ApplyLinks()
	{
		// 入力を一旦すべて外す
		for (auto& _upPass : m_passes)
		{
			if (!_upPass) continue;
			for (const Slot& _in : _upPass->GetInputSlots())
			{
				_upPass->ClearInput(_in.slotID);
			}
		}

		// 線をたどって出力スロットを流し込む
		for (auto& [_srcGUID, _connectionVec] : m_connectionMap)
		{
			Pass* _pSrc = FindPass(_srcGUID);
			if (!_pSrc) continue;

			for (const Connection& _connection : _connectionVec)
			{
				Pass* _pDst = FindPass(_connection.dstPassGUID);
				if (!_pDst) continue;

				const Slot* _pSrcSlot = _pSrc->FindOutputSlot(_connection.srcSlotID);
				if (!_pSrcSlot) continue;

				_pDst->SetInput(_connection.dstSlotID, *_pSrcSlot);
			}
		}
	}

	//======================================================================================
	//
	// リソース
	//
	//======================================================================================
	void RenderGraph::SetViewportSize(UINT a_width, UINT a_height)
	{
		m_viewportWidth = a_width;
		m_viewportHeight = a_height;
	}

	void RenderGraph::ImportResource(
		const std::string& a_name,
		D3D12::GPUResource* a_pResource,
		D3D12_RESOURCE_STATES a_initialState,
		EPassSlotType a_type)
	{
		m_upResourceRegistry->ImportResource(a_name, a_pResource, a_initialState, a_type);
	}

	void RenderGraph::RemoveImportedResource(const std::string& a_name)
	{
		m_upResourceRegistry->RemoveImportedResource(a_name);
	}

	void RenderGraph::ClearImportedResources()
	{
		m_upResourceRegistry->ClearImportedResource();
	}

	void RenderGraph::ClearCompiledData()
	{
		m_compilePasses.clear();
		m_endBarriers.clear();
	}

	void RenderGraph::AddIssue(ValidationIssue&& a_issue)
	{
		m_validationIssueVec.push_back(std::move(a_issue));
	}

	bool RenderGraph::AllocateResources(GraphicsEngine* a_pGraphicsEngine, D3D12::Device* a_pDevice)
	{
		// ビューの置き場を控える : 焼き込みと初回クリアがここから引く
		m_pHeapManager = a_pGraphicsEngine ? a_pGraphicsEngine->RefDescriptorHeapManager() : nullptr;

		//----------------------------------------------------------------------------------
		// ヒープの用意
		//
		// どの席へ置くかと必要な大きさは Compile() で出ている。
		// 用意できなければ IsValid() が false になり、各リソースを個別に作ることになる
		//----------------------------------------------------------------------------------
		if (!m_upGraphHeap) m_upGraphHeap = std::make_unique<GraphHeap>();
		m_upGraphHeap->Create(a_pDevice, m_upResourceAllocator->GetMaxHeapSize());

		// 席に着いたものはこのヒープへ置く。
		// 用意できていなければ nullptr が渡り、全部を個別に作る形に戻る
		ID3D12Heap* _pGraphHeap = m_upGraphHeap->IsValid() ? m_upGraphHeap->RefHeap() : nullptr;

		//----------------------------------------------------------------------------------
		// 実体を作る
		//
		// 仮想リソースはコンパイルのたびに組み直されるので、実体は必ず新品になる。
		// 「要件が同じなら前のものを使い回す」判定はもう要らない
		//----------------------------------------------------------------------------------
		bool _isSuccess = true;
		for (VirtualResource& _virtual : m_upResourceRegistry->RefVirtualResources())
		{
			if (_virtual.IsImported())
			{
				// 外部リソースは実体を作らず、参照だけもらい受ける。
				// フレーム入口のステートも向こうが宣言した値のままにする
				D3D12::GPUResource* _pExternal = m_upResourceRegistry->FindImportedResource(_virtual.GetName());

				if (!_pExternal)
				{
					ENGINE_WARNING("[RenderGraph] 差し込まれた外部リソースが空です : %s", _virtual.GetName().c_str());
					_isSuccess = false;
					continue;
				}

				_virtual.ImportEntity(_pExternal);
				continue;
			}

			if (!_virtual.CreateEntity(a_pDevice, m_pHeapManager, _pGraphHeap))
			{
				_isSuccess = false;
				continue;
			}

			// 作りたてなので、1フレーム目に中身を読まれないようクリアしておく
			if (_virtual.IsTemporal()) m_isTemporalClearPending = true;

			// 生成直後のステートがフレーム入口のステートになる(バリアの起点)
			for (uint32_t _slice = 0; _slice < _virtual.GetEntityCount(); ++_slice)
			{
				if (D3D12::GPUResource* _pRes = _virtual.RefEntity(_slice))
				{
					_virtual.SetInitialState(_pRes->GetState(), _slice);
				}
			}
			_virtual.ResetStateToInitial();
		}

		// 実体ができて入口のステートが確定したので、バリアを積み直してから実体を結びつける。
		// Compile() の時点ではまだ実体が無く、before は仮の値で積んでいる
		RenderGraphCompiler(this).BuildBarriers(m_compilePasses, m_endBarriers);
		ResolveBarrierResources();

		// 出力先(RTV/DSV)を焼き込んでから、各パスへランタイムデータを作らせる。
		// この順でないとパスがディスクリプタを引けない
		ResolveDescriptors();
		CompilePasses(a_pGraphicsEngine);

		return _isSuccess;
	}

	void RenderGraph::ReleaseResources()
	{
		// 実体は仮想リソースが持っているので、捨てる前にディスクリプタごと返させる。
		// (配列を捨てるだけでもデストラクタが通るが、順番を目に見える形にしておく)
		for (VirtualResource& _virtual : m_upResourceRegistry->RefVirtualResources())
		{
			_virtual.ReleaseEntity();
		}

		// 外部リソースの控えは差し込んだ側の持ち物なので、ここで捨てるのは仮想リソースだけ
		m_upResourceRegistry->ClearVirtualResources();

		// 仮想リソースが消えたので、それを指していたバリアも捨てる
		for (CompiledPass& _compiledPass : m_compilePasses)
		{
			_compiledPass.preBarriers.clear();
			_compiledPass.preAliasingBarriers.clear();
		}
		m_endBarriers.clear();

		// 実体を全部手放したあとでヒープを捨てる。
		// 置いたリソースがヒープの参照を握っているので、この順でないと意味がない
		if (m_upGraphHeap) m_upGraphHeap->Release();
	}

	// 仮想リソースは Compile のたびに組み直されるので、
	// 識別子から引き直す。スロット側に添字を焼いておくと、
	// 組み直しのたびに全スロットを舐めて直しに行くことになる
	const VirtualResource* RenderGraph::GetVirtualResource(ResourceID a_resourceID) const
	{
		return m_upResourceRegistry->GetByID(a_resourceID);
	}

	VirtualResource* RenderGraph::RefVirtualResource(ResourceID a_resourceID)
	{
		return m_upResourceRegistry->RefByID(a_resourceID);
	}

	D3D12::GPUResource* RenderGraph::RefGPUResource(ResourceID a_resourceID, uint32_t a_slice) const
	{
		const VirtualResource* _pVirtual = GetVirtualResource(a_resourceID);
		return _pVirtual ? _pVirtual->RefEntity(a_slice) : nullptr;
	}

	// スロットの向きと今のフレームの偶奇から、触るべき実体を決める
	D3D12::GPUResource* RenderGraph::RefGPUResource(const Slot& a_slot) const
	{
		const VirtualResource* _pVirtual = GetVirtualResource(a_slot.resourceID);
		if (!_pVirtual) return nullptr;

		// 出力は Current、入力は Previous。
		// それをフレームの偶奇でひっくり返したものが実際の物理になる
		const uint32_t _slice = VirtualResource::ToSlice(a_slot, _pVirtual->IsTemporal());
		const uint32_t _index = _pVirtual->IsTemporal() ? ((GetFrameParity() + _slice) & 1u) : 0u;

		return RefGPUResource(a_slot.resourceID, _index);
	}

	bool RenderGraph::HasTemporalResource() const
	{
		for (const VirtualResource& _virtual : m_upResourceRegistry->GetVirtualResources())
		{
			if (_virtual.IsTemporal()) return true;
		}
		return false;
	}

	//======================================================================================
	//
	// 検証
	//
	//======================================================================================
	bool RenderGraph::IsWriteAccess(EAccessType a_accessType)
	{
		switch (a_accessType)
		{
		case EAccessType::RTV:
		case EAccessType::UAV:
		case EAccessType::DepthWrite:
		case EAccessType::CopyDst:
			return true;
		default:
			return false;
		}
	}

	bool RenderGraph::IsReadAccess(EAccessType a_accessType)
	{
		switch (a_accessType)
		{
		case EAccessType::SRV:
		case EAccessType::UAV:
		case EAccessType::DepthRead:
		case EAccessType::CopySrc:
			return true;
		default:
			return false;
		}
	}

	// 繋いでよい組み合わせか。
	// UAV は読み書きの両方に出てくるので、write / read の判定は重なる
	bool RenderGraph::IsConnectable(const Slot& a_srcSlot, const Slot& a_dstSlot, std::string* a_pOutReason)
	{
		auto _fail = [a_pOutReason](const char* a_reason)
			{
				if (a_pOutReason) *a_pOutReason = a_reason;
				return false;
			};

		// 向き : 出力スロットから入力スロットへしか繋がない
		if (a_srcSlot.isIn)  return _fail("接続元が入力スロットです");
		if (!a_dstSlot.isIn) return _fail("接続先が出力スロットです");

		// リソースタイプ : テクスチャにバッファは繋げない
		if (a_srcSlot.type != a_dstSlot.type) return _fail("リソースタイプが違います(Texture/Buffer)");

		// アクセスタイプ : 作る側が書き込みになっていること
		if (!IsWriteAccess(a_srcSlot.accessType)) return _fail("接続元が書き込みアクセスではありません");

		// 「すでに描かれている絵へ描き足す」つなぎ。
		// 受け側も書き込みアクセスなら、消して描き直すのではなく重ねる意味になる。
		// スカイ・パーティクル・UI のように、前段の結果へ上描きするパスがこれで前段の後ろに並ぶ。
		//
		// 書き方(RTV/UAV/深度)が揃っている必要は無い。
		// コンピュートが描いた絵へラスタで描き足す組み合わせは普通にある
		// (スカイ(UAV) -> パーティクル(RTV) / 魚眼(UAV) -> デバッグ線(RTV))。
		// ステートはグラフが受け側のアクセスへ遷移させるので、ここで揃える必要は無い
		if (IsWriteAccess(a_dstSlot.accessType)) return true;

		if (!IsReadAccess(a_dstSlot.accessType))  return _fail("接続先が読み取りアクセスではありません");

		return true;
	}

	// 繋ぎ方だけで分かる不備をまとめて拾う。
	// GPUには触らないので、エディターから呼んで結果を出してもよい
	bool RenderGraph::Validate(std::vector<ValidationIssue>* a_pOutIssueVec) const
	{
		m_validationIssueVec.clear();

		auto _push = [this](ValidationIssue::ELevel a_level, const Core::GUID& a_guid, std::string a_message)
			{
				ValidationIssue _issue = {};
				_issue.level = a_level;
				_issue.passGUID = a_guid;
				_issue.message = std::move(a_message);
				m_validationIssueVec.push_back(std::move(_issue));
			};

		// パスをGUIDで引けるようにしておく(接続の検証で何度も引くため)
		std::unordered_map<Core::GUID, const Pass*> _passMap = {};
		for (const auto& _upPass : m_passes)
		{
			if (!_upPass) continue;
			_passMap.emplace(_upPass->GetGUID(), _upPass.get());
		}

		//----------------------------------------------------------------------------------
		// 接続の検証
		//----------------------------------------------------------------------------------
		// 入力スロットが何本の線を受けているか。1本を超えたら繋ぎ方がおかしい
		std::unordered_map<uint64_t, int> _inputLinkCountMap = {};
		auto _makeInputKey = [](const Core::GUID& a_guid, uint32_t a_slotID)
			{
				return (static_cast<uint64_t>(std::hash<Core::GUID>{}(a_guid)) << 32) ^ a_slotID;
			};

		for (const auto& [_srcGUID, _connectionVec] : m_connectionMap)
		{
			auto _srcIt = _passMap.find(_srcGUID);
			if (_srcIt == _passMap.end())
			{
				_push(ValidationIssue::ELevel::Error, _srcGUID, "接続元のパスがありません");
				continue;
			}
			const Pass* _pSrc = _srcIt->second;

			for (const Connection& _connection : _connectionVec)
			{
				auto _dstIt = _passMap.find(_connection.dstPassGUID);
				if (_dstIt == _passMap.end())
				{
					_push(ValidationIssue::ELevel::Error, _srcGUID,
						_pSrc->GetName() + " : 接続先のパスがありません");
					continue;
				}
				const Pass* _pDst = _dstIt->second;

				const Slot* _pSrcSlot = _pSrc->FindOutputSlot(_connection.srcSlotID);
				if (!_pSrcSlot)
				{
					_push(ValidationIssue::ELevel::Error, _srcGUID,
						_pSrc->GetName() + " : 接続元の出力スロットがありません");
					continue;
				}

				const Slot* _pDstSlot = _pDst->FindInputSlot(_connection.dstSlotID);
				if (!_pDstSlot)
				{
					_push(ValidationIssue::ELevel::Error, _connection.dstPassGUID,
						_pDst->GetName() + " : 接続先の入力スロットがありません");
					continue;
				}

				std::string _reason = {};
				if (!IsConnectable(*_pSrcSlot, *_pDstSlot, &_reason))
				{
					_push(ValidationIssue::ELevel::Error, _connection.dstPassGUID,
						_pSrc->GetName() + "." + _pSrcSlot->pinName + " -> " +
						_pDst->GetName() + "." + _pDstSlot->pinName + " : " + _reason);
					continue;
				}

				_inputLinkCountMap[_makeInputKey(_connection.dstPassGUID, _connection.dstSlotID)]++;
			}
		}

		//----------------------------------------------------------------------------------
		// 必須入力の未接続
		//----------------------------------------------------------------------------------
		for (const auto& _upPass : m_passes)
		{
			if (!_upPass) continue;

			for (const Slot& _in : _upPass->GetInputSlots())
			{
				const int _linkCount = _inputLinkCountMap[_makeInputKey(_upPass->GetGUID(), _in.slotID)];

				if (_linkCount > 1)
				{
					_push(ValidationIssue::ELevel::Error, _upPass->GetGUID(),
						_upPass->GetName() + "." + _in.pinName + " : 入力に線が複数入っています");
				}

				if (_linkCount != 0) continue;

				if (_in.isRequired)
				{
					_push(ValidationIssue::ELevel::Error, _upPass->GetGUID(),
						_upPass->GetName() + "." + _in.pinName + " : 必須の入力が繋がっていません");
				}
				else
				{
					_push(ValidationIssue::ELevel::Warning, _upPass->GetGUID(),
						_upPass->GetName() + "." + _in.pinName + " : 入力が繋がっていません(任意)");
				}
			}
		}

		//----------------------------------------------------------------------------------
		// 循環依存
		//
		// 実行順を決めるのと同じ辺の張り方で数えて、全部を消化できるかだけを見る。
		// ここで拾っておくと、Compile() のソートが落ちる前に理由を出せる
		//----------------------------------------------------------------------------------
		{
			std::vector<const Pass*> _nodeVec = {};
			_nodeVec.reserve(m_passes.size());
			for (const auto& _upPass : m_passes)
			{
				if (_upPass) _nodeVec.push_back(_upPass.get());
			}

			const size_t _nodeCount = _nodeVec.size();
			std::vector<std::vector<size_t>> _edgeVec(_nodeCount);
			std::vector<size_t> _indegreeVec(_nodeCount, 0);

			// 接続をたどって「dst は src に依存する」を辺にする
			for (const auto& [_srcGUID, _connectionVec] : m_connectionMap)
			{
				for (const Connection& _connection : _connectionVec)
				{
					// Temporal の接続は前フレームを読むだけなので、循環の輪には数えない
					{
						auto _dstIt = _passMap.find(_connection.dstPassGUID);
						if (_dstIt != _passMap.end())
						{
							const Slot* _pDstSlot = _dstIt->second->FindInputSlot(_connection.dstSlotID);
							if (_pDstSlot && _pDstSlot->isTemporal) continue;
						}
					}

					size_t _srcIndex = _nodeCount;
					size_t _dstIndex = _nodeCount;
					for (size_t _i = 0; _i < _nodeCount; ++_i)
					{
						if (_nodeVec[_i]->GetGUID() == _srcGUID) _srcIndex = _i;
						if (_nodeVec[_i]->GetGUID() == _connection.dstPassGUID) _dstIndex = _i;
					}
					if (_srcIndex >= _nodeCount || _dstIndex >= _nodeCount) continue;
					if (_srcIndex == _dstIndex) continue;

					_edgeVec[_srcIndex].push_back(_dstIndex);
					_indegreeVec[_dstIndex]++;
				}
			}

			std::queue<size_t> _queue = {};
			for (size_t _i = 0; _i < _nodeCount; ++_i)
			{
				if (_indegreeVec[_i] == 0) _queue.push(_i);
			}

			size_t _sortedCount = 0;
			while (!_queue.empty())
			{
				size_t _index = _queue.front();
				_queue.pop();
				++_sortedCount;

				for (size_t _next : _edgeVec[_index])
				{
					if (--_indegreeVec[_next] == 0) _queue.push(_next);
				}
			}

			// 消化しきれなかったノードが輪の中に居る
			if (_sortedCount != _nodeCount)
			{
				for (size_t _i = 0; _i < _nodeCount; ++_i)
				{
					if (_indegreeVec[_i] == 0) continue;
					_push(ValidationIssue::ELevel::Error, _nodeVec[_i]->GetGUID(),
						_nodeVec[_i]->GetName() + " : 循環依存の中にあります");
				}
			}
		}

		//----------------------------------------------------------------------------------
		// 結果
		//----------------------------------------------------------------------------------
		bool _isValid = true;
		for (const ValidationIssue& _issue : m_validationIssueVec)
		{
			if (_issue.level == ValidationIssue::ELevel::Error) _isValid = false;
		}

		if (a_pOutIssueVec) *a_pOutIssueVec = m_validationIssueVec;
		return _isValid;
	}

	//======================================================================================
	//
	// 実行
	//
	//======================================================================================

	// パスの入出力スロットから実行順を決めて m_compilePasses へ流し込む
	// 前から順に「バリアを張ってパスを回す」だけでよい、一本の配列にする
	//
	// 順序とリソースの解決そのものは RenderGraphCompiler の仕事。
	// ここは結果を受け取って自分のコンパイル済みデータへ移すだけにする
	bool RenderGraph::Compile(D3D12::Device* a_pDevice)
	{
		// 失敗しても中途半端な状態で走らせないよう、先に捨てておく
		ClearCompiledData();

		// 占有サイズの見積もりに使う(コンパイラが仮想リソースへ渡す)
		m_pCompileDevice = a_pDevice;

		RenderGraphCompiler _compiler(this);

		// パスのコンパイル
		CompileResult _result = _compiler.Compile();
		if (!_result.isSuccess) return false;

		m_compilePasses = std::move(_result.compiledPassVec);
		m_endBarriers = std::move(_result.endBarrierVec);

		// 占有サイズはコンパイラの ResolveSize で出ている(デバイスが渡されていれば)

		// 仮想リソースがすべて出来上がったので、どの席へ置くかを決める。
		// 必要なヒープの大きさもここで出る
		if (!m_upResourceAllocator) m_upResourceAllocator = std::make_unique<ResourceAllocator>();
		m_upResourceAllocator->CalcAllocation(m_upResourceRegistry->RefVirtualResources());

		// エイリアシングバリアを作成
		_compiler.BuildAliasingBarriers(m_compilePasses);

		// ヒープの実体はここでは作らない。
		// この関数はGPUに触らない約束(設計図側のグラフもここを通るので、
		// 作ると使いもしないヒープをエディターが抱え込む)。
		// 実体は AllocateResources() で用意する

		// 各パスの Compile() はここでは呼ばない。
		// 物理リソースが決まってからでないとディスクリプタを引けないので、
		// AllocateResources() の最後で呼ぶ(仕様のコンパイル順どおり)
		return true;
	}

	//======================================================================================
	//
	// ランタイム実行
	//
	//======================================================================================
	void RenderGraph::Execute(GraphicsEngine* a_pGraphicsEngine, RenderContext* a_pRenderContext)
	{
		if (!a_pRenderContext) return;

		D3D12::GraphicsCommandList* _pCmdList = a_pRenderContext->RefCurrentCmdList();
		if (!_pCmdList) return;

		// 割り当て直後の Temporal は中身が未初期化なので、走らせる前に一度クリアする
		if (m_isTemporalClearPending)
		{
			ClearTemporalResources(a_pRenderContext);
			m_isTemporalClearPending = false;
		}

		// このフレームで使う焼き込みを選ぶ
		const uint32_t _parity = GetFrameParity();

		PassContext _context = MakeContext(a_pGraphicsEngine);
		_context.pRenderContext = a_pRenderContext;
		_context.pCmdList = _pCmdList;

		for (CompiledPass& _compiledPass : m_compilePasses)
		{
			ENGINE_PROFILE_SCOPE(_compiledPass.pPass->GetName());
			// ---- エイリアシングバリア ----
			// 席の使い手が入れ替わる継ぎ目。
			// メモリの持ち主が変わってから遷移させるので、ステート遷移より前に張る
			for (const AliasingBarrier& _barrier : _compiledPass.preAliasingBarriers)
			{
				D3D12::GPUResource* _pAfter = _barrier.pAfterResource[_parity];
				if (!_pAfter) continue;

				// before が nullptr なら「このヒープのどれかが前の使い手」の意味になる
				_pAfter->AliasingBarrier(_pCmdList, _barrier.pBeforeResource[_parity]);
			}

			// ---- バリア ----
			// UAVバリアは既存グラフと同じく今は張らない(状態遷移のみ)
			for (const ResourceBarrier& _barrier : _compiledPass.preBarriers)
			{
				D3D12::GPUResource* _pResource = _barrier.pResource[_parity];
				if (!_pResource) continue;
				if (_barrier.isUAVBarrier) continue;

				_pResource->Barrier(_pCmdList, _barrier.after);
			}

			// ---- 引き継いだ中身をならす ----
			//
			// エイリアシング直後の中身は未定義で、前の使い手が残したものが乗っている。
			// レンダーターゲット / 深度のフラグ付きで作ったリソースは、
			// 一度ならしてからでないと使えない(D3D12の要求)。
			//
			// Discard は書き込みステートでしか呼べないので、そこまで遷移させてから呼び、
			// このパスが求めるステートへ戻す。
			// 戻さないと、遷移が要らないと判断されたパスがそのまま走ってしまう。
			//
			// このパスがどのみちクリアするリソースでも重ねて呼ぶが、実害は無い
			for (const AliasingBarrier& _barrier : _compiledPass.preAliasingBarriers)
			{
				if (_barrier.discardState == D3D12_RESOURCE_STATE_COMMON) continue;

				D3D12::GPUResource* _pAfter = _barrier.pAfterResource[_parity];
				if (!_pAfter) continue;

				// 上のステート遷移まで通した、今この瞬間のステート。
				// GPUResource が自分で追っているので、割り込んで遷移させても食い違わない
				const D3D12_RESOURCE_STATES _passState = _pAfter->GetState();

				_pAfter->Barrier(_pCmdList, _barrier.discardState);
				_pCmdList->DiscardResource(_pAfter->GetResource(), nullptr);
				_pAfter->Barrier(_pCmdList, _passState);
			}

			// ---- レンダーターゲット切り替え ----
			// 出力を1つも持たないパス(コンピュートなど)では張り替えない。
			//
			// 空のディスクリプタ(ptr == 0)を渡すと OMSetRenderTargets の中で落ちるので、
			// 焼き込みが入っているかどうかまで見てから渡す
			const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& _rtvHandles = _compiledPass.rtvHandles[_parity];
			const bool _hasDSV = _compiledPass.hasDSV && (_compiledPass.dsvHandle[_parity].ptr != 0);

			if (!_rtvHandles.empty() || _hasDSV)
			{
				a_pRenderContext->SetRenderTargets(
					_rtvHandles,
					_hasDSV ? &_compiledPass.dsvHandle[_parity] : nullptr);
			}

			// ---- クリア ----
			for (size_t _i = 0; _i < _compiledPass.clearRtvIndices.size(); ++_i)
			{
				const size_t _index = _compiledPass.clearRtvIndices[_i];
				if (_index >= _rtvHandles.size()) continue;

				a_pRenderContext->ClearRenderTarget(
					_rtvHandles[_index],
					_compiledPass.clearRtvColors[_i]);
			}
			if (_compiledPass.isDepthClear && _hasDSV)
			{
				a_pRenderContext->ClearDSV(_compiledPass.dsvHandle[_parity]);
			}

			// ---- グラフが張るもの ----
			// ヒープ・ルートシグネチャ・PSO・ディスクリプタテーブル。
			// ここまで済ませておくと、パスは定数バッファと Dispatch/Draw だけでよくなる
			if (_compiledPass.pPass) ApplyStaticBindings(a_pRenderContext, _compiledPass, _parity);

			// ---- パス本体 ----
			// シェーダーの用意に失敗したコンピュートパスは走らせない。
			// 張られているのは直前のパスのルートシグネチャ/PSOなので、
			// そのままルート定数を張って Dispatch すると番号も中身も食い違う
			// (Release はデバッグレイヤーが無く検証されないので、ここでメモリを壊して落ちる)。
			// 理由は Compile 時に SetupComputeShader が警告で出している
			if (_compiledPass.pPass && !_compiledPass.pPass->CanExecute()) continue;

			if (_compiledPass.pPass) _compiledPass.pPass->Update(_context);
		}

		// ---- フレーム入口のステートへ戻す ----
		// 戻しておかないと、次のフレームのバリアの before が食い違う
		for (const ResourceBarrier& _barrier : m_endBarriers)
		{
			D3D12::GPUResource* _pResource = _barrier.pResource[_parity];
			if (!_pResource) continue;

			_pResource->Barrier(_pCmdList, _barrier.after);
		}

		// 次のフレームでは Temporal の役が入れ替わる
		++m_frameIndex;
	}

	// 1フレーム目は前フレームが無く、そのままだと未初期化のメモリを読むことになる。
	// 割り当て直後に一度だけ両方をクリアしておく
	void RenderGraph::ClearTemporalResources(RenderContext* a_pRenderContext)
	{
		if (!a_pRenderContext) return;

		D3D12::GraphicsCommandList* _pCmdList = a_pRenderContext->RefCurrentCmdList();
		if (!_pCmdList) return;

		if (!m_pHeapManager) return;
		auto& _heapManager = *m_pHeapManager;

		for (const VirtualResource& _virtual : m_upResourceRegistry->GetVirtualResources())
		{
			if (!_virtual.IsTemporal()) continue;
			if (_virtual.IsBuffer()) continue;		// バッファは書き手が埋める前提

			const ResourceID _resourceID = _virtual.GetResourceID();

			for (uint32_t _slice = 0; _slice < _virtual.GetEntityCount(); ++_slice)
			{
				D3D12::GPUResource* _pResource = RefGPUResource(_resourceID, _slice);
				if (!_pResource) continue;

				if (_virtual.HasUsage(Resource::ETextureUsage::RTV))
				{
					_pResource->Barrier(_pCmdList, D3D12_RESOURCE_STATE_RENDER_TARGET);
					a_pRenderContext->ClearRenderTarget(_heapManager.GetCPU(_pResource->GetRTV()));
				}
				else if (_virtual.HasUsage(Resource::ETextureUsage::DSV))
				{
					_pResource->Barrier(_pCmdList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
					a_pRenderContext->ClearDSV(_heapManager.GetCPU(_pResource->GetDSV()));
				}
				else if (_virtual.HasUsage(Resource::ETextureUsage::UAV))
				{
					// 履歴(TAA・デノイズ)はどれもUAVのテクスチャ。
					// ここを消さないと、割り当て直後の1枚目が前に使われていた絵のまま残り、
					// 履歴を混ぜ続けるパスがいつまでもその残骸を引きずる
					_pResource->Barrier(_pCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					const float _clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					a_pRenderContext->ClearUAV(
						_pResource->GetUAV(),
						_pResource->GetResource(),
						_clearColor);
				}

				// クリアのために動かしたぶんを入口のステートへ戻す
				_pResource->Barrier(_pCmdList, _virtual.GetInitialState(_slice));
			}
		}
	}

	// 宣言から焼き込んだものを張る。
	// 実行時に文字列やスロットを引き直すことはもう無い
	void RenderGraph::ApplyStaticBindings(RenderContext* a_pRenderContext, const CompiledPass& a_compiledPass, uint32_t a_parity)
	{
		Pass* _pPass = a_compiledPass.pPass;
		if (!_pPass || !a_pRenderContext) return;

		const bool _isCompute = (_pPass->GetPipelineType() == EPassPipelineType::Compute);

		// ヒープ : シェーダー可視のヒープとサンプラーヒープを張るだけ(コピーはしない)
		if (_pPass->GetHeapMode() == EPassHeapMode::Bindless)
		{
			a_pRenderContext->BindBindlessHeaps();
		}

		// ルートシグネチャ : ハンドルから実体を引くのは使う直前
		if (_pPass->GetRootSignature().IsValid())
		{
			if (_isCompute)	a_pRenderContext->SetComputeRootSignature(_pPass->GetRootSignature());
			else			a_pRenderContext->SetGraphicsRootSignature(_pPass->GetRootSignature());
		}

		// パイプラインステート
		if (_pPass->GetPSOHandle().IsValid())
		{
			if (_isCompute)	a_pRenderContext->SetComputePSO(_pPass->GetPSOHandle());
			else			a_pRenderContext->SetGraphicPSO(_pPass->GetPSOHandle());
		}

		// ルートシグネチャが無いとルート定数は張れない
		if (a_compiledPass.binds.empty()) return;
		if (!_pPass->GetRootSignature().IsValid()) return;

		//------------------------------------------------------------------------------
		// ビューの番号をルート定数で渡す。
		// ルートパラメータの位置はスロットが宣言した番号のまま
		// (シェーダー側は同じ位置に RootConstants を置き、cbuffer の uint で受けて
		//  ResourceDescriptorHeap[番号] で引く)
		//------------------------------------------------------------------------------
		const auto& _indices = a_compiledPass.descriptorIndex[a_parity];
		for (const PassBind& _bind : a_compiledPass.binds)
		{
			if (static_cast<size_t>(_bind.firstIndex) + _bind.count > _indices.size()) continue;

			std::span<const UINT> _span(_indices.data() + _bind.firstIndex, _bind.count);
			if (_isCompute)	a_pRenderContext->ComputeBindDescriptorIndices(_bind.rootIndex, _span);
			else			a_pRenderContext->GraphicsBindDescriptorIndices(_bind.rootIndex, _span);
		}
	}

	// 各パスの出力先を焼き込む。
	// 実行時にスロットから引き直すと毎フレーム同じ探索を繰り返すことになる
	void RenderGraph::ResolveDescriptors()
	{
		if (!m_pHeapManager) return;
		auto& _heapManager = *m_pHeapManager;

		// スロットとフレームの偶奇から実体を引く。
		// Temporal でなければ偶奇に関わらず同じものになる
		auto _refResource = [this](const Slot& a_slot, uint32_t a_parity) -> D3D12::GPUResource*
			{
				const VirtualResource* _pVirtual = GetVirtualResource(a_slot.resourceID);
				if (!_pVirtual) return nullptr;

				const uint32_t _slice = VirtualResource::ToSlice(a_slot, _pVirtual->IsTemporal());
				const uint32_t _index = _pVirtual->IsTemporal() ? ((a_parity + _slice) & 1u) : 0u;

				return RefGPUResource(a_slot.resourceID, _index);
			};

		for (CompiledPass& _compiledPass : m_compilePasses)
		{
			_compiledPass.rtvHandles[0].clear();
			_compiledPass.rtvHandles[1].clear();
			_compiledPass.clearRtvIndices.clear();
			_compiledPass.clearRtvColors.clear();
			_compiledPass.dsvHandle[0] = { 0 };
			_compiledPass.dsvHandle[1] = { 0 };
			_compiledPass.hasDSV = false;
			_compiledPass.isDepthClear = false;

			Pass* _pPass = _compiledPass.pPass;
			if (!_pPass) continue;

			// PSOを焼くのに要る出力フォーマット。偶奇で変わらないので片方だけ集める
			std::vector<DXGI_FORMAT> _rtvFormatVec = {};
			DXGI_FORMAT _dsvFormat = DXGI_FORMAT_UNKNOWN;

			for (uint32_t _parity = 0; _parity < 2; ++_parity)
			{
				// この偶奇で書き込みDSVを決めたか。
				// 偶奇をまたいで持ち越すと、片方の焼き込みが丸ごと抜ける
				bool _isDepthWriteBound = false;

				// ---- 出力側 : RTV / DSV ----
				for (const Slot& _out : _pPass->GetOutputSlots())
				{
					if (_parity == 0)
					{
						const VirtualResource* _pVirtual = GetVirtualResource(_out.resourceID);
						const DXGI_FORMAT _format = _pVirtual ? _pVirtual->GetFormat() : DXGI_FORMAT_UNKNOWN;

						if (_out.accessType == EAccessType::RTV)				_rtvFormatVec.push_back(_format);
						else if (_out.accessType == EAccessType::DepthWrite)	_dsvFormat = _format;
					}

					D3D12::GPUResource* _pResource = _refResource(_out, _parity);
					if (!_pResource) continue;

					if (_out.accessType == EAccessType::RTV)
					{
						_compiledPass.rtvHandles[_parity].push_back(_heapManager.GetCPU(_pResource->GetRTV()));

						// クリア対象の並びは偶奇で変わらないので、片方だけ数える
						if (_parity == 0 && _out.loadOp == ELoadOp::Clear)
						{
							_compiledPass.clearRtvIndices.push_back(_compiledPass.rtvHandles[0].size() - 1);

							// 色は仮想リソース側から引く。
							// 生成時のクリアバリューもここから作っているので、
							// 同じものを渡さないとドライバの最適化が効かず警告も出る
							const VirtualResource* _pClearTarget = GetVirtualResource(_out.resourceID);
							_compiledPass.clearRtvColors.push_back(
								_pClearTarget ? _pClearTarget->GetClearColor() : _out.clearColor);
						}
					}
					else if (_out.accessType == EAccessType::DepthWrite)
					{
						_compiledPass.dsvHandle[_parity] = _heapManager.GetCPU(_pResource->GetDSV());
						_compiledPass.hasDSV = true;
						_isDepthWriteBound = true;

						if (_out.loadOp == ELoadOp::Clear) _compiledPass.isDepthClear = true;
					}
				}

				// ---- 入力側 : 深度を読むだけのパスは読み取り専用DSVを張る ----
				//
				// 出力側で書き込みDSVを決めていたらそちらを優先する。
				// ここで見るのは「この偶奇で決めたか」であって hasDSV ではない。
				// hasDSV は偶奇をまたいで立ちっぱなしになるので、それで打ち切ると
				// 偶数フレームぶんだけ焼いて奇数フレームぶんが 0 のまま残り、
				// 深度を読むだけのパス(パーティクル・デバッグ線)が
				// 空のDSVで OMSetRenderTargets を叩いて落ちる
				if (_isDepthWriteBound) continue;

				for (const Slot& _in : _pPass->GetInputSlots())
				{
					if (_in.accessType != EAccessType::DepthRead) continue;

					// 深度を読むだけのパスも、モデルを描くなら PSO に深度のフォーマットが要る
					// (半透明パス : 深度テストだけして書かない)。
					// 入れないと PSO は深度なしで焼かれ、張った DSV と食い違って描画が落とされる
					if (_parity == 0 && _dsvFormat == DXGI_FORMAT_UNKNOWN)
					{
						const VirtualResource* _pVirtual = GetVirtualResource(_in.resourceID);
						if (_pVirtual) _dsvFormat = _pVirtual->GetFormat();
					}

					D3D12::GPUResource* _pResource = _refResource(_in, _parity);
					if (!_pResource) continue;

					_compiledPass.dsvHandle[_parity] = _heapManager.GetCPU(_pResource->GetReadOnlyDSV());
					_compiledPass.hasDSV = true;
					break;
				}
			}

			// 出力フォーマットが決まったので、このパスのPSOを組めるようにする。
			//
			// 鍵にするのはパスが名乗る名前。
			// 名乗らないパス(モデルを受け取らないパス)は表示名で構わない
			const char* _pShadingName = _pPass->GetShadingPassName();
			const std::string& _psoKeyName = _pShadingName ? std::string(_pShadingName) : _pPass->GetName();

			_pPass->RefPipelineBuilder().Init(_rtvFormatVec, _dsvFormat, _psoKeyName);

			//------------------------------------------------------------------------------
			// SRV / UAV の番号
			//
			// ルートパラメータ番号を指定したスロットだけを、番号ごとにまとめて並べる。
			// 入力を先に見るのは、宣言順がそのままルート定数の並びになるため
			//------------------------------------------------------------------------------
			_compiledPass.descriptorIndex[0].clear();
			_compiledPass.descriptorIndex[1].clear();
			_compiledPass.binds.clear();

			// 同じ番号のスロットを集める(番号の小さい順に張る)
			std::map<int, std::vector<const Slot*>> _rootSlotMap = {};
			for (const Slot& _in : _pPass->GetInputSlots())
			{
				if (_in.rootParamIndex < 0) continue;
				_rootSlotMap[_in.rootParamIndex].push_back(&_in);
			}
			for (const Slot& _out : _pPass->GetOutputSlots())
			{
				if (_out.rootParamIndex < 0) continue;
				_rootSlotMap[_out.rootParamIndex].push_back(&_out);
			}

			for (const auto& [_rootIndex, _slotVec] : _rootSlotMap)
			{
				PassBind _bind = {};
				_bind.rootIndex = static_cast<UINT>(_rootIndex);
				_bind.firstIndex = static_cast<uint16_t>(_compiledPass.descriptorIndex[0].size());
				_bind.count = static_cast<uint16_t>(_slotVec.size());

				for (uint32_t _parity = 0; _parity < 2; ++_parity)
				{
					for (const Slot* _pSlot : _slotVec)
					{
						D3D12::GPUResource* _pResource = _refResource(*_pSlot, _parity);
						if (!_pResource)
						{
							_compiledPass.descriptorIndex[_parity].push_back(CompiledPass::INVALID_DESCRIPTOR_INDEX);
							continue;
						}

						switch (_pSlot->accessType)
						{
						case EAccessType::UAV:
							_compiledPass.descriptorIndex[_parity].push_back(_pResource->GetUAV().GetIndex());
							break;

						// 深度を読むときも、シェーダーからは SRV として引く
						case EAccessType::SRV:
						case EAccessType::DepthRead:
						default:
							_compiledPass.descriptorIndex[_parity].push_back(_pResource->GetSRV().GetIndex());
							break;
						}
					}
				}

				_compiledPass.binds.push_back(_bind);
			}
		}
	}

	// リソースが揃ったところで各パスのランタイムデータを構築させる
	void RenderGraph::CompilePasses(GraphicsEngine* a_pGraphicsEngine)
	{
		PassContext _context = MakeContext(a_pGraphicsEngine);

		for (CompiledPass& _compiledPass : m_compilePasses)
		{
			if (!_compiledPass.pPass) continue;
			_compiledPass.pPass->Compile(_context);
		}
	}

	//======================================================================================
	// パスへ渡すコンテキストを組む
	//
	// シングルトンを引くのはここだけ。
	// パスの中で Instance() を呼ぶと、そのパスが何に依存しているのかが
	// 呼び出し側から見えなくなるので、要るものはこのコンテキストへ足して配る
	//======================================================================================
	PassContext RenderGraph::MakeContext(GraphicsEngine* a_pGraphicsEngine)
	{
		auto& _mainEngine = MainEngine::Instance();

		PassContext _context = {};

		// ---- アプリ寿命のもの ----
		_context.pMainEngine		= &_mainEngine;
		_context.pResourceManager	= a_pGraphicsEngine ? a_pGraphicsEngine->RefResourceManager() : nullptr;
		_context.pAssetDatabase		= _context.pResourceManager ? &_context.pResourceManager->RefAssetDatabase() : nullptr;
		_context.pRayEngine			= a_pGraphicsEngine ? a_pGraphicsEngine->RefRayEngine() : nullptr;
		_context.pParticleManager	= a_pGraphicsEngine ? a_pGraphicsEngine->RefParticleManager() : nullptr;
		_context.pHeapManager		= m_pHeapManager;

		// ---- 描画系 ----
		_context.pGraph				= this;
		_context.pGraphicsEngine	= a_pGraphicsEngine;

		// RenderContext / コマンドリストは実行時にしか無いので、Execute 側で足す

		return _context;
	}

	//======================================================================================
	//
	// バリア構築
	//
	//======================================================================================

	// 積んであるバリアへ物理リソースのポインタを埋める。
	// バリアを積む時点(Compile)ではまだ実体が無いので、割り当てが済んでから解決する
	void RenderGraph::ResolveBarrierResources()
	{
		auto _resolve = [this](std::vector<ResourceBarrier>& a_barrierVec)
			{
				for (ResourceBarrier& _barrier : a_barrierVec)
				{
					const VirtualResource* _pVirtual = GetVirtualResource(_barrier.resourceID);
					if (!_pVirtual)
					{
						_barrier.pResource[0] = nullptr;
						_barrier.pResource[1] = nullptr;
						continue;
					}

					// Temporal はフレームの偶奇で触る物理が入れ替わるので両方を焼き込む。
					// そうでなければどちらも同じ実体になる
					for (uint32_t _parity = 0; _parity < 2; ++_parity)
					{
						const uint32_t _index = _pVirtual->IsTemporal()
							? ((_parity + _barrier.slice) & 1u)
							: 0u;

						_barrier.pResource[_parity] = RefGPUResource(_barrier.resourceID, _index);
					}
				}
			};

		for (CompiledPass& _compiledPass : m_compilePasses)
		{
			_resolve(_compiledPass.preBarriers);
		}
		_resolve(m_endBarriers);

		//----------------------------------------------------------------------------------
		// エイリアシングバリア
		//
		// 使い回しの対象からは履歴つき(Temporal)を外してあるので、
		// フレームの偶奇で実体が入れ替わることはない。両方に同じものを入れる。
		//
		// before は席の一人目だと空の識別子で、引くと nullptr になる。
		// D3D12 ではこれが「このヒープのどれかが前の使い手」の意味になるので、
		// 引けなかったこと自体は異常ではない
		//----------------------------------------------------------------------------------
		for (CompiledPass& _compiledPass : m_compilePasses)
		{
			for (AliasingBarrier& _barrier : _compiledPass.preAliasingBarriers)
			{
				D3D12::GPUResource* _pBefore = RefGPUResource(_barrier.before, 0);
				D3D12::GPUResource* _pAfter = RefGPUResource(_barrier.after, 0);

				_barrier.pBeforeResource[0] = _pBefore;
				_barrier.pBeforeResource[1] = _pBefore;
				_barrier.pAfterResource[0] = _pAfter;
				_barrier.pAfterResource[1] = _pAfter;
			}
		}
	}
}
