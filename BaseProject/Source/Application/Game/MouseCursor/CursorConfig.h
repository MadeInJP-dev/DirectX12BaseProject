#pragma once

namespace Engine::ECS
{
	struct EngineServices;
}

namespace App::Game
{
	/// <summary>
	/// 自前で描くマウスカーソルの設定(エンジン設定の "CursorConfig")。持ち主は MouseCursor
	/// </summary>
	/// <remarks>
	/// OSのカーソルはウィンドウのクライアント領域の上では消して、代わりに
	/// ここで指定した画像をカーソル位置へ描く。
	///
	/// 消すのは「自前の絵を出せているとき」だけにしてある。
	/// 画像が未設定だったり読み込みが終わっていないフレームまで消してしまうと、
	/// カーソルが1つも無い状態になってウィンドウを操作できなくなるため。
	/// </remarks>
	struct CursorConfig
	{
		// 自前のカーソルを使うか。
		// 切るとOSのカーソルがそのまま出る(絵は描かない)
		bool isEnable = true;

		// カーソルとして描くテクスチャ
		Core::GUID textureGUID = {};

		// 描くときの一辺の大きさ(描画解像度基準のpx)。
		// 画像は正方形として扱い、縦横ともこの大きさで描く
		float sizePixel = 64.0f;

		// ホットスポット : 画像の中で「実際に指している点」がどこかを正規化[0,1]で表したもの。
		// 矢印なら尖端。ここがカーソル座標に重なるように描く。
		// 既定値は同梱の MouseCursor.png の尖端を測った値
		Math::Vector2 hotspot = { 0.381f, 0.243f };

		// 乗算する色
		Math::Color color = Engine::Color::WHITE;

		// 設定画面・保存(ConfigRegistry から呼ばれる)
		void DrawEdit(const Engine::ECS::EngineServices& a_services);
		void Archive(Engine::Persistence::Archive& a_archive);
	};
}
