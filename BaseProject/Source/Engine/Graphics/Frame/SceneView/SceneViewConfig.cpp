#include "SceneViewConfig.h"

#include "Engine/EditorField/EditorField.h"

void Engine::Graphics::SceneViewConfig::DrawEdit(const ECS::EngineServices& /*a_services*/)
{
	Engine::EditorField::Field("useJitter (TAA)", useJitter);
}

void Engine::Graphics::SceneViewConfig::Archive(Persistence::Archive& a_archive)
{
	a_archive.Field("useJitter", useJitter);
}
