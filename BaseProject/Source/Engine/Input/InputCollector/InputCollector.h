#pragma once

#include "../Core/InputAction.h"
#include "../Core/InputDevice.h"
#include "../InputDevice/Button/InputButtonBase.h"

namespace Engine::Input
{

	class InputAxisBase;

	struct InputContext;

	/// <summary>
	/// 単一の入力デバイスからの入力をコレクションするクラス
	/// キーボードやゲームパッド等のそれぞれのInputCollectorが必要
	/// ゲームで使う入力Indexの管理もここで行う
	/// </summary>
	/// <remarks>
	/// ボタンと軸は ActionID(uint32_t)を鍵にして持つ。
	/// 引数の ActionKey は番号・enum class・文字列のどれでも受け取れるので、
	/// 呼ぶ側は今までどおり名前で書いても、ゲーム側の enum をそのまま渡してもよい。
	/// </remarks>
	class InputCollector
	{
	public:

		enum class EActiveState
		{
			Disable,		// 無効 : 完全に停止している状態
			Monitoring,		// 監視 : デバイスの入力を更新、アプリに入力の影響はない
			Enable,			// 有効 : アプリに入力の影響を与える
		};

		// a_device : 入力を集めるデバイス。繋がっていない間は入力なしになる
		explicit InputCollector(EInputDevice a_device = EInputDevice::KeyboardMouse);
		~InputCollector();

		void Update(InputContext& a_inputContext);

		/// <summary>
		/// このデバイスに溜まっている入力状態を捨てる
		/// </summary>
		/// <remarks>
		/// アプリのモード切り替えで呼ぶ。押しっぱなし(Hold)のまま切り替えると、
		/// 戻ってきたときに「押した瞬間(Press)」を挟まずに Hold から始まってしまう。
		/// </remarks>
		void ResetInput();

		// 何かしらの入力を検知したか
		bool IsSomethigInput();

		// 任意の入力状況の取得
		InputButtonBase::EState GetButtonState(ActionKey a_action) const;
		Math::Vector2 GetAxisState(ActionKey a_action) const;

		// 入力を集めるデバイス
		EInputDevice GetDevice() const { return m_device; }

		// 入力デバイスの状態の取得と設定
		EActiveState GetActiveState() const { return m_state; }
		void SetActiveState(EActiveState a_state) { m_state = a_state; }

		/// <summary>
		/// モード切り替えの入力リセットで捨てないデバイスにする
		/// </summary>
		/// <remarks>
		/// リセットは「切り替え前の操作を向こう側へ持ち越さない」ためのものなので、
		/// ゲームの操作は捨ててよい。
		///
		/// ただし切り替えそのものに使うキー(Ctrl+P)を捨てると、押しっぱなしの状態が
		/// 「押した瞬間」へ戻り、押している間ずっと切り替わり続けてしまう。
		/// システム用のデバイスはこれを立てて、リセットの対象から外す。
		/// </remarks>
		void SetKeepOnReset(bool a_isKeep) { m_isKeepOnReset = a_isKeep; }
		bool IsKeepOnReset() const { return m_isKeepOnReset; }

		// アプリケーションボタンの追加・上書き(所有権を受け取る)
		void AddButton(ActionKey a_action, std::unique_ptr<InputButtonBase> a_upButton);

		// 入力軸の追加・上書き(所有権を受け取る)
		void AddAxis(ActionKey a_action, std::unique_ptr<InputAxisBase> a_upAxis);

		// 登録されているボタン・軸(無ければ nullptr)
		const InputButtonBase* GetButton(ActionKey a_action) const;
		const InputAxisBase* GetAxis(ActionKey a_action) const;

	private:

		// 解放
		void Release();

	private:
		// 登録されているデバイス(鍵はアクションID)
		std::unordered_map<ActionID, std::unique_ptr<InputButtonBase>> m_upButtonMap;
		std::unordered_map<ActionID, std::unique_ptr<InputAxisBase>> m_upAxisMap;

		// 入力を集めるデバイス
		EInputDevice m_device = EInputDevice::KeyboardMouse;

		// 有効
		EActiveState m_state = EActiveState::Enable;

		// モード切り替えのリセットで捨てないか(システム用の入力だけ true)
		bool m_isKeepOnReset = false;
	};
}