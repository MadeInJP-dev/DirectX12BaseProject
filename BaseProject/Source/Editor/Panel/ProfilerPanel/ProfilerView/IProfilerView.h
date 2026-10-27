#pragma once

#include "Editor/EditorCommon.h"
#include "../../../Internal/EditorContext.h"

namespace Editor
{
	//======================================================================================
	//
	// プロファイラパネルに並べる表示1つ分の基底
	//
	// パネルは左の一覧に GetName を並べ、選ばれたものの Draw を右に描くだけ。
	// 何をどう計って何を出すかは、それぞれの派生クラスが持つ。
	// 表示を足すときは、これを継承して ProfilerPanel のコンストラクタで登録する
	//
	// Draw は選ばれている間だけ呼ばれる。取り直しを頼む表示(ECS / Graphics)は、
	// 見ていない間は計測の手間をかけない
	//
	//======================================================================================
	class IProfilerView
	{
	public:

		virtual ~IProfilerView() = default;

		// 左の一覧と見出しに出す名前
		virtual const char* GetName() const = 0;

		// 見出しの下に添える、この表示で何が分かるかの一言
		virtual const char* GetDescription() const = 0;

		// 中身を描く(ウィンドウと見出しはパネルが出す)
		virtual void Draw(EditorContext& a_editContext) = 0;
	};
}
