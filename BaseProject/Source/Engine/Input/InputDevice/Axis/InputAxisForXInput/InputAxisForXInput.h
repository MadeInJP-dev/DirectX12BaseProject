#pragma once

#include "../InputAxisBase.h"

namespace Engine::Input
{
	/// <summary>
	/// XInputを使用したコントローラー軸制御
	/// </summary>
	/// <remarks>
	/// 値は -1〜1。遊び(デッドゾーン)の内側は 0 にし、外側を 0〜1 へ広げ直す。
	/// コントローラーの状態は InputDeviceManager が1フレーム1回読んだものを見る
	/// </remarks>
	class InputAxisForXInput : public InputAxisBase
	{
	public:

		// a_userIndex : 何番のコントローラーか(0〜3) / a_isLeft : 左スティックか
		InputAxisForXInput(UINT a_userIndex,bool a_isLeft) 
		{
			m_userIndex = a_userIndex;
			m_isLeft = a_isLeft;
		};

		// フレームの移動量を使って、軸の入力状況を更新
		void Update(InputContext& a_inputContext) override;

		/// <summary>
		/// 十字キーもこの軸として読むか
		/// </summary>
		/// <remarks>
		/// UI の項目移動のように、スティックでも十字キーでも同じ向きを出したいとき用。
		/// 十字キーが押されている間はスティックより優先する(値は -1 / 0 / 1)
		/// </remarks>
		void SetUseDPad(bool a_isUse) { m_isUseDPad = a_isUse; }

	private:
		// 左かどうか
		bool m_isLeft = false;

		// 十字キーも読むか
		bool m_isUseDPad = false;

		UINT m_userIndex = 0;
	};
}