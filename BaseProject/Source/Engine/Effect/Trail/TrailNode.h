#pragma once

#include "Engine/Graphics/Frame/DrawList/TrailData.h"

namespace Engine::Effect
{
	//==========================================================================================
	// トレイルの点1つ
	//==========================================================================================
	struct TrailPoint
	{
		Math::Vector3 pos = {};	// ワールド座標
		float age = 0.0f;		// 置いてからの秒数。帯の見た目(色・幅・U)はこれで決まる
	};

	//==========================================================================================
	// 帯の見た目
	//
	// 持ち主(エフェクト)がパーツの値を毎フレーム写す。
	// 持ち主が手放した後は、最後に写した値のまま尾から消えていく
	//==========================================================================================
	struct TrailStyle
	{
		float lifeTime = 0.3f;				// 1つの点が残る秒数
		int   maxPoints = 32;				// 点の数の上限
		float minVertexDistance = 0.1f;		// 前の点からこの距離動くまでは新しい点を足さない
		float width = 0.5f;					// 先頭の幅
		float endWidth = 0.0f;				// 尾の幅
		Math::Color headColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		Math::Color tailColor = { 1.0f, 1.0f, 1.0f, 0.0f };
		float intensity = 1.0f;				// 色に掛ける強さ
		bool  isAdditive = false;			// 加算で重ねるか
		Handle<Resource::Texture> textureHandle = {};	// 帯に貼る絵(無効なら色だけ)
	};

	//==========================================================================================
	// TrailNode
	//
	// トレイル1本ぶんの点の履歴。古い点が先頭、新しい点(帯の先頭)が末尾に並ぶ配列を持つ。
	//
	// ・持ち主が毎フレーム AddPoint で今の位置を渡す。前の点から minVertexDistance 動くまでは
	//   末尾の点を動かすだけにして、点が細かく増えすぎないようにする
	// ・Update で点を古くし、lifeTime を過ぎた点を先頭(尾)から捨てる
	// ・帯はカメラへ向けて BuildVertices で組む(三角形ストリップ)
	//
	// 実体は TrailNodePool が持ち、エフェクトはハンドルだけを持つ
	// (コンポーネントはトリビアルコピーでなければならず、可変長の配列を直接持てないため)
	//==========================================================================================
	class TrailNode
	{
	public:

		// 見た目を写す。点の数の上限が下がったら、溢れたぶんを古い方から捨てる
		void SetStyle(const TrailStyle& a_style);
		const TrailStyle& GetStyle() const { return m_style; }

		// 今の位置を渡す(帯の先頭)
		void AddPoint(const Math::Vector3& a_pos);

		// 点を古くし、寿命を過ぎた点を尾から捨てる
		void Update(float a_dt);

		// 持ち主が手放した印。手放した後は点が増えず、消えきったらプールが返す
		void Detach() { m_isDetached = true; }
		bool IsDetached() const { return m_isDetached; }

		bool IsEmpty() const { return m_pointVec.empty(); }
		const std::vector<TrailPoint>& GetPointVec() const { return m_pointVec; }

		/// <summary>
		/// カメラへ向けた帯の頂点を組む(点ごとに左右2頂点。三角形ストリップの並び)
		/// </summary>
		/// <param name="a_cameraPos">帯の面を向けるカメラの位置</param>
		/// <param name="a_outVertexVec">組んだ頂点を末尾へ足す先(中身は消さない)</param>
		void BuildVertices(const Math::Vector3& a_cameraPos, std::vector<Graphics::TrailVertex>& a_outVertexVec) const;

	private:

		// 上限を超えたぶんを古い方から捨てる
		void TrimToMaxPoints();

	private:

		TrailStyle m_style = {};

		// 点の履歴 : 先頭が一番古い点(尾)、末尾が今の位置(帯の先頭)
		std::vector<TrailPoint> m_pointVec = {};

		bool m_isDetached = false;
	};
}
