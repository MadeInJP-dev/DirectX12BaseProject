#include "DebugDrawConfig.h"

#include "Engine/EditorField/EditorField.h"

void Engine::Graphics::DebugDrawConfig::DrawEdit(const ECS::EngineServices& /*a_services*/)
{
	Engine::EditorField::Field("Draw Debug Wire", drawWire);
}

void Engine::Graphics::DebugDrawConfig::Archive(Persistence::Archive& a_archive)
{
	a_archive.Field("drawWire", drawWire);
}
