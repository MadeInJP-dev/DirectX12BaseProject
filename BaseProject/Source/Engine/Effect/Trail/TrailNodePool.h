#pragma once

#include "Engine/Effect/Trail/TrailNode.h"
#include "Engine/Utility/Pool/ItemPool/ItemPool.h"

namespace Engine::Graphics
{
	class DrawSubmitter;
}

namespace Engine::Effect
{
	//==========================================================================================
	// TrailNodePool
	//
	// トレイルの点の履歴(TrailNode)の置き場。持ち主は GraphicsEngine(ライトと同じ扱い)。
	// エフェクトはハンドルだけを持ち、毎フレーム AddPoint で点を渡す。
	//
	// 持ち主が手放しても(エフェクトを止めた・エンティティが消えた)すぐには消さない。
	// 残りの点が寿命で消えきるまで描き続け、空になったら返す。
	// 時間を進めるのと帯を組むのは GraphicsEngine::Execute が1フレームに1回まとめて行う
	//==========================================================================================
	class TrailNodePool
	{
	public:

		// 新しいトレイルを借りる
		Handle<TrailNode> Acquire();

		TrailNode* Ref(const Handle<TrailNode>& a_handle) { return m_pool.Ref(a_handle); }

		// 手放す : 残りの点が消えきったら Update が返す。ハンドルは空にする
		void Detach(Handle<TrailNode>& a_handle);

		// 点を古くして、手放されて空になったものを返す
		void Update(float a_dt);

		/// <summary>
		/// 全トレイルの帯をカメラへ向けて組み、描画要求として積む
		/// </summary>
		/// <param name="a_cameraPos">帯の面を向けるカメラの位置</param>
		/// <param name="a_submitter">積む先</param>
		void Submit(const Math::Vector3& a_cameraPos, Graphics::DrawSubmitter& a_submitter);

		// すべて捨てる(終了時)
		void Release();

		// 生きているトレイルの数(手放されて消えていく途中のものも含む)
		size_t GetAliveCount() const;

	private:

		Pool::ItemPool<TrailNode> m_pool = {};

		// 帯を組む作業場所(毎フレーム確保し直さないよう持っておく)
		std::vector<Graphics::TrailVertex> m_workVertexVec = {};
	};
}
