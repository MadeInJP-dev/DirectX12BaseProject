#pragma once
namespace App::Object::Decoration
{
	class UIDecoration;

	// 描画時に相対座標を知るためのもの
	struct DrawContext
	{
		Engine::Graphics::GraphicsEngine* pGraphicsEngine = nullptr;
		Engine::Resource::ResourceManager* pResourceManager = nullptr;

		// 親の情報
		UIDecoration* pParent = nullptr;
	};

	struct UpdateContext
	{
		float dt = 0.0f;			// デルタタイム

		bool isHover = false;		// カーソルが重なっているかどうか
		bool isClick = false;		// 今押されたかどうか
		bool isClicked = false;		// 押され続けているかどうか
	};
}