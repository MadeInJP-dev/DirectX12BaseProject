#pragma once

#include "ResourceBuildContext.h"
#include "Engine/Graphics/Device/AsyncGPUManager/AsyncGPUManager.h"

namespace Engine::Resource
{
	/// <summary>
	/// 単体ロード用のスコープバッチ
	///
	/// モデル経由ではなく単体でリソースを読み込むとき(エディタからのメッシュ差し替えなど)、
	/// 呼び出し元がビルドコンテキストを持っていないことがある。
	/// そのときだけこのクラスでその場でバッチを開き、スコープを抜けるところで実行する。
	///
	/// 逆に言うと、まとめて読むべきものは必ず呼び出し元でバッチを開いてコンテキストを渡すこと。
	/// これを多用するとメッシュ1個ごとのsubmitに逆戻りする。
	/// </summary>
	class ScopedResourceBuild
	{
	public:

		// 登録を伴わず、転送だけのバッチを開くときに渡す目印
		struct TransferOnly {};

		// a_pResourceManager : 作ったものを登録する先。コンテキストへそのまま載せる
		explicit ScopedResourceBuild(ResourceManager* a_pResourceManager, bool a_useCopy = true, bool a_useCompute = true);

		// 転送だけ : 登録先(ResourceManager)を持たないコンテキストを組む。
		// 既にあるリソースの中身を書き換えるだけで、新しく登録するものが無い処理向け
		// (使う側がコンテキストから ResourceManager を引かないこと)
		explicit ScopedResourceBuild(TransferOnly, bool a_useCopy = true, bool a_useCompute = true);

		~ScopedResourceBuild();

		// コピーもムーブも禁止 : スコープと寿命を一致させる
		ScopedResourceBuild(const ScopedResourceBuild&) = delete;
		ScopedResourceBuild& operator=(const ScopedResourceBuild&) = delete;
		ScopedResourceBuild(ScopedResourceBuild&&) = delete;
		ScopedResourceBuild& operator=(ScopedResourceBuild&&) = delete;

		// ビルドコンテキストの取得
		const ResourceBuildContext& GetContext() const { return m_context; }

	private:

		// バッチを開いてコンテキストを組む : 登録先は空でもよい
		void Open(ResourceManager* a_pResourceManager, bool a_useCopy, bool a_useCompute);

	private:

		Graphics::AsyncBuildBatch	m_batch = {};
		ResourceBuildContext		m_context = {};
	};

	/// <summary>
	/// 渡されたコンテキストが使えるならそれを、使えないなら自前のスコープバッチを使う
	///
	/// ローダ経路はコンテキストを省略できるようにしてあるため、
	/// 「渡されていればそれに積む、渡されていなければ自分で開く」を1箇所にまとめる。
	/// </summary>
	class ResourceBuildScope
	{
	public:

		explicit ResourceBuildScope(const ResourceBuildContext* a_pContext)
		{
			if (a_pContext && a_pContext->pDevice)
			{
				m_pContext = a_pContext;
				return;
			}

			// 呼び出し元が持っていないので、その場で開く。
			// 登録先のマネージャーだけは呼び出し元のものを引き継ぐ
			m_upOwned = std::make_unique<ScopedResourceBuild>(a_pContext ? a_pContext->pResourceManager : nullptr);
			m_pContext = &m_upOwned->GetContext();
		}

		// 転送だけ : 渡されたコンテキストが使えなければ、登録先を持たないバッチを自分で開く。
		// コピーしか積まないので、自分で開くときはコンピュートのリストを取らない
		ResourceBuildScope(const ResourceBuildContext* a_pContext, ScopedResourceBuild::TransferOnly a_tag)
		{
			if (a_pContext && a_pContext->pDevice)
			{
				m_pContext = a_pContext;
				return;
			}

			m_upOwned = std::make_unique<ScopedResourceBuild>(a_tag, true, false);
			m_pContext = &m_upOwned->GetContext();
		}

		const ResourceBuildContext& GetContext() const { return *m_pContext; }

	private:

		std::unique_ptr<ScopedResourceBuild>	m_upOwned = nullptr;
		const ResourceBuildContext*				m_pContext = nullptr;
	};
}
