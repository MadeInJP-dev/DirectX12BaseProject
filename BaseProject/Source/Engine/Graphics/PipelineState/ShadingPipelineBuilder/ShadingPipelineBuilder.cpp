#include "Engine/Graphics/PipelineState/ShadingPipelineBuilder/ShadingPipelineBuilder.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Graphics/PipelineState/PipelineStateManager/PipelineStateManager.h"
#include "Engine/Graphics/D3D12/InputLayout.h"

namespace Engine::Graphics
{

	void ShadingPipelineBuilder::Init(const std::vector<DXGI_FORMAT>& a_rtvFormat, DXGI_FORMAT a_dsvFormat, UINT a_passNameHash)
	{
		m_rtvFormats = a_rtvFormat;
		m_dsvFormat = a_dsvFormat;
		if (DXGI_FORMAT_R32_TYPELESS == a_dsvFormat)
		{
			m_dsvFormat = DXGI_FORMAT_D32_FLOAT;
		}
		m_passNameHash = a_passNameHash;
	}


	//======================================================================================
	// キーに対応するPSOを引く。無ければ組んで登録する
	//
	// ここはパイプラインを組み直すたびに、パス数 × キー数だけ通る。
	// ルートシグネチャはシェーダーの .cso から起こすが、実体はすでに
	// リソースマネージャーがメモリに持っているので、パスを引き直して
	// ファイルから読み直してはいけない
	//======================================================================================
	Handle<ID3D12PipelineState> ShadingPipelineBuilder::Request(PSOKey a_key, PipelineStateManager* a_pPSOManager, Resource::ResourceManager& a_resourceManager)
	{
		// キャッシュを検索
		auto _it = m_psoMap.find(a_key);
		if (_it != m_psoMap.end())
		{
			return _it->second; // すでに完成していればそれを返す
		}

		auto& _resourceManager = a_resourceManager;

		D3D12::RenderPipelineBuilder _builder;

		// 共通のステート・フォーマット設定
		_builder.DepthEnable(m_depthEnable);
		_builder.DepthWriteMask(m_depthWrite);
		_builder.DepthFunc(m_depthFunc);

		_builder.CullMode(m_cullMode);

		for (auto& _rtvFormat : m_rtvFormats) {
			_builder.AddRenderTargetFormat(_rtvFormat);
		}
		_builder.SetDepthStencilFormat(m_dsvFormat);

		// =========================================================
		// VS / MS / AS の解決
		//
		// シェーダーがまだ読めていないときは、キーを覚えずに帰る。
		// ここで覚えてしまうと、あとから読めてもこのキーは空のまま固定され、
		// 次に組み直すまでそのマテリアルが描かれなくなる
		// =========================================================
		bool _useMeshShader = (a_key.permutationFlags & static_cast<uint32_t>(EShaderPermutationFlags::MeshShader));
		if (_useMeshShader)
		{
			Handle<Resource::Shader> _targetMSHandle;
			Handle<Resource::Shader> _targetASHandle;

			if (a_key.permutationFlags & static_cast<uint32_t>(EShaderPermutationFlags::Skinned))
			{
				_targetMSHandle = m_msMap[EShaderPermutationFlags::Skinned];
				_targetASHandle = m_asMap[EShaderPermutationFlags::Skinned];
			}
			else if (a_key.permutationFlags & static_cast<uint32_t>(EShaderPermutationFlags::UseGPUInstancing))
			{
				_targetMSHandle = m_msMap[EShaderPermutationFlags::UseGPUInstancing];
				_targetASHandle = m_asMap[EShaderPermutationFlags::UseGPUInstancing];
			}
			else {
				_targetMSHandle = m_msMap[EShaderPermutationFlags::Static];
				_targetASHandle = m_asMap[EShaderPermutationFlags::Static];
			}

			// MSのセットとルートシグネチャの抽出。
			// ルートシグネチャはブロブから起こすので、非constで引く
			auto* _pMS = _resourceManager.Ref(_targetMSHandle);
			if (!_pMS || !_pMS->Get()) return {};

			_builder.SetRootSignature(a_pPSOManager->Request(_pMS->Get()));
			_builder.SetMS(_pMS->GetByteCode());

			// ASのセット（存在する場合のみ）
			if (auto* _pAS = _resourceManager.Get(_targetASHandle))
			{
				_builder.SetAS(_pAS->GetByteCode());
			}
		}
		else
		{
			// 従来の VS パイプライン
			Handle<Resource::Shader> _targetVSHandle;

			// アニメーションか、インスタンシングか、静的か等の優先順位でVSを決定
			if (a_key.permutationFlags & static_cast<uint32_t>(EShaderPermutationFlags::Skinned))
			{
				_targetVSHandle = m_vsMap[EShaderPermutationFlags::Skinned];
				_builder.SetInputLayout(D3D12::Input::ANIMATION_INPUT_LAYOUT);
			}
			else if (a_key.permutationFlags & static_cast<uint32_t>(EShaderPermutationFlags::UseGPUInstancing))
			{
				_targetVSHandle = m_vsMap[EShaderPermutationFlags::UseGPUInstancing];
			}
			else {
				_targetVSHandle = m_vsMap[EShaderPermutationFlags::Static];
				_builder.SetInputLayout(D3D12::Input::STATIC_LAYOUT);
			}

			// VSのセットとルートシグネチャの抽出。
			// ルートシグネチャはブロブから起こすので、非constで引く
			auto* _pVS = _resourceManager.Ref(_targetVSHandle);
			if (!_pVS || !_pVS->Get()) return {};

			_builder.SetRootSignature(a_pPSOManager->Request(_pVS->Get()));
			_builder.SetVS(_pVS->GetByteCode());
		}

		// =========================================================
		// Pixel Shader の解決
		// =========================================================
		// ZPreかつ不透明(Opaque)なら、PSのセットをスキップ
		bool _isZPrePass = (m_passNameHash == Core::String::ToHash("ZPre"));
		bool _isOpaque = !(a_key.permutationFlags & static_cast<uint32_t>(EShaderPermutationFlags::AlphaMasked));
		const bool _isDitherFade = (a_key.permutationFlags & static_cast<uint32_t>(EShaderPermutationFlags::DitherFade)) != 0;

		if (_isDitherFade && m_ditherFadePSHandle.IsValid())
		{
			// フェード中の物だけ、ディザーで画素を捨てるPSを張る(深度だけのパス用)。
			// 読めていないうちに組むと、捨てずに深度を書く PSO がキーに残ってしまうので覚えずに帰る
			auto* _pFadePS = _resourceManager.Get(m_ditherFadePSHandle);
			if (!_pFadePS) return {};
			_builder.SetPS(_pFadePS->GetByteCode());
		}
		else if (!(_isZPrePass && _isOpaque))
		{
			// どのPSで描くかはパス自身が持っている。
			// 持っていないパス(深度だけ書くパス)はPSを張らずに組む
			if (auto* _pPassPS = _resourceManager.Get(a_key.psHandle))
			{
				_builder.SetPS(_pPassPS->GetByteCode());
			}
		}

		// マネージャーにPSOをリクエスト。
		// 内容が同じなら、実体もハンドルもマネージャー側で共有される
		auto _psoHandle = a_pPSOManager->RequestHandle(_builder);

		m_psoMap[a_key] = _psoHandle;
		return _psoHandle;
	}

	void ShadingPipelineBuilder::RegisterVertexShader(EShaderPermutationFlags a_flag, Handle<Resource::Shader> a_vsHandle)
	{
		m_vsMap[a_flag] = a_vsHandle;
	}
	void ShadingPipelineBuilder::RegisterMeshShader(EShaderPermutationFlags a_flag, Handle<Resource::Shader> a_msHandle)
	{
		m_msMap[a_flag] = a_msHandle;
	}
	void ShadingPipelineBuilder::RegisterAmplificationShader(EShaderPermutationFlags a_flag, Handle<Resource::Shader> a_asHandle)
	{
		m_asMap[a_flag] = a_asHandle;
	}
	void ShadingPipelineBuilder::RegisterDitherFadePixelShader(Handle<Resource::Shader> a_psHandle)
	{
		m_ditherFadePSHandle = a_psHandle;
	}
	void ShadingPipelineBuilder::SetDepthConfig(bool a_enable, bool a_write, D3D12_COMPARISON_FUNC a_func)
	{
		m_depthEnable = a_enable;
		m_depthWrite = a_write;
		m_depthFunc = a_func;
	}
	void ShadingPipelineBuilder::SetCullMode(D3D12_CULL_MODE a_mode)
	{
		m_cullMode = a_mode;
	}
	bool ShadingPipelineBuilder::HasMeshShader() const
	{
		if (m_msMap.empty())
		{
			return false;
		}
		return true;
	}
}