#pragma once
#include "scene/scene.h"
#include <string>

namespace tsu {

class MultiplayerSystem
{
public:
    static void Update(Scene& scene, float dt);
    static uint64_t MakeNetworkId(const std::string& nickname);
    static bool IsConnected(const Scene& scene);
    static bool IsHost(const Scene& scene);
    static int GetPlayerCount(const Scene& scene);
    static float GetPingMs(const Scene& scene);
};

} // namespace tsu
