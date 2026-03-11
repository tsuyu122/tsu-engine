#pragma once
#include "scene/scene.h"
#include "core/window.h"
#include <string>
#include <vector>

namespace tsu {

// ================================================================
// GameApplication — lean standalone game runtime.
// No editor, no ImGui, no gizmos. Only what the game needs to run.
// ================================================================
class GameApplication
{
public:
    GameApplication(const std::vector<std::string>& args = {});
    void Run();

private:
    // Game-logic updates (identical to Application counterparts)
    void UpdatePlayerControllers(float dt);
    void UpdateMouseLook(float dt);
    void UpdateTriggers(float dt);
    void UpdateAnimationControllers(float dt);
    void UpdateAnimators(float dt);
    void UpdateAudio(float dt);
    void UpdateSkinnedMeshes(float dt);
    void ApplyLaunchOverrides();
    void SpawnMultiplayerPlayers(); // spawn player slots from PlayerPrefab if configured

    Window      m_Window;
    Scene       m_Scene;
    float       m_LastFrameTime = 0.0f;
    std::string m_Title         = "Game";
    std::vector<std::string> m_Args;
};

} // namespace tsu
