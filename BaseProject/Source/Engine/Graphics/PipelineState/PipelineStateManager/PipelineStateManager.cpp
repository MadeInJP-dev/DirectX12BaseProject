#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"

// CD3DX12_* のヘルパーと D3DReadFileToBlob / D3DGetBlobPart はここだけで使う。
// プリコンパイル済みヘッダーへ置くと全翻訳単位に広がるため
#pragma warning(push, 0)
#include "d3dx12.h"
#include <d3dcompiler.h>
#pragma warning(pop)
namespace Engine::Graphics
{
	void PipelineStateManager::Init(D3D12::Device* a_pDevice)
	{
		m_pDevice = a_pDevice;
		m_rootSigHashMap = {};
		m_cpRootSigVec = {};
		m_psoMap = {};
		m_psoHashMap = {};
	}
	void PipelineStateManager::Release()
	{
		m_pDevice = nullptr;

		// ルートシグネチャ解放
		m_rootSigHashMap.clear();
		for (auto& _cpRootSig : m_cpRootSigVec)
		{
			if (_cpRootSig)
			{
				_cpRootSig.Reset();
			}
		}
		m_cpRootSigVec.clear();

		// PSO解放
		m_pPsoVec.clear();
		m_psoHashMap.clear();
		for (auto& [_hash, _cpPSO] : m_psoMap)
		{
			if (_cpPSO)
			{
				_cpPSO.Reset();
			}
		}

	}
	Handle<ID3D12RootSignature> PipelineStateManager::RegisterRootSignature(
		uint64_t a_hash, const ComPtr<ID3D12RootSignature>& a_cpRootSig)
	{
		if (!a_cpRootSig) return {};

		// 同じ内容が既にあればそのハンドルを使い回す
		auto _it = m_rootSigHashMap.find(a_hash);
		if (_it != m_rootSigHashMap.end())
		{
			return _it->second;
		}

		// ハンドルを発行して、そのインデックスの位置へ実体を置く
		auto _handle = m_rootSigHandlePool.Allocate();
		if (m_cpRootSigVec.size() <= _handle.GetIndex())
		{
			m_cpRootSigVec.resize(static_cast<size_t>(_handle.GetIndex()) + 1);
		}
		m_cpRootSigVec[_handle.GetIndex()] = a_cpRootSig;

		m_rootSigHashMap[a_hash] = _handle;
		return _handle;
	}

	ID3D12RootSignature* PipelineStateManager::GetRootSignature(const Handle<ID3D12RootSignature>& a_handle) const
	{
		if (!m_rootSigHandlePool.IsValid(a_handle)) return nullptr;
		if (m_cpRootSigVec.size() <= a_handle.GetIndex()) return nullptr;

		return m_cpRootSigVec[a_handle.GetIndex()].Get();
	}

	Handle<ID3D12RootSignature> PipelineStateManager::Request(const D3D12::RootSignatureDesc& a_desc)
	{
		// ハッシュを求める
		uint64_t _hash = CalcHash(a_desc);

		// キャッシュ検索
		auto _it = m_rootSigHashMap.find(_hash);
		if(_it != m_rootSigHashMap.end())
		{
			return _it->second;
		}

		// キャッシュになければ作成
		ComPtr<ID3D12RootSignature> _rootSig = D3D12::RootSignatureBuilder::Create(m_pDevice, a_desc);
		if (!_rootSig)
		{
			ENGINE_ERRLOG(false, "ルートシグネチャの生成に失敗 : %s", a_desc.name.c_str());
			return {};
		}

		// 名前があれば
		if (!a_desc.name.empty())
		{
			_rootSig->SetName(Core::String::ToWideString(a_desc.name).c_str());
		}

		// 登録してハンドルを返す
		return RegisterRootSignature(_hash, _rootSig);
	}
	Handle<ID3D12RootSignature> PipelineStateManager::Request(const std::string& a_shaderPath)
	{
		// バイナリデータ
		ComPtr<ID3DBlob> _cpBlob = nullptr;
		ComPtr<ID3DBlob> _cpRootSigBlob = nullptr;

		// シェーダーバイトコードの読み込み
		auto _hr = D3DReadFileToBlob(
			Core::String::ToWideString(a_shaderPath).c_str(),
			_cpBlob.ReleaseAndGetAddressOf()
		);
		if (FAILED(_hr))
		{
			ENGINE_ERRLOG(false, "ルートシグネチャ生成用のシェーダーファイル読み込みに失敗");
			return {};
		}

		// シェーダーバイトコードの読み込み
		_hr = D3DGetBlobPart(
			_cpBlob->GetBufferPointer(),
			_cpBlob->GetBufferSize(),
			D3D_BLOB_ROOT_SIGNATURE,
			0,
			&_cpRootSigBlob
		);
		if (FAILED(_hr))
		{
			// 抽出に失敗。無効ハンドルを返す(以前は何も返さず未定義動作だった)
			ENGINE_ERRLOG(false, ".cso内にRootSignatureが見つかりませんでした : %s", a_shaderPath.c_str());
			return {};
		}

		// シェーダーからルートシグネチャの抽出に成功
		// ルートシグネチャの部分からハッシュ値を求める
		uint64_t _hash = CalcHash(_cpRootSigBlob->GetBufferPointer(), _cpRootSigBlob->GetBufferSize());

		// すでに構築されているルートシグネチャならそのハンドルを返す
		auto _it = m_rootSigHashMap.find(_hash);
		if (_it != m_rootSigHashMap.end())
		{
			return _it->second;
		}

		// なければ生成
		ComPtr<ID3D12RootSignature> _rootSig = D3D12::RootSignatureBuilder::Create(m_pDevice, _cpRootSigBlob);
		if (!_rootSig)
		{
			ENGINE_ERRLOG(false, ".cso内にRootSignatureが見つかりませんでした");
			return {};
		}

		// 名前を付ける
		_rootSig->SetName(Core::String::ToWideString(a_shaderPath).c_str());

		// 登録してハンドルを返す
		return RegisterRootSignature(_hash, _rootSig);
	}
	Handle<ID3D12RootSignature> PipelineStateManager::Request(ID3DBlob* a_pShaderBlob)
	{
		if (!a_pShaderBlob) return {};

		ComPtr<ID3DBlob> _cpRootSigBlob = nullptr;
		// シェーダーバイトコードの読み込み
		auto _hr = D3DGetBlobPart(
			a_pShaderBlob->GetBufferPointer(),
			a_pShaderBlob->GetBufferSize(),
			D3D_BLOB_ROOT_SIGNATURE,
			0,
			&_cpRootSigBlob
		);
		if (FAILED(_hr))
		{
			// 抽出に失敗。無効ハンドルを返す(以前は何も返さず未定義動作だった)
			ENGINE_ERRLOG(false, "シェーダー内にRootSignatureが見つかりませんでした");
			return {};
		}

		// シェーダーからルートシグネチャの抽出に成功
		// ルートシグネチャの部分からハッシュ値を求める
		uint64_t _hash = CalcHash(_cpRootSigBlob->GetBufferPointer(), _cpRootSigBlob->GetBufferSize());

		// すでに構築されているルートシグネチャならそのハンドルを返す
		auto _it = m_rootSigHashMap.find(_hash);
		if (_it != m_rootSigHashMap.end())
		{
			return _it->second;
		}

		// なければ生成
		ComPtr<ID3D12RootSignature> _rootSig = D3D12::RootSignatureBuilder::Create(m_pDevice, _cpRootSigBlob);
		if (!_rootSig)
		{
			ENGINE_ERRLOG(false, ".cso内にRootSignatureが見つかりませんでした");
			return {};
		}

		// 登録してハンドルを返す
		return RegisterRootSignature(_hash, _rootSig);
	}
	ID3D12PipelineState* PipelineStateManager::Request(const D3D12::GraphicsPipelineDesc& a_desc)
	{
		uint64_t _hash = 0;
		return AcquirePSO(a_desc, _hash);
	}

	ID3D12PipelineState* PipelineStateManager::AcquirePSO(const D3D12::GraphicsPipelineDesc& a_desc, uint64_t& a_outHash)
	{
		// ハッシュを求める
		uint64_t _hash = CalcHash(&a_desc.desc,sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		a_outHash = _hash;

		// キャッシュ検索
		if (m_psoMap.contains(_hash))
		{
			return m_psoMap[_hash].Get();
		}

		// キャッシュになければ、ComPtrを作成。
		// 実行中に初めて要求されたときはその間フレームが止まるので、遅いフレームの記録に出す
		ENGINE_HITCH_SCOPE("PSO", 0.1, a_desc.name.empty() ? std::string("(Graphics)") : a_desc.name);
		ComPtr<ID3D12PipelineState> _pso;
		auto _hr = m_pDevice->CreateGraphicsPipelineState(&a_desc.desc,IID_PPV_ARGS(&_pso));
		if (FAILED(_hr))
		{
			ENGINE_ERRLOG(false, "PSOの作成失敗");
			return nullptr;
		}

		// 名前があれば
		if (!a_desc.name.empty())
		{
			_pso->SetName(Core::String::ToWideString(a_desc.name).c_str());
		}

		// マップに保存して生ポインタを返す
		m_psoMap[_hash] = _pso;
		return _pso.Get();
	}

	ID3D12PipelineState* PipelineStateManager::Request(const D3D12::ComputePipelineDesc& a_desc)
	{
		uint64_t _hash = 0;
		return AcquirePSO(a_desc, _hash);
	}

	ID3D12PipelineState* PipelineStateManager::AcquirePSO(const D3D12::ComputePipelineDesc& a_desc, uint64_t& a_outHash)
	{
		// ハッシュを求める
		// 混ぜるのはコンピュート用の構造体ぶんだけ。
		// グラフィックス用の大きさで読むと構造体の外まで舐めてしまい、
		// 同じ内容でも呼ぶたびに違うハッシュになりうる
		uint64_t _hash = CalcHash(&a_desc.desc, sizeof(D3D12_COMPUTE_PIPELINE_STATE_DESC));
		a_outHash = _hash;

		// キャッシュ検索
		if (m_psoMap.contains(_hash))
		{
			return m_psoMap[_hash].Get();
		}

		// キャッシュになければ、ComPtrを作成。
		// 実行中に初めて要求されたときはその間フレームが止まるので、遅いフレームの記録に出す
		ENGINE_HITCH_SCOPE("PSO", 0.1, a_desc.name.empty() ? std::string("(Compute)") : a_desc.name);
		ComPtr<ID3D12PipelineState> _pso;
		auto _hr = m_pDevice->CreateComputePipelineState(&a_desc.desc, IID_PPV_ARGS(&_pso));
		if (FAILED(_hr))
		{
			ENGINE_ERRLOG(false, "PSOの作成失敗");
			return nullptr;
		}

		// 名前があれば
		if (!a_desc.name.empty())
		{
			_pso->SetName(Core::String::ToWideString(a_desc.name).c_str());
		}

		// マップに保存して生ポインタを返す
		m_psoMap[_hash] = _pso;
		return _pso.Get();
	}

	ID3D12PipelineState* PipelineStateManager::Request(const D3D12::RenderPipelineBuilder& a_builder)
	{
		uint64_t _hash = 0;
		return AcquirePSO(a_builder, _hash);
	}

	ID3D12PipelineState* PipelineStateManager::AcquirePSO(const D3D12::RenderPipelineBuilder& a_builder, uint64_t& a_outHash)
	{
		// 安全対策：シェーダーがセットされていない場合はクラッシュするので弾く
		bool _hasVS = (a_builder.GetVS().pShaderBytecode != nullptr && a_builder.GetVS().BytecodeLength > 0);
		bool _hasMS = (a_builder.GetMS().pShaderBytecode != nullptr && a_builder.GetMS().BytecodeLength > 0);

		// ストリーム構造体の組み立て : ゼロクリアしてパディングのゴミを消す
		//
		// シェーダーが無いときも先に組み立ててハッシュまで出す。
		// 作れなかったことをそのハッシュに覚えさせておけば、
		// 同じ要求が何度来ても新しい番号を配らずに済む
		RenderPipelineStateStream _streamDesc = {};

		_streamDesc.pRootSignature = GetRootSignature(a_builder.GetRootSignatureHandle());
		_streamDesc.PrimitiveTopologyType = a_builder.GetPrimitiveTopologyType();
		_streamDesc.PS = CD3DX12_SHADER_BYTECODE(a_builder.GetPS());

		// MSを持っているかでストリームに詰めるパーツを変える
		if (a_builder.HasMeshShader())
		{
			_streamDesc.MS = CD3DX12_SHADER_BYTECODE(a_builder.GetMS());
			if (a_builder.HasAmplificationShader()) {
				_streamDesc.AS = CD3DX12_SHADER_BYTECODE(a_builder.GetAS());
			}
		}
		else
		{
			_streamDesc.VS = CD3DX12_SHADER_BYTECODE(a_builder.GetVS());
			_streamDesc.InputLayout = a_builder.GetInputLayout(); // VSの場合はInputLayoutが必要
		}

		_streamDesc.BlendState = CD3DX12_BLEND_DESC(a_builder.GetBlendState());
		_streamDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(a_builder.GetRasterizerState());
		_streamDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(a_builder.GetDepthStencilState());

		// RTVフォーマットは構造体(D3D12_RT_FORMAT_ARRAY)に直して渡す
		D3D12_RT_FORMAT_ARRAY _rtvFormatArray = {};
		_rtvFormatArray.NumRenderTargets = a_builder.GetNumRenderTargets();
		for (UINT i = 0; i < a_builder.GetNumRenderTargets(); ++i)
		{
			_rtvFormatArray.RTFormats[i] = a_builder.GetRenderTargetFormats()[i];
		}
		_streamDesc.RTVFormats = _rtvFormatArray;
		_streamDesc.DSVFormat = a_builder.GetDepthStencilFormat();
		_streamDesc.SampleDesc = a_builder.GetSampleDesc();

		// ハッシュ計算
		// ストリーム構造体自体をハッシュ化すれば、VS版/MS版どちらでも一意のハッシュになる
		uint64_t _hash = CalcHash(&_streamDesc, sizeof(_streamDesc));
		a_outHash = _hash;

		// キャッシュにあれば返す
		if (m_psoMap.contains(_hash)) {
			return m_psoMap[_hash].Get();
		}

		if (!_hasVS && !_hasMS)
		{
			// まだシェーダーの準備ができていないので生成をスキップ。
			// 空の席を登録して、同じ要求が繰り返し来ても警告とハンドルが増えないようにする。
			// シェーダーが読めたあとはバイトコードが変わってハッシュも変わるので、
			// ここへ入れた空の席が生成の邪魔をすることはない
			ENGINE_WARNING("シェーダーが読み込まれていないためPSOが作成できません : レンダー");
			m_psoMap[_hash] = nullptr;
			return nullptr;
		}

		// ストリーム構造体をDirectX側に登録
		D3D12_PIPELINE_STATE_STREAM_DESC _streamInfo = {};
		_streamInfo.SizeInBytes = sizeof(_streamDesc);
		_streamInfo.pPipelineStateSubobjectStream = &_streamDesc;

		ComPtr<ID3D12PipelineState> _pPso;
		// 従来の CreateGraphicsPipelineState ではなく、Stream を使う CreatePipelineState を呼ぶ
		HRESULT hr = m_pDevice->CreatePipelineState(&_streamInfo, IID_PPV_ARGS(&_pPso));

		if (FAILED(hr)) {
			ENGINE_ERRLOG(false, "パイプラインステート(Render)の生成に失敗");
			return nullptr;
		}

		// マップに登録して返す
		m_psoMap[_hash] = _pPso;

		ENGINE_LOG("パイプラインステートの作成に成功 : レンダー");
		return _pPso.Get();
	}

	//======================================================================================
	// ハッシュに対応するハンドルを配る
	//
	// 内容が同じPSOには同じ番号を返す。要求のたびに新しい番号を取ると、
	// パイプラインを組み直すだけで番号が増え続け、
	// 描画アイテムのソートキー(PSO番号)に収まらなくなる
	//======================================================================================
	Handle<ID3D12PipelineState> PipelineStateManager::RegisterPSO(uint64_t a_hash, ID3D12PipelineState* a_pPSO)
	{
		// 同じ内容が既にあればそのハンドルを使い回す
		auto _it = m_psoHashMap.find(a_hash);
		if (_it != m_psoHashMap.end() && m_psoHandlePool.IsValid(_it->second))
		{
			const uint16_t _idx = _it->second.GetIndex();

			// 前回は作れず空席のままだったものが、今回は作れたときは実体を入れ直す
			if (a_pPSO && _idx < m_pPsoVec.size() && !m_pPsoVec[_idx])
			{
				m_pPsoVec[_idx] = a_pPSO;
			}
			return _it->second;
		}

		// ハンドルを発行して、そのインデックスの位置へ実体を置く
		auto _handle = m_psoHandlePool.Allocate();
		if (!_handle.IsValid()) return {};

		if (m_pPsoVec.size() <= _handle.GetIndex())
		{
			m_pPsoVec.resize(static_cast<size_t>(_handle.GetIndex()) + 1, nullptr);
		}
		m_pPsoVec[_handle.GetIndex()] = a_pPSO;

		m_psoHashMap[a_hash] = _handle;
		return _handle;
	}

	Handle<ID3D12PipelineState> PipelineStateManager::RequestHandle(
		const D3D12::GraphicsPipelineDesc& a_desc
	)
	{
		uint64_t _hash = 0;
		ID3D12PipelineState* _pPSO = AcquirePSO(a_desc, _hash);
		return RegisterPSO(_hash, _pPSO);
	}

	Handle<ID3D12PipelineState> PipelineStateManager::RequestHandle(
		const D3D12::ComputePipelineDesc& a_desc
	)
	{
		uint64_t _hash = 0;
		ID3D12PipelineState* _pPSO = AcquirePSO(a_desc, _hash);
		return RegisterPSO(_hash, _pPSO);
	}

	Handle<ID3D12PipelineState> PipelineStateManager::RequestHandle(const D3D12::RenderPipelineBuilder& a_builder)
	{
		uint64_t _hash = 0;
		ID3D12PipelineState* _pPSO = AcquirePSO(a_builder, _hash);
		return RegisterPSO(_hash, _pPSO);
	}

	ID3D12PipelineState* PipelineStateManager::GetPSO(Handle<ID3D12PipelineState> a_handle) const
	{
		if (m_psoHandlePool.IsValid(a_handle))
		{
			return m_pPsoVec[a_handle.GetIndex()];
		}
		return nullptr;
	}

	ID3D12PipelineState* PipelineStateManager::GetPSO(uint16_t a_rawIndex) const
	{
		// 生添字は描画アイテムのソートキーから来る。
		// 無効ハンドルの添字(0xFFFF)がそのまま届くので、範囲外はここで止める。
		// 呼び出し側は nullptr を「張らない」として扱う
		if (a_rawIndex >= m_pPsoVec.size()) return nullptr;

		return m_pPsoVec[a_rawIndex];
	}

	uint64_t PipelineStateManager::CalcHash(const void* a_pData, size_t a_size)
	{
		const uint8_t* _ptr = static_cast<const uint8_t*>(a_pData);
		uint64_t _hash = 14695981039346656037ull;
		for (size_t _i = 0; _i < a_size; ++_i)
		{
			_hash ^= _ptr[_i];
			_hash *= 1099511628211ull;
		}
		return _hash;
	}
	uint64_t PipelineStateManager::CalcHash(const D3D12::RootSignatureDesc& a_desc)
	{
		uint64_t _hash = 14695981039346656037ull; // FNV offset basis

		// ヘルパー：既存の CalcHash のロジックを再利用してハッシュを更新する
		auto UpdateHash = [&](const void* p, size_t size) {
			const uint8_t* ptr = static_cast<const uint8_t*>(p);
			for (size_t i = 0; i < size; ++i) {
				_hash ^= ptr[i];
				_hash *= 1099511628211ull; // FNV prime
			}
		};

		// 基本メンバを混ぜる
		UpdateHash(&a_desc.flags, sizeof(a_desc.flags));
		UpdateHash(&a_desc.isUseStaticSampler, sizeof(a_desc.isUseStaticSampler));

		// vector の中身をループして、実体データを混ぜる
		for (const auto& param : a_desc.paramVec) {
			// Paramそのものをハッシュ化
			UpdateHash(&param.paramType, sizeof(param.paramType));
			UpdateHash(&param.shaderRegisterIndex, sizeof(param.shaderRegisterIndex));

			// rangeVec も vector なので、さらにループして中身を混ぜる
			for (const auto& range : param.rangeVec) {
				UpdateHash(&range, sizeof(range)); // rangeInitの中身が単純なら一気にいける
			}
		}

		return _hash;
	}
}