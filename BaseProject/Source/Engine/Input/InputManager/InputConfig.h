#pragma once

namespace Engine::ECS
{
	struct EngineServices;
}

namespace Engine::Input
{
	/// <summary>
	/// 入力関係の設定(エンジン設定の "InputConfig")。持ち主は InputManager
	/// </summary>
	/// <remarks>
	/// 視点移動は生のマウス入力(WM_INPUT)から作っているため、Windowsの
	/// 「ポインターの速度」スライダーや「ポインターの精度を高める」(加速)の影響を受けない。
	/// 振り向きの速さを変えられるのはここだけなので、感度は必ずこの設定で調整する。
	/// </remarks>
	struct InputConfig
	{
		// カーソルを画面中央へ固定するか(プレイ中のみ働く)
		bool isCursorLockedToCenter = false;

		//------------------------------------------------------------------
		// 視点感度
		//------------------------------------------------------------------
		// 単位は「マウスのカウント1つあたり何度回るか」。
		// 例: 0.5 なら 800DPI のマウスを 1 インチ動かすと 800 * 0.5 = 400 度回る。
		//
		// 既定値が 0.5 なのは、生のマウス入力へ切り替える前の見え方に合わせるため。
		// 以前はカーソル座標の差分を使っており、Windowsのポインター速度
		// (既定の半分の設定なら約 0.5 倍)を通った後の値がそのまま角度になっていた。
		float lookSensitivityX = 0.5f;		// 左右
		float lookSensitivityY = 0.5f;		// 上下

		// 上下の反転(いわゆるインバート)
		bool isInvertLookY = false;

		//------------------------------------------------------------------
		// 表示用
		//------------------------------------------------------------------
		// 感度の目安(360度回すのに必要なマウスの移動距離)を出すためだけに使う。
		// 実際の入力には一切影響しない。使っているマウスのDPIを入れておくと、
		// 「何cmで振り向けるか」で感度を決められる。
		int mouseDpi = 800;

		// 設定画面・保存(ConfigRegistry から呼ばれる)
		void DrawEdit(const ECS::EngineServices& a_services);
		void Archive(Persistence::Archive& a_archive);
	};
}
