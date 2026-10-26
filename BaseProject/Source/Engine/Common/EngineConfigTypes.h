#pragma once
namespace Engine
{
	// ウィンドウモード
	enum class EWindowMode : UINT
	{
		Windowed,		// ウィンドウモード
		FullScreen,		// フルスクリーンモード
		Borederless,	// ボーダレスモード
	};

	// FPSレート設定
	// FPS_xx は値がそのままフレームレートの上限になる
	enum class EFPSRate : UINT
	{
		Unlimited	= 0,		// 制限なし
		VSync		= 1,		// 垂直同期
		FPS_30		= 30,
		FPS_60		= 60,
		FPS_120		= 120,
		FPS_144		= 144,
		FPS_240		= 240,
	};

	// フレームバッファ数
	inline constexpr UINT BACKBUFFER_COUNT = 3;	// バックバッファ数
	inline constexpr UINT CPU_FRAME_COUNT = 3;	// CPUカウント数

	// ※ 以前のビルド構成(EBuildConfiguration)は廃止した。
	//    実行時に切り替える機能は起動設定(Config/BuildConfig)の項目で持つ

	// アプリケーションモード
	enum class EAppMode : UINT
	{
		Game,			// ゲームを遊ぶ時と同じ画面
		Editor,			// エディター画面

		// エディターを出したまま、シーンビューの中で遊ぶ(デバッグプレイ)。
		//
		// 見た目は Editor、操作は Game。
		//   ・エディターのパネルはそのまま描かれる(インスペクターで値を見ながら動かせる)
		//   ・フリーカメラの割り込みはしない(ゲームのカメラがそのまま映る)
		//   ・入力はゲームへ渡る(カーソル中央固定も working)
		// 抜けるのは Ctrl+P(Editor へ戻る)。
		//
		// ※ モードを見て分岐している箇所は「Game かどうか」で書かれていることが多い。
		//   足すときは Game / Editor のどちらの仲間なのかを必ず確かめること
		DebugPlay,
	};
}

// magic_enum の既定の範囲は 127 までなので、FPS_144 / FPS_240 が名前を引けるように広げる
template<>
struct magic_enum::customize::enum_range<Engine::EFPSRate>
{
	static constexpr int min = 0;
	static constexpr int max = 240;
};