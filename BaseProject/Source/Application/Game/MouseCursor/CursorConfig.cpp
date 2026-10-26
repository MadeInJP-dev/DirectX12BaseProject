#include "CursorConfig.h"

#include "Engine/EditorField/EditorField.h"

namespace
{
	// 大きすぎ・小さすぎで見失わないよう幅を決めておく
	constexpr float MIN_SIZE = 8.0f;
	constexpr float MAX_SIZE = 512.0f;
}

void App::Game::CursorConfig::DrawEdit(const Engine::ECS::EngineServices& a_services)
{
	Engine::EditorField::Field("Enable", isEnable);
	Engine::EditorField::Tooltip("切るとOSのカーソルがそのまま出る");

	Engine::EditorField::Header("Texture");

	Engine::EditorField::AssetField(a_services, "Cursor", "Texture", textureGUID);
	if (!textureGUID.IsValid())
	{
		Engine::EditorField::HelpText("(未設定 : OSのカーソルを消さずにそのまま出す)");
	}

	Engine::EditorField::Header("Shape");

	Engine::EditorField::Field("Size", sizePixel, 1.0f, MIN_SIZE, MAX_SIZE, "%.0f px");
	sizePixel = std::clamp(sizePixel, MIN_SIZE, MAX_SIZE);
	Engine::EditorField::SameLine();
	Engine::EditorField::HelpText("(描画解像度基準)");

	// ホットスポットは「画像のどこがカーソルの先端か」。
	// 矢印の絵は余白の中に描かれていることが多く、中心(0.5,0.5)ではまず合わない
	Engine::EditorField::Field("Hotspot", hotspot, 0.005f, 0.0f, 1.0f);
	hotspot.x = std::clamp(hotspot.x, 0.0f, 1.0f);
	hotspot.y = std::clamp(hotspot.y, 0.0f, 1.0f);
	Engine::EditorField::HelpText("画像の中で実際に指している点(正規化)。矢印なら尖端");

	Engine::EditorField::ColorField("Color", color);
}

void App::Game::CursorConfig::Archive(Engine::Persistence::Archive& a_archive)
{
	a_archive.Field("isEnable", isEnable);
	a_archive.GUIDField("textureGUID", textureGUID);
	a_archive.Field("sizePixel", sizePixel);
	a_archive.Field("hotspot", hotspot);
	a_archive.Field("color", color);
}
