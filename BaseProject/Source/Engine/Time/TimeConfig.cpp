#include "TimeConfig.h"

#include "Engine/EditorField/EditorField.h"

void Engine::Time::TimeConfig::DrawEdit(const ECS::EngineServices& /*a_services*/)
{
	Engine::EditorField::Field("FPSRate", fpsRate);
	Engine::EditorField::HelpText("プレイヤーが設定画面で選んだ値があればそちらが効く");
}

void Engine::Time::TimeConfig::Archive(Persistence::Archive& a_archive)
{
	a_archive.Field("fpsRate", fpsRate);
}
