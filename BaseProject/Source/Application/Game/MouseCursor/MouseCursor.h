#pragma once

#include "CursorConfig.h"

namespace Engine::Resource
{
	class Texture;
}

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Engine::Graphics
{
	class DrawSubmitter;
}

namespace App::Game
{
	/// <summary>
	/// ゲーム中に自前で描くマウスカーソル
	/// </summary>
	/// <remarks>
	/// ゲームモードの間だけ、OSのカーソルをクライアント領域で消して
	/// 代わりに設定された画像をカーソル位置へ描く。設定は CursorConfig(持ち主はこのクラス)。
	/// エディター(デバッグプレイを含む)の間は何もしない = OSのカーソルがそのまま出る。
	///
	/// OSのカーソルを消すのは「自前の絵を出せているとき」だけにしてある。
	/// 画像が未設定・読み込み中のフレームまで消すとカーソルが1つも無くなり、
	/// ウィンドウを操作できなくなるため。
	/// </remarks>
	class MouseCursor
	{
	public:

		// 初期化・解放
		// a_pServices : カーソル画像の読み込み先・入力・ウィンドウ・設定の登録先を引く(借り物)。
		// 設定はエンジン設定へ登録し、解放で外す
		void Init(const Engine::ECS::EngineServices* a_pServices);
		// テクスチャの参照を握っているので、リソースの解放より前に呼ぶこと
		void Release();

		// 設定(画像・大きさ・ホットスポット)
		const CursorConfig& GetConfig() const { return m_config; }

		/// <summary>
		/// 毎フレームの更新 : 設定の反映と、このフレームの位置・可否を決める
		/// </summary>
		/// <remarks>
		/// 入力の更新とモード切替が済んだ後、描画より前に一度だけ呼ぶ。
		/// OSのカーソルを消すかどうかもここでウィンドウへ伝える。
		/// </remarks>
		void Update();

		// ゲーム画面へ描く : UIを積み終えた最後に呼ぶ(UIパスは積んだ順に重なる)
		void SubmitUI(Engine::Graphics::DrawSubmitter* a_pDrawSubmitter) const;

	private:

		// 自前のカーソルを出す条件がそろっているか調べ、m_isHideOSCursor / m_isDraw を決める
		void Evaluate();

		// カーソルのクライアント座標を取り、クライアント領域の内側かどうかも見る
		bool TryGetCursorClientPos(Math::Vector2& a_outClientPos) const;

	private:

		const Engine::ECS::EngineServices* m_pServices = nullptr;	// 借り物

		CursorConfig m_config = {};

		// 描画に使うテクスチャ。設定のGUIDが変わったら読み直す
		Engine::ResourceRef<Engine::Resource::Texture> m_texRef = {};
		Core::GUID m_loadedGUID = {};

		// OSのカーソルを消してよいか(＝自前の絵を出せる状態か)
		bool m_isHideOSCursor = false;

		// このフレームに絵を描くか
		bool m_isDraw = false;

		// カーソル位置(クライアント座標 px)
		Math::Vector2 m_clientPos = {};
	};
}
