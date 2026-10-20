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

	private:
		// 左かどうか
		bool m_isLeft = false;

		UINT m_userIndex = 0;
	};
}