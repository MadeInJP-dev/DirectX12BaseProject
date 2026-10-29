#pragma once

#include "Engine/Graphics/PipelineState/PSOKey.h"

namespace Engine::Graphics
{
	class PipelineStateManager;


	/// <summary>
	/// マテリアルやエンティティのフラグから、PSOを検索、生成する場所
	/// パスにつき一つ持たせる
	/// </summary>
	class ShadingPipelineBuilder
	{
	public:

		/// <summary>
		/// レンダーグラフのコンパイル時に、このパスのフォーマットを登録しておく
		/// </summary>
		/// <param name="a_rtvFormat">レンダーターゲット配列</param>
		/// <param name="a_dsvFormat">デプスステンシル</param>
		void Init(const std::vector<DXGI_FORMAT>& a_rtvFormat, DXGI_FORMAT a_dsvFormat, UINT a_passNameHash);

		/// <summary>
		/// シェーディングパイプラインが見つかれば返し、なければ作成して返します
		/// シェーダーがまだ読めていない間は無効なハンドルが返るので、
		/// 呼び出し側はそのアイテムを積まずに次のフレームへ回すこと
		/// </summary>
		/// <param name="a_key">PSO検索用キー</param>
		/// <returns>PSOマネージャーから帰ってきたハンドルを返す</returns>
		/// <param name="a_resourceManager">シェーダーの実体を引く先</param>
		Handle<ID3D12PipelineState> Request(PSOKey a_key, PipelineStateManager* a_pPSOManager, Resource::ResourceManager& a_resourceManager);

		/// <summary>
		/// フラグに対応したVSを登録する
		/// </summary>
		/// <param name="a_flag">フラグ</param>
		/// <param name="a_vsHandle">VSハンドル</param>
		void RegisterVertexShader(EShaderPermutationFlags a_flag, Handle<Resource::Shader> a_vsHandle);

		/// <summary>
		/// フラグに対応したMS（メッシュシェーダー）を登録する
		/// </summary>
		/// <param name="a_flag">フラグ</param>
		/// <param name="a_msHandle">MSハンドル</param>
		void RegisterMeshShader(EShaderPermutationFlags a_flag, Handle<Resource::Shader> a_msHandle);

		/// <summary>
		/// フラグに対応したAS（アンプリフィケーションシェーダー）を登録する
		/// </summary>
		/// <param name="a_flag">フラグ</param>
		/// <param name="a_asHandle">ASハンドル</param>
		void RegisterAmplificationShader(EShaderPermutationFlags a_flag, Handle<Resource::Shader> a_asHandle);

		/// <summary>
		/// フェード中(DitherFade)の描画で、パスのPSOの代わりに張るピクセルシェーダーを登録する。
		/// 深度だけを書くパス(ZPre)が、フェード中の物だけディザーで画素を捨てるために使う。
		/// 登録しないパスは DitherFade でも今までどおりパスのPSで組む
		/// </summary>
		/// <param name="a_psHandle">PSハンドル</param>
		void RegisterDitherFadePixelShader(Handle<Resource::Shader> a_psHandle);

		/// <summary>
		/// パスの標準深度設定を受け取る
		/// </summary>
		/// <param name="a_enable">深度テストするかどうか</param>
		/// <param name="a_write">深度書き込みをするかどうか</param>
		/// <param name="a_func">深度値テストの際のコンフィグ</param>
		void SetDepthConfig(bool a_enable, bool a_write, D3D12_COMPARISON_FUNC a_func);

		/// <summary>
		/// パスのカリング設定を受け取る
		/// 既定は D3D12_CULL_MODE_BACK。スカイドームのように内側から見る形状で
		/// 裏返したいパスだけが触る
		/// </summary>
		/// <param name="a_mode">カリングモード</param>
		void SetCullMode(D3D12_CULL_MODE a_mode);

		/// <summary>
		/// メッシュシェーダーが登録されているかのチェック
		/// </summary>
		/// <returns></returns>
		bool HasMeshShader() const;

	private:

		// PSOの作成用キーハッシュとハンドル
		std::unordered_map<PSOKey, Handle<ID3D12PipelineState>> m_psoMap = {};

		std::vector<DXGI_FORMAT> m_rtvFormats = {};
		DXGI_FORMAT m_dsvFormat;

		UINT m_passNameHash;

		// フラグに対応するVSのマップ
		std::unordered_map<EShaderPermutationFlags, Handle<Resource::Shader>> m_vsMap;

		// フラグに対応するMSのマップ
		std::unordered_map<EShaderPermutationFlags, Handle<Resource::Shader>> m_msMap;

		// フラグに対応するASのマップ
		std::unordered_map<EShaderPermutationFlags, Handle<Resource::Shader>> m_asMap;

		// フェード中(DitherFade)に張るPS。無効ならパスのPSのまま
		Handle<Resource::Shader> m_ditherFadePSHandle = {};

		// パイプラインモード

		// 深度テスト設定
		bool m_depthEnable = true;				// 深度テストするかどうか
		bool m_depthWrite = true;				// 深度テストした結果を書き込むかどうか
		D3D12_COMPARISON_FUNC m_depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;	// 深度テストする際のコンフィグ

		// カリング設定 : 既定はビルダーの標準と同じ背面カリング
		D3D12_CULL_MODE m_cullMode = D3D12_CULL_MODE_BACK;
	};
}