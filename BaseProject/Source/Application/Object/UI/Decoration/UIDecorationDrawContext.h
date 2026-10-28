#pragma once

#include "Application/Object/UI/Decoration/DecorationParent.h"
#include "Application/Object/UI/Decoration/DecorationReaction.h"

namespace Engine::Graphics { class GraphicsEngine; }
namespace Engine::Resource { class ResourceManager; }

namespace App::Object::Decoration
{
	class UIDecoration;

	//==========================================================================================
	// 描画時に渡すもの
	//
	// 持ち主(UIBase)が親のトランスフォーム・設定・差し替えを入れて UIDecoration::Draw へ渡す。
	// UIDecoration が自分の倍率とマテリアルを足し込み、pParent を埋めてからエレメントへ配る
	//==========================================================================================
	struct DrawContext
	{
		Engine::Graphics::GraphicsEngine* pGraphicsEngine = nullptr;
		Engine::Resource::ResourceManager* pResourceManager = nullptr;

		ParentTransform parentTr = {};		// 親(UI)のトランスフォーム
		ParentOption parentOp = {};			// 親しか持たない設定(湾曲・切り抜き・マテリアル)
		DrawOverride drawOverride = {};		// その場かぎりの差し替え

		// 親の情報 : UIDecoration が自分を入れる
		const UIDecoration* pParent = nullptr;
	};

	//==========================================================================================
	// 更新時に渡すもの
	//==========================================================================================
	struct UpdateContext
	{
		float dt = 0.0f;							// デルタタイム

		// 親(UI)の今の状態 : カーソルへの反応(UIReaction)が見る。
		// 乗っている・押しているに加えて押せない(Disabled)も要るので、真偽値ではなく状態で受け取る。
		// UIBase::GetUIState がそのまま入る(ReactToChildren も込み)
		EUIState parentState = EUIState::Normal;
	};
}
