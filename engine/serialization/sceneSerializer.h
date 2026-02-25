#pragma once
#include <string>

namespace tsu {

class Scene;

class SceneSerializer
{
public:
    static void Load(Scene& scene, const std::string& filepath);
    static void Save(const Scene& scene, const std::string& filepath);
};

} // namespace tsu