#pragma once

#include "Application/Object/UI/UIBase.h"

namespace App::Object
{
	/// <summary>
	/// 画像を1枚出すだけのUI
	/// </summary>
	/// <remarks>
	/// タイトルの背景やロゴのように「置くだけ」のものに使う。
	/// UIBase そのままで足りるので、名前を付けてエディターの一覧へ出すためだけに用意している。
	/// </remarks>
	class UIImage : public UIBase
	{
	public:

		// ヒエラルキー/インスペクター表示名
		const char* GetEditorName() const override { return "UIImage"; }
	};
}
