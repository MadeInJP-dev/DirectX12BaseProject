#pragma once

#include "Engine/GameObject/BaseObject/BaseObject.h"

namespace App::Object
{
	//======================================================================================
	// 境界面の面の番号 : 立方体の6面。描くかどうかを面ごとに選べる
	//======================================================================================
	enum class EBoundaryFace : uint8_t
	{
		PositiveX,
		NegativeX,
		PositiveY,		// 天井
		NegativeY,		// 床(普段は地面の下に埋まるので描かない)
		PositiveZ,
		NegativeZ,
		Count,
	};

	//======================================================================================
	// 境界面の見え方
	//======================================================================================
	struct BoundaryLook
	{
		// 面に貼る画像。未設定なら何も描かない
		Core::GUID texGUID = Core::DEFAULT_GUID;
		Engine::ResourceRef<Engine::Resource::Texture> texRef = {};		// ランタイム用

		Math::Color color = { 0.3f, 0.8f, 1.0f, 0.8f };		// 画像に掛ける色(a は一番濃いときの濃さ)

		// 画像1枚の大きさ(ワールドの長さ)。面の大きさをこれで割った数だけ並べる。
		// 面ごとに縦横の比が違っても、画像が引き伸ばされない
		Math::Vector2 tileSize = { 10.0f, 10.0f };
		Math::Vector2 scrollSpeed = { 0.0f, 0.1f };			// UVのずらしの速さ(毎秒)

		// 見え方 : 基準点(プレイヤー)との距離が revealStartDistance より近いと見え始め、
		// revealEndDistance で一番濃くなる
		float revealStartDistance = 30.0f;
		float revealEndDistance = 5.0f;

		// 走査線・ノイズ・明滅(0 で掛けない)
		float scanLineDensity = 8.0f;		// 画像1枚あたりの走査線の本数
		float opacityScanLine = 0.2f;
		float opacityNoise = 0.1f;
		float flickeringSpeed = 10.0f;
		float flickeringStrength = 0.05f;
	};

	/// <summary>
	/// 行ける範囲の境界 : 立方体の各面を、プレイヤーが近づいたときだけ見える壁として描く
	/// </summary>
	/// <remarks>
	/// マップを囲う箱で、プレイヤーは内側から外へ出られない。
	/// 当たり判定は持たず、範囲を BoundaryResource へ書き写して、
	/// BoundaryClampSystem がプレイヤーの位置を範囲の内側へ挟む。
	///
	/// 回転は持たない(軸に沿った箱)。外へ出さない処理を単純な範囲の比較で済ませるため。
	///
	/// 背景として使うこともできる(ガレージの電子空間など)。
	/// 押し戻しを切り(m_isClampPlayer)、見え始めの距離を一番濃い距離以下にすれば、
	/// どこから見ても常に見える壁になる
	/// </remarks>
	class Boundary : public Engine::GameObject::BaseObject
	{
	public:

		// 更新処理 : 範囲を書き写し、画像のずらしと走査線などの時間を進める
		void Update(Engine::GameObject::ObjectContext& a_context) override;

		// 描画処理 : 描く面を1枚ずつサブミッターへ積む
		void Draw(Engine::GameObject::ObjectContext& a_context) override;

		// 初期化 : 画像の読み込みを始める
		void Awake(Engine::GameObject::ObjectContext& a_context) override;

		// 初期化 : 最初のフレームの押し戻しに間に合うよう、範囲を書き写しておく
		void Start(Engine::GameObject::ObjectContext& a_context) override;

		// 解放処理 : 範囲を外す(押し戻しを止める)
		void Release(Engine::GameObject::ObjectContext& a_context) override;

		// アーカイブ
		void Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context) override;

		//=======================================================================
		// 範囲
		//=======================================================================

		// 範囲(ワールド・軸に沿った箱)
		DirectX::BoundingBox GetBounds() const;

		const Math::Vector3& GetCenter() const { return m_center; }
		const Math::Vector3& GetSize() const { return m_size; }

		//=======================================================================
		// エディター用
		//=======================================================================

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "Boundary"; }

		// インスペクター : 範囲と見え方
		void DrawInspector(Engine::GameObject::ObjectContext& a_context) override;

		// ギズモ : 中心を動かす
		bool DrawGizmo(const Engine::GameObject::ObjectGizmoContext& a_ctx, Engine::GameObject::ObjectContext& a_context) override;

	private:

		// 範囲を BoundaryResource へ書き写す(押し戻すのは BoundaryClampSystem)
		void WriteBoundaryResource(Engine::GameObject::ObjectContext& a_context) const;

		// 画像を読み直す(GUID を差し替えたとき)
		void RequestTexture(Engine::GameObject::ObjectContext& a_context);

		// 近づき具合を測る基準点。プレイヤー、居なければメインカメラ。どちらも居なければ false
		bool TryGetRevealPos(Engine::GameObject::ObjectContext& a_context, Math::Vector3& a_outPos) const;

		// 1x1 の板(XY 平面・表は +Z)をその面へ移す行列と、面の大きさ(板の横・縦)
		Math::Matrix CalcFaceMatrix(EBoundaryFace a_face, Math::Vector2& a_outFaceSize) const;

		// 範囲の枠をデバッグ線で出す
		void DrawBoundsMarker(Engine::GameObject::ObjectContext& a_context) const;

	private:

		//-------------------------------------------------------------------
		// 設定(保存される)
		//-------------------------------------------------------------------
		Math::Vector3 m_center = {};							// 範囲の中心(ワールド)
		Math::Vector3 m_size = { 200.0f, 100.0f, 200.0f };		// 範囲の大きさ(各軸の全長)

		// 壁から内側へ空ける距離。プレイヤーの位置(足元)がこれより壁へ寄らない。
		// 機体の半径くらいにしておくと、体が面にめり込まない
		float m_wallMargin = 2.0f;

		// プレイヤーを範囲の内側へ押し戻すか。切ると見た目だけの壁(背景)になる
		bool m_isClampPlayer = true;

		// 面ごとに描くか(EBoundaryFace の順)。床は地面に埋まるので既定で描かない
		std::array<bool, static_cast<size_t>(EBoundaryFace::Count)> m_isDrawFace = { true, true, true, false, true, true };

		BoundaryLook m_look = {};

		//-------------------------------------------------------------------
		// 状態(保存しない)
		//-------------------------------------------------------------------
		float m_time = 0.0f;					// 走査線・明滅を動かす経過時間
		Math::Vector2 m_uvScroll = {};			// 今のUVのずらし(0〜1 に畳む)
	};
}
