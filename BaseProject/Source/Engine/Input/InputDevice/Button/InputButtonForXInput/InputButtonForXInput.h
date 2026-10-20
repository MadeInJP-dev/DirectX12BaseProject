#pragma once
#include "../InputButtonBase.h"

namespace Engine::Input
{
	//=================================================================================================
	// コントローラーのボタン
	// 
	// 番号は GamepadKey(Core/GamepadKey.h)。複数渡すとどれか1つで押下になる。
	// トリガーもしきい値を超えたらボタンとして押したことにする。
	// コントローラーの状態は InputDeviceManager が1フレーム1回読んだものを見る
	//=================================================================================================
	class InputButtonForXInput : public InputButtonBase
	{
	public:

		// a_userIndex : 何番のコントローラーか(0〜3)
		InputButtonForXInput(UINT a_userIndex, int a_padKey);
		InputButtonForXInput(UINT a_userIndex, const std::vector<int>& a_padKeyVec);
		~InputButtonForXInput() override = default;

		void Update(InputContext& a_inputContext) override;

		void GetCode(std::vector<int>& a_ret)const override;


	private:

		UINT m_userIndex = 0;

		std::vector<int> m_padKeyVec = {};
	};
}