#pragma once
#include "scene/scene.h"
#include <string>

namespace tsu {

class PrefabSerializer
{
public:
    // Save a PrefabAsset to a .tprefab file
    static void Save(const PrefabAsset& prefab, const std::string& filepath);
    // Load a PrefabAsset from a .tprefab file. Returns false on failure.
    static bool Load(PrefabAsset& prefab, const std::string& filepath);
};

} // namespace tsu
