#pragma once

#include "Application/Object/UI/Decoration/Decoration.h"
#include "Application/Object/UI/Decoration/DecorationParent.h"

//==========================================================================================
// デコレーションの描画
//
// 親(UI)から受け取る位置・設定と、その場かぎりの差し替え、描く関数。
// 実装は DecorationDraw.cpp
//==========================================================================================
// 描画命令の積み先。ヘッダーでは前方宣言だけにして、実装側で GraphicsEngine.h を読む
namespace Engine::Graphics { class GraphicsEngine; }
namespace Engine::Resource { class ResourceManager; struct UIMaterial; }

namespace App::Object::Decoration
{
	//======================================================================================
	// 操作
	//======================================================================================

	/// <summary>
	/// デコレーションを1つ描く
	/// </summary>
	/// <param name="a_pGraphicsEngine">描画命令の積み先</param>
	/// <param name="a_pResourceManager">テクスチャ・フォントを引くのに使う</param>
	/// <param name="a_decoration">描くもの</param>
	/// <param name="a_parent">親(UI)のトランスフォーム</param>
	/// <param name="a_override">その場かぎりの差し替え</param>
	void DrawDecoration(
		Engine::Graphics::GraphicsEngine* a_pGraphicsEngine,
		Engine::Resource::ResourceManager* a_pResourceManager,
		const Decoration& a_decoration,
		const ParentTransform& a_parent,
		const ParentOption& a_parentOp,
		const DrawOverride& a_override = {});

	/// <summary>
	/// 保存されているGUIDから、テクスチャとフォントの参照を引き直す
	/// </summary>
	/// <remarks>読み込み直後とエディターでの差し替え時に呼ぶ。実体の到着は待たない</remarks>
	void RequestResources(Decoration& a_decoration, Engine::Resource::ResourceManager* a_pResourceManager);
}
