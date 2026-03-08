#pragma once
#include "scene/scene.h"
#include "core/window.h"
#include <string>

namespace tsu {

// ================================================================
// GameApplication — lean standalone game runtime.
// No editor, no ImGui, no gizmos. Only what the game needs to run.
// ================================================================
class GameApplication
{
public:
    GameApplication();
    void Run();

private:
    // Game-logic updates (identical to Application counterparts)
    void UpdatePlayerControllers(float dt);
    void UpdateMouseLook(float dt);
    void UpdateTriggers(float dt);
    void UpdateAnimators(float dt);

    Window      m_Window;
    Scene       m_Scene;
    float       m_LastFrameTime = 0.0f;
    std::string m_Title         = "Game";
};

} // namespace tsu
