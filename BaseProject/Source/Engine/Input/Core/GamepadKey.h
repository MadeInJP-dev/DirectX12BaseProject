#pragma once
namespace Engine::Input
{
	//=================================================================================================
	// コントローラーのボタン・スティックの番号
	//
	// 割り当て(ユーザーデータ)へはキーボードの仮想キーと同じく int で保存する。
	// 値は XInput の VK_PAD_* と同じにしてある(InputButtonForXInput.cpp で確かめている)。
	// XInput.h を読まずに使えるよう、ここで持つ
	//=================================================================================================
	namespace GamepadKey
	{
		// ボタン
		constexpr int A				= 0x5800;
		constexpr int B				= 0x5801;
		constexpr int X				= 0x5802;
		constexpr int Y				= 0x5803;
		constexpr int RShoulder		= 0x5804;		// RB
		constexpr int LShoulder		= 0x5805;		// LB
		constexpr int LTrigger		= 0x5806;		// LT : しきい値を超えたら押したとする
		constexpr int RTrigger		= 0x5807;		// RT

		// 十字キー
		constexpr int DPadUp		= 0x5810;
		constexpr int DPadDown		= 0x5811;
		constexpr int DPadLeft		= 0x5812;
		constexpr int DPadRight		= 0x5813;

		constexpr int Start			= 0x5814;
		constexpr int Back			= 0x5815;
		constexpr int LThumbPress	= 0x5816;		// 左スティック押し込み
		constexpr int RThumbPress	= 0x5817;		// 右スティック押し込み

		// スティックの向き : 軸の割り当てで、どちらのスティックかを表すのに使う
		constexpr int LThumbUp		= 0x5820;
		constexpr int LThumbDown	= 0x5821;
		constexpr int LThumbRight	= 0x5822;
		constexpr int LThumbLeft	= 0x5823;

		constexpr int RThumbUp		= 0x5830;
		constexpr int RThumbDown	= 0x5831;
		constexpr int RThumbRight	= 0x5832;
		constexpr int RThumbLeft	= 0x5833;

		/// <summary>
		/// 画面に出す名前(設定画面のキー名など)
		/// </summary>
		/// <remarks>スティックの向きは、どちらのスティックかだけを返す(軸の割り当てはスティック1本ごとのため)</remarks>
		inline const char* GetName(int a_key)
		{
			switch (a_key)
			{
			case A:				return "A";
			case B:				return "B";
			case X:				return "X";
			case Y:				return "Y";
			case RShoulder:		return "RB";
			case LShoulder:		return "LB";
			case LTrigger:		return "LT";
			case RTrigger:		return "RT";
			case DPadUp:		return "D-Pad Up";
			case DPadDown:		return "D-Pad Down";
			case DPadLeft:		return "D-Pad Left";
			case DPadRight:		return "D-Pad Right";
			case Start:			return "Start";
			case Back:			return "Back";
			case LThumbPress:	return "LS";
			case RThumbPress:	return "RS";
			case LThumbUp: case LThumbDown: case LThumbRight: case LThumbLeft:	return "L Stick";
			case RThumbUp: case RThumbDown: case RThumbRight: case RThumbLeft:	return "R Stick";
			default:			return "-";
			}
		}
	}
}
